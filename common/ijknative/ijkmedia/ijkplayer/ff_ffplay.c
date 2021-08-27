/*
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ff_ffplay.h"

/**
 * @file
 * simple media player based on the FFmpeg libraries
 */

#include "config.h"
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#if CONFIG_AVDEVICE
#include "libavdevice/avdevice.h"
#endif
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavcodec/avcodec.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include "ijksdl/ijksdl_log.h"
#include "ijkavformat/ijkavformat.h"
#include "ff_cmdutils.h"
#include "ff_fferror.h"
#include "ff_ffpipeline.h"
#include "ff_ffpipenode.h"
#include "ff_ffplay_debug.h"
#include "ffplay_format_def.h"
#include "version.h"
#include "ijkmeta.h"

#ifndef AV_CODEC_FLAG2_FAST
#define AV_CODEC_FLAG2_FAST CODEC_FLAG2_FAST
#endif

#ifndef AV_CODEC_CAP_DR1
#define AV_CODEC_CAP_DR1 CODEC_CAP_DR1
#endif

// FIXME: 9 work around NDKr8e or gcc4.7 bug
// isnan() may not recognize some double NAN, so we test both double and float
#if defined(__ANDROID__)
#ifdef isnan
#undef isnan
#endif
#define isnan(x) (isnan((double)(x)) || isnanf((float)(x)))
#endif

#if defined(__ANDROID__)
#define printf(...) ALOGD(__VA_ARGS__)
#endif

#define FFP_IO_STAT_STEP (50 * 1024)

#define FFP_BUF_MSG_PERIOD (3)

// static const AVOption ffp_context_options[] = ...
#include "ff_ffplay_options.h"
#include "ijkutil.h"

static AVEMPacket flush_pkt;

#if CONFIG_AVFILTER
// FFP_MERGE: opt_add_vfilter
#endif

#if CONFIG_AVFILTER
static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_em_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}
#endif

static void free_picture(Frame *vp);

static void toggle_pause(FFPlayer *ffp, int pause_on);

static int packet_queue_put_private(PacketQueue *q, AVEMPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
       return -1;

#ifdef FFP_MERGE
    pkt1 = av_em_alloc(sizeof(MyAVPacketList));
#else
    pkt1 = q->recycle_pkt;
    if (pkt1) {
        q->recycle_pkt = pkt1->next;
        q->recycle_count++;
    } else {
        q->alloc_count++;
        pkt1 = av_em_alloc(sizeof(MyAVPacketList));
    }
#ifdef FFP_SHOW_PKT_RECYCLE
    int total_count = q->recycle_count + q->alloc_count;
    if (!(total_count % 50)) {
        av_em_log(ffp, AV_LOG_DEBUG, "pkt-recycle \t%d + \t%d = \t%d\n", q->recycle_count, q->alloc_count, total_count);
    }
#endif
#endif
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}

static int packet_queue_get_count(PacketQueue *q)
{
    int ret = 0;
    SDL_LockMutex(q->mutex);
    ret = q->nb_packets;
    SDL_UnlockMutex(q->mutex);
    return ret;
}

static int packet_queue_put(PacketQueue *q, AVEMPacket *pkt)
{
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_em_packet_unref(pkt);

    return ret;
}

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVEMPacket pkt1, *pkt = &pkt1;
    av_em_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_em_packet_unref(&pkt->pkt);
#ifdef FFP_MERGE
        av_em_freep(&pkt);
#else
        pkt->next = q->recycle_pkt;
        q->recycle_pkt = pkt;
#endif
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);

    SDL_LockMutex(q->mutex);
    while(q->recycle_pkt) {
        MyAVPacketList *pkt = q->recycle_pkt;
        if (pkt)
            q->recycle_pkt = pkt->next;
        av_em_freep(&pkt);
    }
    SDL_UnlockMutex(q->mutex);

    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVEMPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
#ifdef FFP_MERGE
            av_em_free(pkt1);
#else
            pkt1->next = q->recycle_pkt;
            q->recycle_pkt = pkt1;
#endif
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    //printf("q->nbpackets:%d\n", q->nb_packets);
    return ret;
}

static int packet_queue_get_or_buffering(FFPlayer *ffp, PacketQueue *q, AVEMPacket *pkt, int *serial, int *finished)
{
    assert(finished);
    if (!ffp->packet_buffering)
        return packet_queue_get(q, pkt, 1, serial);

    while (1) {
        int new_packet = packet_queue_get(q, pkt, 0, serial);
        if (new_packet < 0)
            return -1;
        else if (new_packet == 0) {
            if (q->is_buffer_indicator && !*finished) {
                ffp_toggle_buffering(ffp, 1);
            }
            new_packet = packet_queue_get(q, pkt, 1, serial);
            if (new_packet < 0)
                return -1;
        }

        if (*finished == *serial) {
            av_em_packet_unref(pkt);
            continue;
        }
        else
            break;
    }

    return 1;
}

void ffsonic_free_p(emsonicStream handle)
{
    if (handle) {
        emsonicDestroyStream(handle);
    }
}

static void decoder_init(Decoder *d, AVEMCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
    memset(d, 0, sizeof(Decoder));
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;

    d->first_frame_decoded_time = SDL_GetTickHR();
    d->first_frame_decoded = 0;
    d->clear_picture_flushed = 0;
    SDL_ProfilerReset(&d->decode_profiler, -1);
}

int64_t check_tx_stream_unix_time(uint8_t *buffer) {
    int64_t ret = -1;
    if (buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x09) {
        if (buffer[4] == 0x06) {
            ret = 0;
            ret |= ((int64_t)buffer[5] << 56);
            ret |= ((int64_t)buffer[6] << 48);
            ret |= ((int64_t)buffer[7] << 40);
            ret |= ((int64_t)buffer[8] << 32);
            ret |= ((int64_t)buffer[9] << 24);
            ret |= ((int64_t)buffer[10] << 16);
            ret |= ((int64_t)buffer[11] << 8);
            ret |= buffer[12];
        }
    }
    return ret;
}

static int decoder_decode_frame(FFPlayer *ffp, Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int got_frame = 0;
    do {
        int ret = -1;

        if (d->queue->abort_request)
            return -1;

        if (!d->packet_pending || d->queue->serial != d->pkt_serial) {
            AVEMPacket pkt;
            do {
                if (d->queue->nb_packets == 0)
                    SDL_CondSignal(d->empty_queue_cond);
                if (packet_queue_get_or_buffering(ffp, d->queue, &pkt, &d->pkt_serial, &d->finished) < 0) {
                    av_em_log(NULL, AV_LOG_ERROR, "======get video packet return -1=====\n");
                    return -1;
                }
                if (pkt.data == flush_pkt.data) {
                    av_em_log(NULL, AV_LOG_INFO, "avcodec flush data, decoder:%p, ctx:%p.codec id:%d, codec:%p \n", d, d->avctx, d->avctx->codec_id, d->avctx->codec);
                    avcodec_em_flush_buffers(d->avctx);
                    av_em_log(NULL, AV_LOG_INFO, "avcodec flush data success.\n");
                    d->finished = 0;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                    if (d->avctx->codec_type == AVMEDIA_TYPE_VIDEO && d->clear_picture_flushed) {
                        ffp_frame_queue_empty(&ffp->is->pictq);
                        d->clear_picture_flushed = 0;
                    }
                }
            } while (pkt.data == flush_pkt.data || d->queue->serial != d->pkt_serial);
            av_em_packet_unref(&d->pkt);
            d->pkt_temp = d->pkt = pkt;
            d->packet_pending = 1;
        }

        switch (d->avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                ret = avcodec_em_decode_video2(d->avctx, frame, &got_frame, &d->pkt_temp);
                if (got_frame) {
                    AVEMRational tb = ffp->is->video_st->time_base;
                    SDL_SpeedSampler3Add(&ffp->stat.video_bitrate_sampler, d->pkt_temp.dts * av_em_q2d(tb) * 1000, d->pkt_temp.size);
                    ffp->stat.vdps = SDL_SpeedSamplerAdd(&ffp->vdps_sampler, FFP_SHOW_VDPS_AVCODEC, "vdps[avcodec]");
                    if (ffp->decoder_reorder_pts == -1) {
                        frame->pts = av_em_frame_get_best_effort_timestamp(frame);
                    } else if (ffp->decoder_reorder_pts) {
                        frame->pts = frame->pkt_pts;
                    } else {
                        frame->pts = frame->pkt_dts;
                    }
                    if (d->pkt_temp.data) {
                        ffp->cur_stream_unix_time = check_tx_stream_unix_time(d->pkt_temp.data);
                    }
                }
                break;
            case AVMEDIA_TYPE_AUDIO:
                ret = avcodec_em_decode_audio4(d->avctx, frame, &got_frame, &d->pkt_temp);
                //printf("decode one audio frame, ret:%d, got_frame:%d.\n", ret, got_frame);
                if (got_frame) {
                    AVEMRational tb = ffp->is->audio_st->time_base;
                    SDL_SpeedSampler3Add(&ffp->stat.audio_bitrate_sampler, d->pkt_temp.pts * av_em_q2d(tb) * 1000, d->pkt_temp.size);
                    tb = (AVEMRational){1, frame->sample_rate};
                    if (frame->pts != AV_NOPTS_VALUE)
                        frame->pts = av_em_rescale_q(frame->pts, d->avctx->time_base, tb);
                    else if (frame->pkt_pts != AV_NOPTS_VALUE)
                        frame->pts = av_em_rescale_q(frame->pkt_pts, av_em_codec_get_pkt_timebase(d->avctx), tb);
                    else if (d->next_pts != AV_NOPTS_VALUE)
                        frame->pts = av_em_rescale_q(d->next_pts, d->next_pts_tb, tb);
                    if (frame->pts != AV_NOPTS_VALUE) {
                        d->next_pts = frame->pts + frame->nb_samples;
                        d->next_pts_tb = tb;
                    }
                }
                break;
            // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
            default:
                break;
        }

        if (ret < 0) {
            d->packet_pending = 0;
        } else {
            d->pkt_temp.dts =
            d->pkt_temp.pts = AV_NOPTS_VALUE;
            if (d->pkt_temp.data) {
                if (d->avctx->codec_type != AVMEDIA_TYPE_AUDIO)
                    ret = d->pkt_temp.size;
                d->pkt_temp.data += ret;
                d->pkt_temp.size -= ret;
                if (d->pkt_temp.size <= 0)
                    d->packet_pending = 0;
            } else {
                if (!got_frame) {
                    d->packet_pending = 0;
                    d->finished = d->pkt_serial;
                }
            }
        }
    } while (!got_frame && !d->finished);
    return got_frame;
}

static void decoder_destroy(Decoder *d) {
    av_em_packet_unref(&d->pkt);
    avcodec_em_free_context(&d->avctx);
}

static void frame_queue_unref_item(Frame *vp)
{
#ifdef FFP_MERGE
    int i;
    for (i = 0; i < vp->sub.num_rects; i++) {
        av_em_freep(&vp->subrects[i]->data[0]);
        av_em_freep(&vp->subrects[i]);
    }
    av_em_freep(&vp->subrects);
#endif
    av_em_frame_unref(vp->frame);
    SDL_VoutUnrefYUVOverlay(vp->bmp);
#ifdef FFP_MERGE
    avsubtitle_free(&vp->sub);
#endif
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex())) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond())) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_em_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_em_frame_free(&vp->frame);
        free_picture(vp);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

static void frame_queue_empty(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    f->rindex = 0;
    f->windex = 0;
    f->size = 0;
    f->rindex_shown = 0;
    for (int i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
    }
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
    
}

static void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

static Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

static Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

static Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

static void frame_queue_next(FrameQueue *f)
{
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
    int ret = 0;
    SDL_LockMutex(f->mutex);
    ret = f->size - f->rindex_shown;
    SDL_UnlockMutex(f->mutex);
    return ret;
}


/* return last shown position */
#ifdef FFP_MERGE
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}
#endif

static void decoder_abort(Decoder *d, FrameQueue *fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    av_em_log(NULL, AV_LOG_INFO, "start wait decoder exit.\n");
    SDL_WaitThread(d->decoder_tid, NULL);
    av_em_log(NULL, AV_LOG_INFO, "wait decoder exit finished.\n");
    d->decoder_tid = NULL;
    packet_queue_flush(d->queue);
}

// FFP_MERGE: fill_rectangle
// FFP_MERGE: fill_border
// FFP_MERGE: ALPHA_BLEND
// FFP_MERGE: RGBA_IN
// FFP_MERGE: YUVA_IN
// FFP_MERGE: YUVA_OUT
// FFP_MERGE: BPP
// FFP_MERGE: blend_subrect

static void free_picture(Frame *vp)
{
    if (vp->bmp) {
        SDL_VoutFreeYUVOverlay(vp->bmp);
        vp->bmp = NULL;
    }
}

static void ff_post_rgba_video_buffer(FFPlayer *ffp, SDL_VoutOverlay *overlay)
{
    if (!overlay)
        return;
    int w = overlay->pitches[0] / 4;
    int h = overlay->h;
    switch (overlay->format) {
        case SDL_FCC_RV32:
            break;
        default:
            ALOGE("[rgbx8888] unexpected format %x\n", overlay->format);
            return;
    }
    ffp->video_present_callback(ffp->inject_opaque, overlay->pixels[0], w * h * 4, w, h, 0);
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
    double time = av_em_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

static double get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_em_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

static void set_clock_speed(Clock *c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    set_clock(&is->vidclk, pts, serial);
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}

// FFP_MERGE: calculate_display_rect
// FFP_MERGE: video_image_display
static void video_image_display2(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    Frame *vp;
    vp = frame_queue_peek_last(&is->pictq);

    if (is->latest_seek_load_serial == vp->serial)
        ffp->stat.latest_seek_load_duration = (av_em_gettime() - is->latest_seek_load_start_at) / 1000;

    if (vp->bmp) {
        if (vp->unix_time > ffp->last_send_unix_time) {
            ffp_notify_msg3(ffp, FFP_MSG_STREAM_UNIX_TIME, (int)(vp->unix_time >> 32), (int)vp->unix_time & 0xFFFFFFFF);
            ffp->last_send_unix_time = vp->unix_time;
        }
        
        bool renderFrameSuccess = false;
        if (ffp->video_present_callback) {
            if (!ffp->videotoolbox) {
                ff_post_rgba_video_buffer(ffp, vp->bmp);
            }
            else {
                SDL_VoutPresentYUVOverlayBuffer(ffp->vout, vp->bmp, ffp);
            }
            renderFrameSuccess = true;
        } else {
            if (SDL_VoutDisplayYUVOverlay(ffp->vout, vp->bmp) >= 0) {
                renderFrameSuccess = true;
            }
        }
        
        if(renderFrameSuccess){
            if (!ffp->first_video_frame_rendered) {
                ffp->first_video_frame_rendered = 1;
                if(!ffp->start_on_prepared && ffp->view_video_first_frame){
                    toggle_pause(ffp, 1);
                    ffp_set_mute_audio(ffp, is->muted);
                }
                ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
                av_em_log(NULL, AV_LOG_INFO, "render first video frame!!\n");
            }
        }
        
        SDL_LockMutex(is->pictq.mutex);
        if (is && !isnan(vp->pts) && vp->pts > 0) {
            ffp->video_clock_error = 0;
            update_video_pts(is, vp->pts, vp->pos, vp->serial);
            //av_em_log(NULL, AV_LOG_WARNING, "video clock:%f, start_time:%lld.\n", get_clock(&is->vidclk) * 1000, is->ic_start_time);
            if (is->audio_stream < 0 || is->auddec.finished) {
                ffp_notify_msg3(ffp, FFP_MSG_PROGRESS, get_clock(&is->vidclk) * 1000 - is->ic_start_time,
                                (int) ffp_get_duration_l(ffp));
            }
        } else {
            ffp->video_clock_error = 1;
        }
        SDL_UnlockMutex(is->pictq.mutex);
        ffp->stat.vfps = SDL_SpeedSamplerAdd(&ffp->vfps_sampler, FFP_SHOW_VFPS_FFPLAY, "vfps[ffplay]");
    }
}

// FFP_MERGE: compute_mod
// FFP_MERGE: video_audio_display

static void stream_component_close(FFPlayer *ffp, int stream_index)
{
    VideoState *is = ffp->is;
    AVEMFormatContext *ic = is->ic;
    AVEMCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_AUDIO step 1.\n");
        decoder_abort(&is->auddec, &is->sampq);

        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_AUDIO step 2.\n");
        SDL_AoutCloseAudio(ffp->aout);

        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_AUDIO step 3.\n");
        decoder_destroy(&is->auddec);

        em_swr_free(&is->swr_ctx);
        av_em_freep(&is->audio_buf1);
        av_em_freep(&is->audio_new_buffer);
        is->audio_buf1_size = 0;
        is->audio_new_buffer_size = 0;
        is->audio_buf = NULL;

        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_AUDIO destroy\n");

#ifdef FFP_MERGE
        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_em_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
#endif
        break;
    case AVMEDIA_TYPE_VIDEO:
        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_VIDEO step 1.\n");
        decoder_abort(&is->viddec, &is->pictq);
        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_VIDEO step 2.\n");
        decoder_destroy(&is->viddec);

        av_em_log(NULL, AV_LOG_ERROR, "AVMEDIA_TYPE_VIDEO destroy.\n");
        break;
        // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
        // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    default:
        break;
    }
}

static void stream_close(FFPlayer *ffp)
{
    av_em_log(NULL, AV_LOG_INFO, "enter func:%s\n", __func__);
    VideoState *is = ffp->is;
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    packet_queue_abort(&is->videoq);
    packet_queue_abort(&is->audioq);
    SDL_WaitThread(is->read_tid, NULL);
    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(ffp, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(ffp, is->video_stream);
#ifdef FFP_MERGE
    if (is->subtitle_stream >= 0)
        stream_component_close(ffp, is->subtitle_stream);
#endif
    if (!is->prepared_source && is->ic) {
        avformat_em_close_input(&is->ic);
        is->ic = NULL;
    }
    av_em_log(NULL, AV_LOG_DEBUG, "wait for video_refresh_tid\n");
    SDL_WaitThread(is->video_refresh_tid, NULL);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
#ifdef FFP_MERGE
    packet_queue_destroy(&is->subtitleq);
#endif

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
#ifdef FFP_MERGE
    frame_queue_destory(&is->subpq);
#endif
    SDL_DestroyCond(is->continue_read_thread);
    SDL_DestroyMutex(is->play_mutex);
#if !CONFIG_AVFILTER
    em_sws_freeContext(is->img_convert_ctx);
#endif
#ifdef FFP_MERGE
    em_sws_freeContext(is->sub_convert_ctx);
#endif
    av_em_free(is->filename);
    av_em_free(is);
}

// FFP_MERGE: do_exit
// FFP_MERGE: sigterm_handler
// FFP_MERGE: video_open
// FFP_MERGE: video_display

/* display the current picture, if any */
static void video_display2(FFPlayer *ffp)
{

    VideoState *is = ffp->is;
    if (is->video_st)
        video_image_display2(ffp);
}

static int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = get_clock(&is->vidclk);
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = get_clock(&is->audclk);
            break;
        default:
            val = get_clock(&is->extclk);
            break;
    }
    return val;
}

static void check_external_clock_speed(VideoState *is) {
   if ((is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
       (is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)) {
       set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
   } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
              (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
       set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
   } else {
       double speed = is->extclk.speed;
       if (speed != 1.0)
           set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
   }
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        is->is_seeking = 0;
        SDL_CondSignal(is->continue_read_thread);
    }
}

/* pause or resume the video */
static void stream_toggle_pause_l(FFPlayer *ffp, int pause_on)
{
    VideoState *is = ffp->is;
    if (is->paused && !pause_on) {
        is->frame_timer += av_em_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
#ifdef FFP_MERGE
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
#endif
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    } else {
    }
    if (is->pause_buffering && !pause_on) {
        is->pause_buffering = 0;
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = pause_on;

    SDL_AoutPauseAudio(ffp->aout, pause_on);
}

static void stream_update_pause_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    if (!is->step && (is->pause_req || is->buffering_on)) {
        stream_toggle_pause_l(ffp, 1);
    } else {
        stream_toggle_pause_l(ffp, 0);
    }
}

static void toggle_pause_l(FFPlayer *ffp, int pause_on)
{
    VideoState *is = ffp->is;
    is->pause_req = pause_on;
    ffp->auto_resume = !pause_on;
    stream_update_pause_l(ffp);
    is->step = 0;
}

static void toggle_pause(FFPlayer *ffp, int pause_on)
{
    SDL_LockMutex(ffp->is->play_mutex);
    toggle_pause_l(ffp, pause_on);
    SDL_UnlockMutex(ffp->is->play_mutex);
}

static void toggle_standby(FFPlayer *ffp, int pause_on)
{
    SDL_LockMutex(ffp->is->play_mutex);
    toggle_pause_l(ffp, pause_on);
    ffp->is->pause_buffering = pause_on;
    SDL_UnlockMutex(ffp->is->play_mutex);
}

// FFP_MERGE: toggle_mute
// FFP_MERGE: update_volume

static void step_to_next_frame_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause_l(ffp, 0);
    is->step = 1;
}

static double compute_target_delay(FFPlayer *ffp, double delay, VideoState *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = get_clock(&is->vidclk) - get_master_clock(is);
        //av_em_log(NULL, AV_LOG_INFO, "clock diff:%f, vid clock:%f, aud clock:%f.\n", diff, is->vidclk.pts, is->audclk.pts);
        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        /* -- by bbcallen: replace is->max_frame_duration with AV_NOSYNC_THRESHOLD */
        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    if (ffp) {
        ffp->stat.avdelay = delay;
        ffp->stat.avdiff  = diff;
    }
#ifdef FFP_SHOW_AUDIO_DELAY
    av_em_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);
#endif

    return delay;
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        //av_em_log(NULL, AV_LOG_INFO, "frame pts:%f.\n", vp->pts * 1000);
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

/* called to display each frame */
static void video_refresh(FFPlayer *opaque, double *remaining_time)
{
    FFPlayer *ffp = opaque;
    VideoState *is = ffp->is;
    double time;

#ifdef FFP_MERGE
    Frame *sp, *sp2;
#endif

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (!ffp->display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_em_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + ffp->rdftspeed < time) {
            video_display2(ffp);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + ffp->rdftspeed - time);
    }

    if (is->video_st) {
retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
            // SDL_VoutClear(ffp->vout);
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&is->pictq);
            vp = frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }
            if (lastvp->serial != vp->serial)
                is->frame_timer = av_em_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);
            delay = compute_target_delay(ffp, last_duration, is);

            time= av_em_gettime_relative()/1000000.0;
            if (isnan(is->frame_timer) || time < is->frame_timer)
                is->frame_timer = time;
            
           // av_em_log(NULL, AV_LOG_INFO, "time:%f, frame timer:%f, delay:%f.,last duration:%f\n", time, is->frame_timer, delay, last_duration);
            if (time < is->frame_timer + delay && delay < AV_SYNC_ONCE_WAIT_MAX_DELAY) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            } else {
                *remaining_time  = 0;
            }
            
            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                if(!is->step && (ffp->framedrop > 0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration) {
                    frame_queue_next(&is->pictq);
                    goto retry;
                }
            }

            // FFP_MERGE: if (is->subtitle_st) { {...}

            frame_queue_next(&is->pictq);
            is->force_refresh = 1;

            SDL_LockMutex(ffp->is->play_mutex);
            if (is->step) {
                is->step = 0;
                if (!is->paused)
                    stream_update_pause_l(ffp);
            }
            SDL_UnlockMutex(ffp->is->play_mutex);
        }
display:
        /* display picture */
        if (!ffp->display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown) {
            video_display2(ffp);
            if (!ffp->b_first_display_time) {
                av_em_log(NULL, AV_LOG_INFO, "first refresh frame takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
                ffp->b_first_display_time = 1;
            }
        }
    }
    is->force_refresh = 0;
    if (ffp->show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize __unused;
        double av_diff;

        cur_time = av_em_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
#ifdef FFP_MERGE
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
#else
            sqsize = 0;
#endif
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);
            av_em_log(NULL, AV_LOG_INFO,
                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                   get_master_clock(is),
                   (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
                   av_diff,
                   is->frame_drops_early + is->frame_drops_late,
                   aqsize / 1024,
                   vqsize / 1024,
                   sqsize,
                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);
            fflush(stdout);
            last_time = cur_time;
        }
    }
}

/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
static void alloc_picture(FFPlayer *ffp, int frame_format)
{
    VideoState *is = ffp->is;
    Frame *vp;
#ifdef FFP_MERGE
    int64_t bufferdiff;
#endif

    vp = &is->pictq.queue[is->pictq.windex];

    free_picture(vp);

#ifdef FFP_MERGE
    video_open(ffp, 0, vp);
#endif

    SDL_VoutSetOverlayFormat(ffp->vout, ffp->overlay_format);
    vp->bmp = SDL_Vout_CreateOverlay(vp->width, vp->height,
                                   frame_format,
                                   ffp->vout);
#ifdef FFP_MERGE
    bufferdiff = vp->bmp ? FFMAX(vp->bmp->pixels[0], vp->bmp->pixels[1]) - FFMIN(vp->bmp->pixels[0], vp->bmp->pixels[1]) : 0;
    if (!vp->bmp || vp->bmp->pitches[0] < vp->width || bufferdiff < (int64_t)vp->height * vp->bmp->pitches[0]) {
#else
    /* RV16, RV32 contains only one plane */
    if (!vp->bmp || (!vp->bmp->is_private && vp->bmp->pitches[0] < vp->width)) {
#endif
        /* SDL allocates a buffer smaller than requested if the video
         * overlay hardware is unable to support the requested size. */
        av_em_log(NULL, AV_LOG_FATAL,
               "Error: the video system does not support an image\n"
                        "size of %dx%d pixels. Try using -lowres or -vf \"scale=w:h\"\n"
                        "to reduce the image size.\n", vp->width, vp->height );
        free_picture(vp);
    }

    SDL_LockMutex(is->pictq.mutex);
    vp->allocated = 1;
    SDL_CondSignal(is->pictq.cond);
    SDL_UnlockMutex(is->pictq.mutex);
}

#ifdef FFP_MERGE
static void duplicate_right_border_pixels(SDL_Overlay *bmp) {
    int i, width, height;
    Uint8 *p, *maxp;
    for (i = 0; i < 3; i++) {
        width  = bmp->w;
        height = bmp->h;
        if (i > 0) {
            width  >>= 1;
            height >>= 1;
        }
        if (bmp->pitches[i] > width) {
            maxp = bmp->pixels[i] + bmp->pitches[i] * height - 1;
            for (p = bmp->pixels[i] + width - 1; p < maxp; p += bmp->pitches[i])
                *(p+1) = *p;
        }
    }
}
#endif

static int queue_picture(FFPlayer *ffp, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    VideoState *is = ffp->is;
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif
    if (is->is_seeking && fftime_to_milliseconds(is->seek_pos) / (float)1000 > pts) {
        //av_em_log(NULL, AV_LOG_INFO, "video pts is lower than seek target, target:%f, pts:%f.\n", fftime_to_milliseconds(is->seek_pos) / (float)1000, pts);
        //fix seek maybe failed by ccl 2019-04-10
        //return 0;
    } else if (is->is_seeking) {
        is->is_seeking = 0;
        ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, (int)fftime_to_milliseconds(is->seek_pos), 0);
    }
    
    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;
    
    /* alloc or resize hardware picture buffer */
    if (!vp->bmp || vp->reallocate || !vp->allocated ||
        vp->width  != src_frame->width ||
        vp->height != src_frame->height) {
        if (vp->width != src_frame->width || vp->height != src_frame->height)
            ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, src_frame->width, src_frame->height);

        vp->allocated  = 0;
        vp->reallocate = 0;
        vp->width = src_frame->width;
        vp->height = src_frame->height;

        /* the allocation must be done in the main thread to avoid
           locking problems. */
        alloc_picture(ffp, src_frame->format);

        if (is->videoq.abort_request)
            return -1;
    }

    /* if the frame is not skipped, then display it */
    if (vp->bmp) {
        /* get a pointer on the bitmap */
        SDL_VoutLockYUVOverlay(vp->bmp);

#ifdef FFP_MERGE
#if CONFIG_AVFILTER
        // FIXME use direct rendering
        av_image_copy(data, linesize, (const uint8_t **)src_frame->data, src_frame->linesize,
                        src_frame->format, vp->width, vp->height);
#else
        // sws_getCachedContext(...);
#endif
#endif
        // FIXME: set swscale options
        if (src_frame->format == AV_PIX_FMT_YUVJ420P) {
            src_frame->format = AV_PIX_FMT_YUV420P;
        }
        if (SDL_VoutFillFrameYUVOverlay(vp->bmp, src_frame) < 0) {
            av_em_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            exit(1);
        }
        /* update the bitmap content */
        SDL_VoutUnlockYUVOverlay(vp->bmp);

        vp->pts = pts;
        vp->duration = duration;
        vp->pos = pos;
        vp->serial = serial;
        vp->sar = src_frame->sample_aspect_ratio;

        vp->bmp->sar_num = vp->sar.num;
        vp->bmp->sar_den = vp->sar.den;
        vp->unix_time = ffp->cur_stream_unix_time;
        /* now we can update the picture count */

        if (!is->is_seeking){
            frame_queue_push(&is->pictq);
        }

        if (!is->viddec.first_frame_decoded) {
            ALOGD("Video: first frafme decoded\n");
            is->viddec.first_frame_decoded_time = SDL_GetTickHR();
            is->viddec.first_frame_decoded = 1;
            ffp_notify_msg1(ffp, FFP_MSG_VIDEO_DECODE_FIRST_I_FRAME);
        }
    }
    return 0;
}

static int get_video_frame(FFPlayer *ffp, AVFrame *frame)
{
    VideoState *is = ffp->is;
    int got_picture;
    static int first = 0;
    ffp_video_statistic_l(ffp);
    if ((got_picture = decoder_decode_frame(ffp, &is->viddec, frame, NULL)) < 0) {
        return -1;
    }
    SDL_LockMutex(ffp->reconfigure_mutex);
    if (got_picture) {
        if (!first) {
            av_em_log(NULL, AV_LOG_INFO, "get first video frame takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
            first = 1;
        }
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_em_q2d(is->video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_em_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

#ifdef FFP_MERGE
        is->viddec_width  = frame->width;
        is->viddec_height = frame->height;
#endif

        if (ffp->framedrop>0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    is->continuous_frame_drops_early++;
                    if (is->continuous_frame_drops_early > ffp->framedrop) {
                        is->continuous_frame_drops_early = 0;
                    } else {
                        av_em_frame_unref(frame);
                        got_picture = 0;
                    }
                }
            }
        }
    }
    SDL_UnlockMutex(ffp->reconfigure_mutex);
    return got_picture;
}

#if CONFIG_AVFILTER
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_em_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_em_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(FFPlayer *ffp, AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVEMCodecParameters *codecpar = is->video_st->codecpar;
    AVEMRational fr = av_em_guess_frame_rate(is->ic, is->video_st, NULL);
    AVEMDictionaryEntry *e = NULL;

    while ((e = av_em_dict_get(ffp->sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    graph->scale_sws_opts = av_em_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    /* SDL YUV code is not handling odd width/height for some driver
     * combinations, therefore we crop the picture to an even width/height. */
    INSERT_FILT("crop", "floor(in_w/2)*2:floor(in_h/2)*2");

    if (ffp->autorotate) {
        double theta  = get_rotation(is->video_st);

        if (fabs(theta - 90) < 1.0) {
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }

#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        char setpts_buf[256];
        float rate = 1.0f / ffp->pf_playback_rate;
        rate = av_clipf_c(rate, 0.5f, 2.0f);
        av_em_log(ffp, AV_LOG_INFO, "vf_rate=%f(1/%f)\n", ffp->pf_playback_rate, rate);
        snprintf(setpts_buf, sizeof(setpts_buf), "%f*PTS", rate);
        INSERT_FILT("setpts", setpts_buf);
    }
#endif

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

fail:
    return ret;
}

static int configure_audio_filters(FFPlayer *ffp, const char *afilters, int force_output_format)
{
    VideoState *is = ffp->is;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVEMDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;
    char afilters_args[4096];

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    while ((e = av_em_dict_get(ffp->swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_em_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64,  is->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels       [0] = is->audio_tgt.channels;
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }

    afilters_args[0] = 0;
    if (afilters)
        snprintf(afilters_args, sizeof(afilters_args), "%s", afilters);

#ifdef FFP_AVFILTER_PLAYBACK_RATE
    if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        if (afilters_args[0])
            av_strlcatf(afilters_args, sizeof(afilters_args), ",");

        av_em_log(ffp, AV_LOG_INFO, "af_rate=%f\n", ffp->pf_playback_rate);
        av_strlcatf(afilters_args, sizeof(afilters_args), "atempo=%f", ffp->pf_playback_rate);
    }
#endif

    if ((ret = configure_filtergraph(is->agraph, afilters_args[0] ? afilters_args : NULL, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    return ret;
}
#endif  /* CONFIG_AVFILTER */

static int audio_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    AVFrame *frame = av_em_frame_alloc();
    Frame *af;
    static int first = 0;
#if CONFIG_AVFILTER
    int last_serial = -1;
    int64_t dec_channel_layout;
    int reconfigure;
#endif
    int got_frame = 0;
    AVEMRational tb;
    int ret = 0;

    if (!frame) {
        return AVERROR(ENOMEM);
    }
    av_em_log(NULL, AV_LOG_INFO, "start decode audio frame, takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
    do {
        ffp_audio_statistic_l(ffp);
        if ((got_frame = decoder_decode_frame(ffp, &is->auddec, frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
                if (!first) {
                    av_em_log(NULL, AV_LOG_INFO, "get first audio samples takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
                    first = 1;
                }
                tb = (AVEMRational){1, frame->sample_rate};

#if CONFIG_AVFILTER
                dec_channel_layout = get_valid_channel_layout(frame->channel_layout, av_em_frame_get_channels(frame));

                reconfigure =
                    cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                                   frame->format, av_em_frame_get_channels(frame))    ||
                    is->audio_filter_src.channel_layout != dec_channel_layout ||
                    is->audio_filter_src.freq           != frame->sample_rate ||
                    is->auddec.pkt_serial               != last_serial        ||
                    ffp->af_changed;

                if (reconfigure) {
                    SDL_LockMutex(ffp->af_mutex);
                    ffp->af_changed = 0;
                    char buf1[1024], buf2[1024];
                    av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                    av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                    av_em_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           is->audio_filter_src.freq, is->audio_filter_src.channels, av_em_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, av_em_frame_get_channels(frame), av_em_get_sample_fmt_name(frame->format), buf2, is->auddec.pkt_serial);

                    is->audio_filter_src.fmt            = frame->format;
                    is->audio_filter_src.channels       = av_em_frame_get_channels(frame);
                    is->audio_filter_src.channel_layout = dec_channel_layout;
                    is->audio_filter_src.freq           = frame->sample_rate;
                    last_serial                         = is->auddec.pkt_serial;

                    if ((ret = configure_audio_filters(ffp, ffp->afilters, 1)) < 0) {
                        SDL_UnlockMutex(ffp->af_mutex);
                        goto the_end;
                    }
                    SDL_UnlockMutex(ffp->af_mutex);
                }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = is->out_audio_filter->inputs[0]->time_base;
#endif
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_em_q2d(tb);
                af->pos = av_em_frame_get_pkt_pos(frame);
                af->serial = is->auddec.pkt_serial;
                af->duration = av_em_q2d((AVEMRational){frame->nb_samples, frame->sample_rate});
                av_em_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif
    av_em_frame_free(&frame);
    return ret;
}

static int decoder_start(Decoder *d, int (*fn)(void *), void *arg, const char *name)
{
    packet_queue_start(d->queue);
    d->decoder_tid = SDL_CreateThreadEx(&d->_decoder_tid, fn, arg, name);
    if (!d->decoder_tid) {
        av_em_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int ffplay_video_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    AVFrame *frame = av_em_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVEMRational tb = is->video_st->time_base;
    AVEMRational frame_rate = av_em_guess_frame_rate(is->ic, is->video_st, NULL);
    av_em_log(NULL, AV_LOG_INFO, "enter thread:%s\n", __func__);
    if (!frame) {
        return AVERROR(ENOMEM);
    }
    av_em_log(NULL, AV_LOG_INFO, "start decode video frames, takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
    for (;;) {
        ret = get_video_frame(ffp, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
        tb = is->video_st->time_base;
        frame_rate = av_em_guess_frame_rate(is->ic, is->video_st, NULL);
        duration = (frame_rate.num && frame_rate.den ? av_em_q2d((AVEMRational){frame_rate.den, frame_rate.num}) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_em_q2d(tb);
        ret = queue_picture(ffp, frame, pts, duration, av_em_frame_get_pkt_pos(frame), is->viddec.pkt_serial);
        av_em_frame_unref(frame);
        if (ret < 0)
            goto the_end;
    }
 the_end:
    av_em_frame_free(&frame);
    av_em_log(NULL, AV_LOG_INFO, "exit thread:%s\n", __func__);
    return 0;
}

static int video_thread(void *arg)
{
    FFPlayer *ffp = (FFPlayer *)arg;
    int       ret = 0;

    if (ffp->node_vdec) {
        ret = ffpipenode_run_sync(ffp->node_vdec);
    }
    return ret;
}

// FFP_MERGE: subtitle_thread

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
static int synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_em_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}
    
    
/***
 resample pcm buffer to needed length.
***/
static int audio_resample_frame(uint8_t *in, uint8_t *out, int length, int need_length, int channels, int sample_bytes)
{
    memcpy(out, in, need_length);
    return 0;
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
static int audio_decode_frame(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused || is->step)
        return -1;

    if (ffp->sync_av_start &&                       /* sync enabled */
        is->video_st &&                             /* has video stream */
        !is->viddec.first_frame_decoded &&          /* not hot */
        is->viddec.finished != is->videoq.serial) { /* not finished */
        /* waiting for first video frame */
        Uint64 now = SDL_GetTickHR();
        if (now < is->viddec.first_frame_decoded_time ||
            now > is->viddec.first_frame_decoded_time + 2000) {
            is->viddec.first_frame_decoded = 1;
            ffp_notify_msg1(ffp, FFP_MSG_VIDEO_DECODE_FIRST_I_FRAME);
        } else {
            /* video pipeline is not ready yet */
            return -1;
        }
    }

    do {
#if defined(_WIN32) || defined(__APPLE__)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_em_gettime_relative() - ffp->audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
        
            av_em_usleep (1000);
        }
#endif
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);

    data_size = av_em_samples_get_buffer_size(NULL, av_em_frame_get_channels(af->frame),
                                           af->frame->nb_samples,
                                           af->frame->format, 1);

    dec_channel_layout =
        (af->frame->channel_layout && av_em_frame_get_channels(af->frame) == av_em_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_em_get_default_channel_layout(av_em_frame_get_channels(af->frame));
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        /*ffp->pf_playback_rate_changed || by ccl*/
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        AVEMDictionary *swr_opts = NULL;
        em_swr_free(&is->swr_ctx);
        is->swr_ctx = em_swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt,
                                         is->audio_tgt.freq /*/ ffp->pf_playback_rate by ccl*/,
                                         dec_channel_layout,           af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx) {
            av_em_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_em_get_sample_fmt_name(af->frame->format), av_em_frame_get_channels(af->frame),
                    is->audio_tgt.freq, av_em_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            return -1;
        }
        av_em_dict_copy(&swr_opts, ffp->swr_opts, 0);
        if (af->frame->channel_layout == AV_CH_LAYOUT_5POINT1_BACK)
            av_em_opt_set_double(is->swr_ctx, "center_mix_level", ffp->preset_5_1_center_mix_level, 0);
        av_em_opt_set_dict(is->swr_ctx, &swr_opts);
        av_em_dict_free(&swr_opts);

        if (em_swr_init(is->swr_ctx) < 0) {
            av_em_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_em_get_sample_fmt_name(af->frame->format), av_em_frame_get_channels(af->frame),
                    is->audio_tgt.freq, av_em_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            em_swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = av_em_frame_get_channels(af->frame);
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = af->frame->format;
        
        /*
        if (ffp->pf_playback_rate_changed) {
            ffp->pf_playback_rate_changed = 0;
        }
         by ccl */
    }

    if (is->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio_buf1;
        int out_count = (int)((int64_t)wanted_nb_samples * is->audio_tgt.freq /*/ ffp->pf_playback_rate by ccl*/ / af->frame->sample_rate + 256);
        int out_size  = av_em_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "av_em_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (em_swr_set_compensation(is->swr_ctx
                    , (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq /*/ ffp->pf_playback_rate by ccl*/ / af->frame->sample_rate,
                                        wanted_nb_samples * is->audio_tgt.freq /*/ ffp->pf_playback_rate by ccl */ / af->frame->sample_rate) < 0) {
                av_em_log(NULL, AV_LOG_ERROR, "em_swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_em_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = em_swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "em_swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_em_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (em_swr_init(is->swr_ctx) < 0)
                em_swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        int bytes_per_sample = av_em_get_bytes_per_sample(is->audio_tgt.fmt);
        resampled_data_size = len2 * is->audio_tgt.channels * bytes_per_sample;
        
#if defined(__ANDROID__)
        if(ffp->pf_playback_rate != 1.0f && ffp->enable_sonic_handle){
            if (ffp->sonic_handle == NULL) {
                ffp->sonic_handle = emsonicCreateStream(is->audio_tgt.freq, is->audio_tgt.channels);
                av_em_log(NULL, AV_LOG_ERROR, "create sonic stream simplerate is %d, channels is %d, simplebit is %d\n"
                          , is->audio_tgt.freq
                          , is->audio_tgt.channels
                          , is->audio_tgt.fmt);
            }
            
            av_em_fast_malloc(&is->audio_new_buffer, &is->audio_new_buffer_size, out_size * sizeof(short));
            if (!is->audio_new_buffer){
                return AVERROR(ENOMEM);
            }
            
            for (int i = 0; i < (resampled_data_size / 2); i++){
                is->audio_new_buffer[i] = (is->audio_buf1[i * 2] | (is->audio_buf1[i * 2 + 1] << 8));
            }
            
            emsonicSetSpeed(ffp->sonic_handle, ffp->pf_playback_rate);
            int ret_len = emsonicWriteShortToStream(ffp->sonic_handle
                                                    , is->audio_new_buffer
                                                    , resampled_data_size / (bytes_per_sample * is->audio_tgt.channels));
            int numSamples = (int)(resampled_data_size / ( bytes_per_sample * is->audio_tgt.channels) / ffp->pf_playback_rate);
            if (ret_len){
                ret_len = emsonicReadShortFromStream(ffp->sonic_handle, is->audio_new_buffer, numSamples);
            }

            if (ret_len > 0) {
                is->audio_buf = (uint8_t *)is->audio_new_buffer;
                resampled_data_size = ret_len * bytes_per_sample * is->audio_tgt.channels;
            }
            else{
                return -1;
            }
        }
#endif
        
    } else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }
    /*if (fabsf(ffp->pf_playback_rate) > 0.00001 &&
        fabsf(ffp->pf_playback_rate - 1.0f) > 0.00001) {
        wanted_nb_samples = resampled_data_size / ffp->pf_playback_rate;
        //sample_bytes =   is->audio_tgt.channels * is->audio_tgt.frame_size / 8;
        wanted_nb_samples = wanted_nb_samples / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
        if (wanted_nb_samples > is->audio_buf2_size) {
            if (is->audio_buf2) {
                av_em_free(is->audio_buf2);
                is->audio_buf2 = NULL;
            }
            is->audio_buf2 = (uint8_t *)av_em_alloc(wanted_nb_samples);
            if (!is->audio_buf2) {
                return AVERROR(ENOMEM);
            }
            is->audio_buf2_size = wanted_nb_samples;
            
        }
        audio_resample_frame(is->audio_buf, is->audio_buf2, resampled_data_size, wanted_nb_samples, is->audio_tgt.channels, is->audio_tgt.frame_size);
        is->audio_buf = is->audio_buf2;
        resampled_data_size = wanted_nb_samples;
    } */
    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts) && af->pts > 0)
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else if (!isnan(af->pts) && af->pts <= 0)
        is->audio_clock += (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef FFP_SHOW_AUDIO_DELAY
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    if (!is->auddec.first_frame_decoded) {
        ALOGD("avcodec/Audio: first frame decoded\n");
        is->auddec.first_frame_decoded_time = SDL_GetTickHR();
        is->auddec.first_frame_decoded = 1;
    }
    if (!ffp->first_audio_frame_rendered) {
        ffp->first_audio_frame_rendered = 1;
        ffp_notify_msg1(ffp, FFP_MSG_AUDIO_RENDERING_START);
    }
    return resampled_data_size;
}

/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    FFPlayer *ffp = opaque;
    VideoState *is = ffp->is;
    int audio_size, len1;
    if (!ffp || !is) {
        memset(stream, 0, len);
        return;
    }
    int play_channel_mode = ffp->play_channel_mode;
    ffp->audio_callback_time = av_em_gettime_relative();
    
    if (ffp->pf_playback_rate_changed
#if defined(__ANDROID__)
        && !ffp->enable_sonic_handle
#endif
        ) {
        ffp->pf_playback_rate_changed = 0;
        SDL_AoutSetPlaybackRate(ffp->aout, ffp->pf_playback_rate);
    }
    
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
            audio_size = audio_decode_frame(ffp);
        //av_em_log(NULL, AV_LOG_INFO, "decode audio frame size:%d", audio_size);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        if (is->auddec.pkt_serial != is->audioq.serial) {
            is->audio_buf_index = is->audio_buf_size;
            memset(stream, 0, len);
            // stream += len;
            // len = 0;
            SDL_AoutFlushAudio(ffp->aout);
            break;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        ////////////////////////////////////////////////
        if (len1 > len)
            len1 = len;
        if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME) {
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
            if (ffp->aout->func_present_audio_pcm) {
                ffp->aout->func_present_audio_pcm(ffp->aout, stream, len1, is->audio_tgt.freq, is->audio_tgt.channels, (int)is->audio_clock * 1000);
            }
            if (play_channel_mode != FFP_PLAY_CHANNEL_MODE_STEREO && is->audio_tgt.channels == 2) {
                uint16_t *ptr = (uint16_t *)stream;
                if (play_channel_mode == FFP_PLAY_CHANNEL_MODE_LEFT) {
                    for (int i = 0; i < len1 / 4; i++) {
                        ptr[2 * i + 1] = ptr[2 * i];
                    }
                } else {
                    for (int i = 0; i < len1 / 4; i++) {
                        ptr[2 * i] = ptr[2 * i + 1];
                    }
                }
            }
        }
        //////////////////////////////////////////////TODO:use hook function to change audio speed or repeat.
        else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf)
                SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, is->audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    
    if (!isnan(is->audio_clock)) {
        set_clock_at(&is->audclk, is->audio_clock - (double)(is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec - SDL_AoutGetLatencySeconds(ffp->aout), is->audio_clock_serial, ffp->audio_callback_time / 1000000.0);
        sync_clock_to_slave(&is->extclk, &is->audclk);
        //av_em_log(NULL, AV_LOG_INFO, "set audclk :%f.\n", is->audio_clock - (double)(is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec);
        /*if (is->video_stream < 0 || ffp->video_clock_error)*/ {
            int64_t start_time = 0;
            if (is->ic) start_time = fftime_to_milliseconds(is->ic->start_time);
            if (start_time < 0 /*|| start_time > ffp_get_duration_l(ffp)*/) {
                start_time = 0;
            }
            //av_em_log(NULL, AV_LOG_ERROR, "sound master clock:%fms, start time clock:%lld.\n", get_master_clock(is) * 1000, start_time);
            //add !is->auddec.finished 修复视频中音频比视频数据少，导致播放progress异常问题
            //http://1252516306.vod2.myqcloud.com/e32c0d7evodgzp1252516306/6ca6370a5285890815226132549/f60VHCUE9xcA.mp4
//            if(!is->auddec.finished){
//                ffp_notify_msg3(ffp,FFP_MSG_PROGRESS, (get_master_clock(is) + SDL_AoutGetLatencySeconds(ffp->aout)) * 1000 - start_time , (int)ffp_get_duration_l(ffp));
//            }
            if((is->video_st != NULL && !is->auddec.finished) || is->video_st == NULL){
                ffp_notify_msg3(ffp,FFP_MSG_PROGRESS, (get_master_clock(is) + SDL_AoutGetLatencySeconds(ffp->aout)) * 1000 - start_time , (int)ffp_get_duration_l(ffp));
            }
        }
    }
}

static int audio_open(FFPlayer *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    FFPlayer *ffp = opaque;
    VideoState *is = ffp->is;
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
#ifdef FFP_MERGE
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
#endif
    static const int next_sample_rates[] = {0, 44100, 48000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_em_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_em_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_em_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_em_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_em_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AoutGetAudioPerSecondCallBacks(ffp->aout)));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    wanted_spec.streamtype = ffp->audio_stream_type;
    while (SDL_AoutOpenAudio(ffp->aout, &wanted_spec, &spec) < 0) {
        /* avoid infinity loop on exit. --by bbcallen */
        if (is->abort_request)
            return -1;
        av_em_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_em_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_em_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_em_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_em_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_em_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_em_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_em_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_em_log(NULL, AV_LOG_ERROR, "av_em_samples_get_buffer_size failed\n");
        return -1;
    }

    SDL_AoutSetDefaultLatencySeconds(ffp->aout, ((double)(2 * spec.size)) / audio_hw_params->bytes_per_sec);
    return spec.size;
}

int create_avformat_internal(FFPlayer *ffp, VideoState *is, char *filename, int play_type, AVEMInputFormat *iformat, AVEMFormatContext **ic_out);


static int prepare_source_internal(FFPlayer *ffp, VideoState *is, char *filename, int play_type, AVEMInputFormat *iformat, ffplay_format_t **ffp_format_out)
{
    AVEMFormatContext *ic;
    AVEMDictionary **opts;
    int64_t start_ms = ijk_get_timems();
    int ret = create_avformat_internal(ffp, is, filename,play_type, iformat, &ic);
    if (ret < 0) {
        av_em_log(NULL, AV_LOG_INFO, "create avformat internal failed:%d.\n", ret);
        return ret;
    }
    opts = setup_find_stream_info_opts(ic, ffp->codec_opts);
    int orig_nb_streams = ic->nb_streams;
    int i = 0;
    int err = avformat_em_find_stream_info(ic, opts);
    av_em_log(NULL, AV_LOG_INFO, "success find stream info takes time:%lld.\n", ijk_get_timems() - start_ms);
    ffplay_format_t *ffp_format = (ffplay_format_t *) av_em_mallocz(sizeof(ffplay_format_t));
    if (!ffp_format) {
        av_em_log(NULL, AV_LOG_ERROR, "malloc ffplay format failed.\n");
        goto fail;
    }
    memset(ffp_format->stream_index, -1, sizeof(int) * AVMEDIA_TYPE_NB);
    strcpy(ffp_format->filename, filename);
    ffp_format->ic = ic;
    for (i = 0; i < orig_nb_streams; i++)
        av_em_dict_free(&opts[i]);
    av_em_freep(&opts);
    if (err < 0) {
        av_em_log(NULL, AV_LOG_WARNING,
               "%s: could not find codec parameters\n", filename);
        ret = -1;
        goto fail;
    }
    if (ic->pb)
        ic->pb->eof_reached = 0;
    av_em_dump_format(ic, 0, filename, 0);
    int video_stream_count = 0;
    int h264_stream_count = 0;
    int first_h264_stream = -1;
    for (i = 0; i < ic->nb_streams; i++) {
        AVEMStream *st = ic->streams[i];
        enum AVEMMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type == AVMEDIA_TYPE_VIDEO) {
            enum AVEMCodecID codec_id = st->codecpar->codec_id;
            video_stream_count++;
            if (codec_id == AV_CODEC_ID_H264) {
                h264_stream_count++;
                if (first_h264_stream < 0)
                    first_h264_stream = i;
            }
        }
    }
    if (video_stream_count > 1 && ffp_format->stream_index[AVMEDIA_TYPE_VIDEO] < 0) {
        ffp_format->stream_index[AVMEDIA_TYPE_VIDEO] = first_h264_stream;
        av_em_log(NULL, AV_LOG_WARNING, "multiple video stream found, prefer first h264 stream: %d\n", first_h264_stream);
    }
    if (!ffp->video_disable)
        ffp_format->stream_index[AVMEDIA_TYPE_VIDEO] =
                av_em_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                    ffp_format->stream_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!ffp->audio_disable)
        ffp_format->stream_index[AVMEDIA_TYPE_AUDIO] =
                av_em_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                    ffp_format->stream_index[AVMEDIA_TYPE_AUDIO],
                                    ffp_format->stream_index[AVMEDIA_TYPE_VIDEO],
                                    NULL, 0);
    *ffp_format_out = ffp_format;
    av_em_log(NULL, AV_LOG_INFO, "prepared filename:%s success, audio stream index:%d, video stream index:%d.\n", filename, ffp_format->stream_index[AVMEDIA_TYPE_AUDIO], ffp_format->stream_index[AVMEDIA_TYPE_VIDEO]);
    fail:
    return ret;
}

static int stream_component_reconfigure(FFPlayer *ffp, AVEMFormatContext *ic, int stream_index)
{
    VideoState *is = ffp->is;
    AVEMCodecContext *avctx;
    AVEMCodec *codec = NULL;
    AVEMCodecParameters *codec_par;
    AVEMFormatContext *ffp_ic = is->ic;
    AVEMCodecParameters *ffp_codec_par;
    AVEMDictionary *opts = NULL;
    AVEMDictionaryEntry *t;
    const char *forced_codec_name = NULL;
    int ret = 0;
    int stream_lowres = ffp->lowres;
    int need_reconfigure_codec = 0;
    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    avctx = avcodec_em_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);
    ret = avcodec_em_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    codec_par = ic->streams[stream_index]->codecpar;
    switch (codec_par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (is->audio_stream < 0) {
                need_reconfigure_codec = 1;
                break;
            }
            ffp_codec_par = ffp_ic->streams[is->audio_stream]->codecpar;
            /*if (ffp_codec_par->sample_rate != codec_par->sample_rate \
                    || ffp_codec_par->channels != codec_par->channels \
                    || ffp_codec_par->codec_id != codec_par->codec_id \
                    || ffp_codec_par->channel_layout != codec_par->channel_layout \
                    || ffp_codec_par->extradata_size != codec_par->extradata_size \
                    || memcmp(ffp_codec_par->extradata, codec_par->extradata, ffp_codec_par->extradata_size)) {   //TODO:need compare more audio charactor
                need_reconfigure_codec = 1;
            }*/
            need_reconfigure_codec = 1;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (is->video_stream < 0) {
                need_reconfigure_codec = 1;
                break;
            }
            ffp_codec_par = ffp_ic->streams[is->video_stream]->codecpar;
            av_em_log(NULL, AV_LOG_INFO, "src width:%d, height:%d, codecid:%d.\n", ffp_codec_par->width, ffp_codec_par->height, ffp_codec_par->codec_id);
            av_em_log(NULL, AV_LOG_INFO, "new width:%d, height:%d, codecid:%d.\n", codec_par->width, codec_par->height, codec_par->codec_id);
            if (ffp_codec_par->width != codec_par->width \
                    || ffp_codec_par->height != codec_par->height \
                    || ffp_codec_par->codec_id != codec_par->codec_id \
                    || ffp_codec_par->extradata_size != codec_par->extradata_size \
                    || memcmp(ffp_codec_par->extradata, codec_par->extradata, ffp_codec_par->extradata_size)) { //TODO:need compare more video charactor
                need_reconfigure_codec = 1;
            }
            break;
        default:
            break;
    }
    if (need_reconfigure_codec) {
        av_em_log(NULL, AV_LOG_INFO, "reconfigure codec ,stream index:%d\n", stream_index);
        switch (avctx->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (is->audio_stream >= 0) {
                    av_em_log(NULL, AV_LOG_INFO, "start abort audio decoder thread.\n");
                    decoder_abort(&is->auddec, &is->sampq);
                    av_em_log(NULL, AV_LOG_INFO, "abort audio decoder thread success.\n");
                    SDL_AoutCloseAudio(ffp->aout);
                    decoder_destroy(&is->auddec);
                }
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (is->video_stream >= 0) {
                    av_em_log(NULL, AV_LOG_INFO, "start abort video decoder thread.\n");
                    decoder_abort(&is->viddec, &is->pictq);
                    av_em_log(NULL, AV_LOG_INFO, "abort video decoder thread success.\n");
                    decoder_destroy(&is->viddec);
                    ffpipenode_flush(ffp->node_vdec);
                    ffpipenode_free(ffp->node_vdec);
                }
                break;
            default:
                break;
        }
        av_em_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);
        codec = avcodec_em_find_decoder(avctx->codec_id);
        switch (avctx->codec_type) {
            case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name = ffp->audio_codec_name; break;
                // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
            case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name = ffp->video_codec_name; break;
            default: break;
        }
        if (forced_codec_name)
            codec = avcodec_em_find_decoder_by_name(forced_codec_name);
        if (!codec) {
            if (forced_codec_name) av_em_log(NULL, AV_LOG_WARNING,
                                          "No codec could be found with name '%s'\n", forced_codec_name);
            else                   av_em_log(NULL, AV_LOG_WARNING,
                                          "No codec could be found with id %d\n", avctx->codec_id);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        avctx->codec_id = codec->id;
        if(stream_lowres > av_em_codec_get_max_lowres(codec)){
            av_em_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                   av_em_codec_get_max_lowres(codec));
            stream_lowres = av_em_codec_get_max_lowres(codec);
        }
        av_em_codec_set_lowres(avctx, stream_lowres);

#if FF_API_EMU_EDGE
        if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
        if (ffp->fast)
            avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
        if(codec->capabilities & AV_CODEC_CAP_DR1)
            avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

        opts = filter_codec_opts(ffp->codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
        if (!av_em_dict_get(opts, "threads", NULL, 0))
            av_em_dict_set(&opts, "threads", "auto", 0);
        if (stream_lowres)
            av_em_dict_set_int(&opts, "lowres", stream_lowres, 0);
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
            av_em_dict_set(&opts, "refcounted_frames", "1", 0);
        if ((ret = avcodec_em_open2(avctx, codec, &opts)) < 0) {
            goto fail;
        }
        av_em_log(NULL, AV_LOG_WARNING, "reconfigure codec, avctx：%p, codec id:%d, codec:%p.\n", avctx, avctx->codec_id, avctx->codec);
        if ((t = av_em_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
            av_em_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        }

        is->eof = 0;
        ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
        switch (avctx->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                /* prepare audio output */
                if ((ret = audio_open(ffp, avctx->channel_layout, avctx->channels, avctx->sample_rate, &is->audio_tgt)) < 0)
                    goto fail;
                ffp_set_audio_codec_info(ffp, AVCODEC_MODULE_NAME, avcodec_em_get_name(avctx->codec_id));
                is->audio_hw_buf_size = ret;
                is->audio_src = is->audio_tgt;
                is->audio_buf_size  = 0;
                is->audio_buf_index = 0;

                /* init averaging filter */
                is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
                is->audio_diff_avg_count = 0;
                /* since we do not have a precise anough audio FIFO fullness,
                   we correct audio sync only if larger than this threshold */
                is->audio_diff_threshold = 2.0 * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec;
                decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
                if ((ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !ic->iformat->read_seek) {
                    is->auddec.start_pts = is->audio_st->start_time;
                    is->auddec.start_pts_tb = is->audio_st->time_base;
                }
                if ((ret = decoder_start(&is->auddec, audio_thread, ffp, "ff_audio_dec")) < 0)
                    goto fail;
                av_em_log(NULL, AV_LOG_INFO, "start ff audio dec thread succ");
                SDL_AoutPauseAudio(ffp->aout, 0);
                is->audio_stream = stream_index;
                is->audio_st = ic->streams[stream_index];
                break;
            case AVMEDIA_TYPE_VIDEO:
                is->video_stream = stream_index;
                is->video_st = ic->streams[stream_index];
                decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
                ffp->node_vdec = ffpipeline_open_video_decoder(ffp->pipeline, ffp);
                if (!ffp->node_vdec)
                    goto fail;
                if ((ret = decoder_start(&is->viddec, video_thread, ffp, "ff_video_dec")) < 0)
                    goto fail;
                is->queue_attachments_req = 1;
                if (ffp->max_fps >= 0) {
                    if(is->video_st->avg_frame_rate.den && is->video_st->avg_frame_rate.num) {
                        double fps = av_em_q2d(is->video_st->avg_frame_rate);
                        SDL_ProfilerReset(&is->viddec.decode_profiler, fps + 0.5);
                        if (fps > ffp->max_fps && fps < 130.0) {
                            is->is_video_high_fps = 1;
                            av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (too high)\n", fps);
                        } else {
                            av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (normal)\n", fps);
                        }
                    }
                    if(is->video_st->r_frame_rate.den && is->video_st->r_frame_rate.num) {
                        double tbr = av_em_q2d(is->video_st->r_frame_rate);
                        if (tbr > ffp->max_fps && tbr < 130.0) {
                            is->is_video_high_fps = 1;
                            av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (too high)\n", tbr);
                        } else {
                            av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (normal)\n", tbr);
                        }
                    }
                }
                if (is->is_video_high_fps) {
                    avctx->skip_frame       = FFMAX(avctx->skip_frame, AVDISCARD_NONREF);
                    avctx->skip_loop_filter = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
                    avctx->skip_idct        = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
                }
                break;
                // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
            default:
                break;
        }
    }
out:
    if (opts) {
        av_em_dict_free(&opts);
    }
    if (!need_reconfigure_codec) {
        avcodec_em_free_context(&avctx);
    }
    return 0;
fail:
    if (opts) {
        av_em_dict_free(&opts);
    }
    if (avctx) {
        avcodec_em_free_context(&avctx);
    }
    return 0;
}

/* open a given stream. Return 0 if OK */
static int stream_component_open(FFPlayer *ffp, int stream_index)
{
    VideoState *is = ffp->is;
    AVEMFormatContext *ic = is->ic;
    AVEMCodecContext *avctx;
    AVEMCodec *codec = NULL;
    const char *forced_codec_name = NULL;
    AVEMDictionary *opts = NULL;
    AVEMDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = ffp->lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    avctx = avcodec_em_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);
    av_em_log(NULL, AV_LOG_WARNING, "alloc new avctx addr:%p.\n", avctx);
    ret = avcodec_em_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    av_em_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);

    codec = avcodec_em_find_decoder(avctx->codec_id);

    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name = ffp->audio_codec_name; break;
        // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name = ffp->video_codec_name; break;
        default: break;
    }
    if (forced_codec_name)
        codec = avcodec_em_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_em_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_em_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with id %d\n", avctx->codec_id);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if(stream_lowres > av_em_codec_get_max_lowres(codec)){
        av_em_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                av_em_codec_get_max_lowres(codec));
        stream_lowres = av_em_codec_get_max_lowres(codec);
    }
    av_em_codec_set_lowres(avctx, stream_lowres);

#if FF_API_EMU_EDGE
    if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
    if (ffp->fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
    if(codec->capabilities & AV_CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

    opts = filter_codec_opts(ffp->codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_em_dict_get(opts, "threads", NULL, 0))
        av_em_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_em_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_em_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_em_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_em_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_em_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;

        /* prepare audio output */
        if ((ret = audio_open(ffp, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        ffp_set_audio_codec_info(ffp, AVCODEC_MODULE_NAME, avcodec_em_get_name(avctx->codec_id));
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = 2.0 * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        if ((ret = decoder_start(&is->auddec, audio_thread, ffp, "ff_audio_dec")) < 0)
            goto out;
        av_em_log(NULL, AV_LOG_INFO, "start ff audio dec thread succ");
        SDL_AoutPauseAudio(ffp->aout, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];
        decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
        ffp->node_vdec = ffpipeline_open_video_decoder(ffp->pipeline, ffp);
        if (!ffp->node_vdec)
            goto fail;
        if ((ret = decoder_start(&is->viddec, video_thread, ffp, "ff_video_dec")) < 0)
            goto out;
        av_em_log(NULL, AV_LOG_INFO, "start ff video dec thread succ, takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);

        is->queue_attachments_req = 1;

        if (ffp->max_fps >= 0) {
            if(is->video_st->avg_frame_rate.den && is->video_st->avg_frame_rate.num) {
                double fps = av_em_q2d(is->video_st->avg_frame_rate);
                SDL_ProfilerReset(&is->viddec.decode_profiler, fps + 0.5);
                if (fps > ffp->max_fps && fps < 130.0) {
                    is->is_video_high_fps = 1;
                    av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (too high)\n", fps);
                } else {
                    av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (normal)\n", fps);
                }
            }
            if(is->video_st->r_frame_rate.den && is->video_st->r_frame_rate.num) {
                double tbr = av_em_q2d(is->video_st->r_frame_rate);
                if (tbr > ffp->max_fps && tbr < 130.0) {
                    is->is_video_high_fps = 1;
                    av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (too high)\n", tbr);
                } else {
                    av_em_log(ffp, AV_LOG_WARNING, "fps: %lf (normal)\n", tbr);
                }
            }
        }

        if (is->is_video_high_fps) {
            avctx->skip_frame       = FFMAX(avctx->skip_frame, AVDISCARD_NONREF);
            avctx->skip_loop_filter = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
            avctx->skip_idct        = FFMAX(avctx->skip_loop_filter, AVDISCARD_NONREF);
        }

        break;
    // FFP_MERGE: case AVMEDIA_TYPE_SUBTITLE:
    default:
        break;
    }
    goto out;

fail:
    avcodec_em_free_context(&avctx);
out:
    av_em_dict_free(&opts);

    return ret;
}


static int decode_interrupt_cb(void *ctx)
{
    AVEMFormatContext *ic = ctx;
    FFPlayer *ffp = ic->opaque;
    int ret = 0;
    SDL_LockMutex(ffp->reconfigure_mutex);
    VideoState *is = ffp->is;
    if (is && is->ic == ic) {
        ret = is->abort_request || is->pause_buffering || (is->network_disconnect && av_em_gettime_relative() - is->network_disconnect_time < is->reconnect_interval * 1000000);
    } else {
        ret = ffp->prepare_abort;
        //fix:当没有连接上服务器时，不加上这个判断可能会无法退出
        //时间:2018-12-20
        if (is) {
            ret = ffp->prepare_abort | is->abort_request;
        }
    }
    SDL_UnlockMutex(ffp->reconfigure_mutex);
    return ret;
}

static int stream_has_enough_packets(AVEMStream *st, int stream_id, PacketQueue *queue, int min_frames) {
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
#ifdef FFP_MERGE
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_em_q2d(st->time_base) * queue->duration > 1.0);
#endif
           queue->nb_packets > min_frames;
}

static int is_realtime(AVEMFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
       || !strcmp(s->iformat->name, "rtmp")
    )
        return 1;

    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                 || !strncmp(s->filename, "udp:", 4)
                )
    )
        return 1;
    return 0;
}


static int is_ffp_in_live_mode(FFPlayer * ffp)
{
    if (ffp->play_mode == FFP_PLAY_MODE_FLV_LIVE ||
            ffp->play_mode == FFP_PLAY_MODE_RTMP) {
        return  1;
    }
    return 0;
}
    
static int is_ffp_in_vod_mode(FFPlayer *ffp)
{
    if (ffp->play_mode == FFP_PLAY_MODE_VOD_FLV ||
        ffp->play_mode == FFP_PLAY_MODE_VOD_HLS ||
        ffp->play_mode == FFP_PLAY_MODE_VOD_MP4 ) {
        return  1;
    }
    return 0;
}

    
static void flush_all_packets_frames(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    ffp_toggle_buffering(ffp, 1);
    ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);
    ffp->error = 0;
    if (is->audio_stream >= 0) {
        packet_queue_flush(&is->audioq);
        packet_queue_put(&is->audioq, &flush_pkt);
        frame_queue_empty(&is->sampq);
    }
    if (is->video_stream >= 0) {
        //is->viddec.clear_picture_flushed = 1;
        if (ffp->node_vdec) {
            ffpipenode_flush(ffp->node_vdec);
        }
        packet_queue_flush(&is->videoq);
        packet_queue_put(&is->videoq, &flush_pkt);
        frame_queue_empty(&is->pictq);
    }
}

static int do_change_video_source_internal(FFPlayer *ffp, ffplay_format_t *play_format, int need_close_source)
{
    VideoState *is = ffp->is;
    int ret = 0;
    int video_stream = -1, audio_stream = -1;
    av_em_log(NULL, AV_LOG_INFO, "enter func:%s.\n", __func__);
    AVEMFormatContext *ic = play_format->ic;
    video_stream = play_format->stream_index[AVMEDIA_TYPE_VIDEO];
    if (video_stream >= 0) {
        av_em_log(NULL, AV_LOG_INFO, "video stream component reconfigure start.\n");
        ret = stream_component_reconfigure(ffp, ic, video_stream);
        av_em_log(NULL, AV_LOG_INFO, "video stream component reconfigure end.\n");
        if (ret < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "stream reconfigure error.\n");
            ret = -1;
            return ret;
        }
        is->viddec.first_frame_decoded = 0;
        is->viddec.first_frame_decoded_time = SDL_GetTickHR();
    } else if (is->video_stream >= 0) {
        stream_component_close(ffp, is->video_stream);
    }
    audio_stream = play_format->stream_index[AVMEDIA_TYPE_AUDIO];
    if (audio_stream >= 0) {
        av_em_log(NULL, AV_LOG_INFO, "audio stream component reconfigure start.\n");
        ret = stream_component_reconfigure(ffp, ic, audio_stream);
        av_em_log(NULL, AV_LOG_INFO, "audio stream component reconfigure end.\n");
        if (ret < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "stream reconfigure error.\n");
            ret = -1;
            return ret;
        }
        is->auddec.first_frame_decoded = 0;
        is->auddec.first_frame_decoded_time = SDL_GetTickHR();
    } else if (is->audio_stream >= 0) {
        stream_component_close(ffp, is->audio_stream);
    }
    SDL_LockMutex(ffp->reconfigure_mutex);
    if (is->seek_flags & AVSEEK_FLAG_BYTE) {
        set_clock(&is->extclk, NAN, 0);
    }
    is->latest_seek_load_serial = is->videoq.serial;
    is->latest_seek_load_start_at = av_em_gettime();
    //ffp->dcc.current_high_water_mark_in_ms = ffp->dcc.first_high_water_mark_in_ms;
    is->seek_req = 0;
    is->is_seek_find_next_frame = 0;
    is->queue_attachments_req = 1;
    is->eof = 0;
    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    is->max_frame_duration = 10.0;
    is->audio_buf_index = is->audio_buf_size = 0;
    is->realtime = is_realtime(ic);
    is->show_mode = ffp->show_mode;
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;
    ijkmeta_set_avformat_context_l(ffp->meta, ic);
    ffp->stat.bit_rate = ic->bit_rate;
    if (need_close_source) {
        SDL_UnlockMutex(ffp->reconfigure_mutex);
        avformat_em_close_input(&is->ic);
        SDL_LockMutex(ffp->reconfigure_mutex);
    }
    is->ic = ic;
    is->ic_start_time = fftime_to_milliseconds(is->ic->start_time);
    if (is->ic_start_time < 0 /*|| is->ic_start_time > ffp_get_duration_l(ffp)*/) {
        is->ic_start_time = 0;
    }
    is->pause_buffering = 0;
    is->buffering_start_ms = -1;
    is->network_disconnect = 0;
    is->reconnect_retry_count = 0;
    if (play_format->stream_index[AVMEDIA_TYPE_VIDEO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_VIDEO_STREAM, play_format->stream_index[AVMEDIA_TYPE_VIDEO]);
    if (play_format->stream_index[AVMEDIA_TYPE_AUDIO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_AUDIO_STREAM, play_format->stream_index[AVMEDIA_TYPE_AUDIO]);
    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_em_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        is->abort_request = 1;
        ffp_notify_msg1(ffp, FFP_MSG_ERROR_UNSUPPORTED_FORMAT);
        ret = -1;
        SDL_UnlockMutex(ffp->reconfigure_mutex);
        return ret;
    }
    if (is->audio_stream >= 0) {
        is->audioq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->audioq;
        is->audio_st = ic->streams[is->audio_stream];
        is->audio_st->discard = AVDISCARD_DEFAULT;
    }
    if (is->video_stream >= 0) {
        is->videoq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->videoq;
        is->video_st = ic->streams[is->video_stream];
        is->video_st->discard = AVDISCARD_DEFAULT;
    } else {
        assert("invalid streams");
    }
    set_clock(&is->extclk, NAN, 0);
    SDL_UnlockMutex(ffp->reconfigure_mutex);
    
//    if (!ffp->start_on_prepared)
//        toggle_pause(ffp, 1);
    if (!ffp->start_on_prepared){
        if(ffp->view_video_first_frame && is->video_st){
            ffp_set_mute_audio(ffp, 1);
        }
        else{
            toggle_pause(ffp, 1);
        }
        av_em_log(NULL, AV_LOG_INFO, "auto play false, view first video frame:%d\n", ffp->view_video_first_frame);
    }
    
    SDL_LockMutex(ffp->is->play_mutex);
    if (ffp->infinite_buffer < 0 && is->realtime)
        ffp->infinite_buffer = 1;
//    if (!ffp->start_on_prepared) // deadlock 
//        toggle_pause(ffp, 1);
    if (is->video_st && is->video_st->codecpar) {
        AVEMCodecParameters *codecpar = is->video_st->codecpar;
        ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, ffp_get_video_rotate_degrees(ffp));
        ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, codecpar->width, codecpar->height);
        ffp_notify_msg3(ffp, FFP_MSG_SAR_CHANGED, codecpar->sample_aspect_ratio.num, codecpar->sample_aspect_ratio.den);
    }
    
    if (ffp->auto_resume) {
        is->pause_req = 0;
        if (ffp->packet_buffering)
            is->buffering_on = 1;
        ffp->auto_resume = 0;
        stream_update_pause_l(ffp);
    }
    if (is->pause_req)
        step_to_next_frame_l(ffp);
    SDL_UnlockMutex(ffp->is->play_mutex);
    ffp_toggle_buffering(ffp, 1);
    ffp->first_video_frame_rendered = 0;
    ffp_notify_msg1(ffp, FFP_MSG_PREPARED);
    av_em_log(NULL, AV_LOG_INFO, "exit func:%s.\n", __func__);
    return 0;
}

static int do_read_seek_internal(FFPlayer *ffp, VideoState *is)
{
    int ret = 0;
    int64_t seek_target = is->seek_pos;
    int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
    int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables
    
    ffp_toggle_buffering(ffp, 1);
    ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);
    ffp->error = 0;
    av_em_log(NULL, AV_LOG_WARNING, "seek file , target:%lld", seek_target);
    ret = avformat_em_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
    if (ret < 0 && ret != AVERROR_EOF) {
        av_em_log(NULL, AV_LOG_ERROR,
               "%s: error while seeking:%d\n", is->ic->filename, ret);
        if (ret == AVERROR_EOF) {
            is->seek_req = 0;
        }
        return ret;
    } else {
        if (is->audio_stream >= 0) {
            packet_queue_flush(&is->audioq);
            packet_queue_put(&is->audioq, &flush_pkt);
        }
        if (is->video_stream >= 0) {
            if (ffp->node_vdec) {
                ffpipenode_flush(ffp->node_vdec);
            }
            packet_queue_flush(&is->videoq);
            packet_queue_put(&is->videoq, &flush_pkt);
        }
        if (is->seek_flags & AVSEEK_FLAG_BYTE) {
            set_clock(&is->extclk, NAN, 0);
        } else {
            set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
        }
        is->latest_seek_load_serial = is->videoq.serial;
        is->latest_seek_load_start_at = av_em_gettime();
    }
    //ffp->dcc.current_high_water_mark_in_ms = ffp->dcc.first_high_water_mark_in_ms;


    is->seek_req = 0;
    if (is->video_stream < 0) {
        is->is_seeking = 0;
    }
    is->queue_attachments_req = 1;
    is->eof = 0;
    SDL_LockMutex(ffp->is->play_mutex);
    if (ffp->auto_resume) {
        is->pause_req = 0;
        if (ffp->packet_buffering)
            is->buffering_on = 1;
        ffp->auto_resume = 0;
        stream_update_pause_l(ffp);
    }
    if (is->pause_req)
        step_to_next_frame_l(ffp);
    SDL_UnlockMutex(ffp->is->play_mutex);
    //ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, (int)fftime_to_milliseconds(seek_target), ret);
    ffp_toggle_buffering(ffp, 1);
    return ret;
}

int create_avformat_internal(FFPlayer *ffp, VideoState *is, char *filename, int play_type, AVEMInputFormat *iformat, AVEMFormatContext **ic_out)
{
    int err = 0;
    int reconnectCount = 0;
    int scan_all_pmts_set = 0;
    AVEMDictionaryEntry *t;
    AVEMDictionary *format_opts = NULL;
    if (av_em_dict_copy(&format_opts, ffp->format_opts, 0) < 0) {
        av_em_log(NULL, AV_LOG_ERROR, "copy format opts failed.\n");
        return -1;
    }

    AVEMFormatContext*ic = avformat_em_alloc_context();
    if (!ic) {
        av_em_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        err = AVERROR(ENOMEM);
        return err;
    }
    ic->opaque = ffp;
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = ic;
    av_em_log(NULL, AV_LOG_INFO, "set interrupt callback opaque:%p, ffplayer:%p.\n", ic, ic->opaque);
    if (!av_em_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_em_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    if (av_em_stristart(filename, "rtmp", NULL) ||
        av_em_stristart(filename, "rtsp", NULL)) {
        // There is total different meaning for 'timeout' option in rtmp
          av_em_log(ffp, AV_LOG_WARNING, "remove 'timeout' option for rtmp.\n");
          av_em_dict_set(&format_opts, "timeout", NULL, 0);
          av_em_dict_set(&format_opts, "rw_timeout", "10000000", 0);
          av_em_dict_set(&format_opts, "use_ijktcphook", "1", 0);
    }
    if (strlen(filename) + 1 > 1024) {
        av_em_log(ffp, AV_LOG_ERROR, "%s too long url\n", __func__);
        if (avio_em_find_protocol_name("ijklongurl:")) {
            av_em_dict_set(&format_opts, "ijklongurl-url", filename, 0);
            filename = "ijklongurl:";
        }
    }
    //av_em_dict_set(&ffp->format_opts, "http_proxy", "http://172.16.63.104:8888", 0);
    if (is && ffp->iformat_name)
        is->iformat = av_em_find_input_format(ffp->iformat_name);
    if ((av_em_stristart(filename, "http", NULL) && (play_type == FFP_PLAY_MODE_VOD_FLV || play_type == FFP_PLAY_MODE_VOD_HLS || play_type == FFP_PLAY_MODE_VOD_MP4)) ||
        av_em_stristr(filename, "emmul"))
    {
        av_em_dict_set_int(&format_opts, "reconnect_interval", 3, 0);
        av_em_dict_set_int(&format_opts, "retry_count", 20, 0);
        av_em_dict_set_int(&format_opts, "b_save_prefix_buffer", 1, 0);
        av_em_dict_set_int(&format_opts, "prefix_buffer_max_size", 512000, 0);
        av_em_dict_set_int(&format_opts, "reconnect_delay_max", 0, 0);
        
        av_em_dict_set_int(&format_opts, "probesize", ffp->probesize * 1024, 0);
        av_em_dict_set_int(&format_opts, "analyzeduration", 0, 0);
        av_em_dict_set_int(&format_opts, "dns_timeout", ffp->dns_timeout, 0);
        av_em_dict_set_int(&format_opts, "dns_cache_count", ffp->dns_cache_count, 0);
    }
    
    if (scan_all_pmts_set)
        av_em_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    if ((t = av_em_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_em_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
    }
    for (;;) {
        err = avformat_em_open_input(&ic, filename, iformat, &format_opts);
        if (err == AVERROR_HTTP_FORBIDDEN) {
            av_em_log(NULL, AV_LOG_ERROR, "open input failed:AVERROR_HTTP_FORBIDDEN!\n");
            av_em_dict_free(&format_opts);
            return -403;
        }
        
        if (err >=  0) {
            av_em_log(NULL, AV_LOG_INFO, "open input success, takes time:%lld\n", ijk_get_timems() - ffp->prepared_timems);
            break;
        }

        if (reconnectCount >= ffp->reconnect_count) {
            av_em_log(NULL, AV_LOG_ERROR, "open input failed.\n");
            av_em_dict_free(&format_opts);
            return -1;
        }
        reconnectCount++;
        int sleep_us = 0;
        while (sleep_us < ffp->reconnect_interval * 1000000) {
            if ((is != NULL && is->abort_request) || (is == NULL && ffp->prepare_source_abort)) {
                av_em_dict_free(&format_opts);
                return -1;
            }
            usleep(20000);
            sleep_us += 20000;
        }
        av_em_log(NULL, AV_LOG_WARNING, "open input failed, retry connect, count:%d is handle is %d\n", reconnectCount, is == NULL);
        print_error(filename, err);
    }
    av_em_dict_free(&format_opts);
    if (ffp->genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;
    av_em_format_inject_global_side_data(ic);
    *ic_out = ic;
    return 0;
}
    
static void ffp_set_network_disconnect(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    int64_t cur_time = av_em_gettime_relative();
    is->network_disconnect = 1;
    if (cur_time - is->network_disconnect_time >= is->reconnect_interval * 1000000) {
        is->network_disconnect_time = cur_time;
        if (is->reconnect_retry_count > is->reconnect_count) {
            ffp_notify_msg1(ffp, FFP_MSG_ERROR_NET_DISCONNECT);
        }
        is->reconnect_retry_count ++;
        av_em_log(NULL, AV_LOG_INFO, "network disconnect, time:%lld, retry count:%d.\n", is->network_disconnect_time, is->reconnect_retry_count);
    }
}

static int check_play_complete_internal(FFPlayer *ffp, int completed, SDL_mutex *wait_mutex, int force_completed) {
    VideoState *is = ffp->is;
    int ret = -1;
    if ((!is->paused || completed) &&
        (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
        (!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0)) ||
        force_completed) {
        av_em_log(NULL, AV_LOG_INFO, "decode all frames.\n");
        if (ffp->loop != 1 && (!ffp->loop || --ffp->loop)) {
            stream_seek(is, ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0, 0, 0);
            ret = 1; //play from begin
        } else if (ffp->autoexit) {
            ret = AVERROR_EOF;
        } else {
            ffp_statistic_l(ffp);
            if (completed) {
                av_em_log(ffp, AV_LOG_INFO, "ffp_toggle_buffering: eof\n");
                return AVERROR_EOF;
            } else {
                completed = 1;
                ffp->auto_resume = 0;
                // TODO: 0 it's a bit early to notify complete here
                ffp_toggle_buffering(ffp, 0);
                //toggle_pause(ffp, 1);
                if (ffp->error) {
                    av_em_log(ffp, AV_LOG_INFO, "ffp_toggle_buffering: error: %d\n", ffp->error);
                    if (ffp->error == AVERROR(ETIMEDOUT)) {
                        //ffp_notify_msg1(ffp, FFP_MSG_ERROR_NET_DISCONNECT);
                        ffp_set_network_disconnect(ffp);
                    } else {
                        ffp_notify_msg1(ffp, FFP_MSG_ERROR);
                    }
                } else {
                    av_em_log(ffp, AV_LOG_INFO, "ffp_toggle_buffering: completed: OK\n");
                    ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
                }
                return 0;
            }
        }
    }
    return ret;
}

static int ffp_get_available_prepared_video_source_index(FFPlayer *ffp)
{
    int i = 0;
    if (ffp->prepared_source_count >= FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE) {
        return -1;
    }
    for (i = 0; i < FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE; i++) {
        if (ffp->prepared_source[i].ic == NULL) {
            return i;
        }
    }
    av_em_log(NULL, AV_LOG_ERROR, "fatal error!!!prepare source count not exceed max, but no available index.\n");
    return -1;
}

int ffp_prepare_new_video_source_l(FFPlayer *ffp, char *video_path, int playType)
{
    int index = 0;
    SDL_LockMutex(ffp->prepared_lock);
    if (ffp->prepared_source_count >= FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE) {
        av_em_log(NULL, AV_LOG_ERROR, "prepared video source exceed max count, cannot prepared now.\n");
        SDL_UnlockMutex(ffp->prepared_lock);
        return -1;
    }
    index = ffp_get_available_prepared_video_source_index(ffp);
    if (index < 0) {
        av_em_log(NULL, AV_LOG_ERROR, "get available video source prepared index failed.\n");
        SDL_UnlockMutex(ffp->prepared_lock);
        return -1;
    }
    strcpy(ffp->prepared_source[index].filename, video_path);
    ffp->prepared_source[index].fileType = playType;
    ffp->prepared_source[index].ic = FFP_PREPARE_VIDEO_SOURCE_FLAG; //indicate this addr is for used.
    ffp->prepared_source_count++;
    av_em_log(NULL, AV_LOG_INFO, "prepare video source count:%d.\n", ffp->prepared_source_count);
    SDL_CondSignal(ffp->prepare_cond);
    SDL_UnlockMutex(ffp->prepared_lock);
    return index;
}
    
static int ffp_change_video_source_internal(FFPlayer *ffp, char *path, int playType)
{
    if (path != NULL) {
        SDL_LockMutex(ffp->change_source_lock);
        strcpy(ffp->new_video_path, path);
        ffp->new_video_type = playType;
        ffp->cur_format = NULL;
        ffp->b_change_source = 1;
        SDL_UnlockMutex(ffp->change_source_lock);
        return 0;
    }
    return -1;
}

int ffp_change_video_source_with_prepared_index_l(FFPlayer *ffp, int index)
{
    int ret = 0;
    SDL_LockMutex(ffp->prepared_lock);
    ffplay_format_t *avformat = &ffp->prepared_source[index];
    if (avformat->ic && avformat->ic != FFP_PREPARE_VIDEO_SOURCE_FLAG && avformat->ic != FFP_PREPARING_VIDEO_SOURCE_FLAG) {
        //ffp->prepared_source_count--;
    } else if (avformat->ic == FFP_PREPARING_VIDEO_SOURCE_FLAG) {
        av_em_log(NULL, AV_LOG_INFO, "video source index:%d is preparing now.\n", index);
        for (int i = 0; i < FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE; i++) {
            ffp->prepared_source[i].play_after_prepared = 0;
        }
        ffp->prepared_source[index].play_after_prepared = 1;
        ret = 1;
    } else if (avformat->ic == FFP_PREPARE_VIDEO_SOURCE_FLAG) {
        av_em_log(NULL, AV_LOG_INFO, "video source index:%d is not prepare success, change source with url directly.\n", index);
        for (int i = 0; i < FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE; i++) {
            ffp->prepared_source[i].play_after_prepared = 0;
        }
        if (ffp_change_video_source_internal(ffp, avformat->filename, avformat->fileType)) {
            ret = -1;
        }
        //avformat->ic = NULL;
        //ffp->prepared_source_count--;
        ret = 2;
    } else {
        ret = -1;
    }
    SDL_UnlockMutex(ffp->prepared_lock);
    if (!ret) {
        ffplay_format_t *avformat1 = (ffplay_format_t *)av_em_alloc(sizeof(ffplay_format_t));
        if (avformat1) {
            memcpy(avformat1, avformat, sizeof(ffplay_format_t));
            //avformat->ic = NULL;
            avformat->play_after_prepared = 0;
            SDL_LockMutex(ffp->change_source_lock);
            ffp->cur_format = avformat1;
            ffp->b_change_source = 1;
            SDL_UnlockMutex(ffp->change_source_lock);
        } else
            ret = -1;
    }
    return ret;
}

int ffp_delete_prepared_video_source(FFPlayer *ffp, int index)
{
    int ret = 0;
    SDL_LockMutex(ffp->prepared_lock);
    ffplay_format_t *avformat = &ffp->prepared_source[index];
    if (avformat->ic && avformat->ic != FFP_PREPARE_VIDEO_SOURCE_FLAG && avformat->ic != FFP_PREPARING_VIDEO_SOURCE_FLAG) {
        VideoState *is = ffp->is;
        SDL_LockMutex(ffp->change_source_lock);
        if (is && is->ic != avformat->ic && (!ffp->cur_format || ffp->cur_format->ic != avformat->ic)) {
            avformat_em_close_input(&avformat->ic);
            SDL_UnlockMutex(ffp->change_source_lock);
        } else if (!is->pause_buffering){
            SDL_UnlockMutex(ffp->change_source_lock);
            SDL_UnlockMutex(ffp->prepared_lock);
            av_em_log(NULL, AV_LOG_INFO, "this video source is playing now.\n");
            return -1;
        }
    }
    avformat->ic = NULL;
    ffp->prepared_source_count--;
    SDL_UnlockMutex(ffp->prepared_lock);
    return ret;
}


/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    AVEMFormatContext *ic = NULL;
    int err, ret;
    AVEMPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int completed = 0;
    int pkt_in_play_range = 0;
    SDL_mutex *wait_mutex = SDL_CreateMutex();
    int64_t pkt_ts;
    int64_t stream_unix_time;
    int last_error = 0;
    int64_t prev_io_tick_counter = 0;
    int64_t io_tick_counter = 0;
    int force_completed = 0;
    int reconnect_count = 0;
    ffplay_format_t *source_format;
    int source_play_type;
    av_em_log(NULL, AV_LOG_INFO, "enter thread:%s\n", __func__);
    if (!wait_mutex) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
    is->eof = 0;
    is->reconnect_interval = ffp->reconnect_interval;
    is->reconnect_count = ffp->reconnect_count;
    is->reconnect_retry_count = 0;//
    is->show_mode = ffp->show_mode;
    ffplay_format_t *play_format = NULL;
    ret = prepare_source_internal(ffp, ffp->is, is->filename, ffp->play_mode, is->iformat, &play_format);
    if (ret < 0 || !play_format) {
        ffp_notify_msg2(ffp, FFP_MSG_ERROR_CONNECT_FAILD, ret);
        ret = -1;
        goto standby;
    }
    ic = play_format->ic;
    is->ic = ic;
    is->ic_start_time = fftime_to_milliseconds(is->ic->start_time);
    if (is->ic_start_time < 0 /*|| is->ic_start_time > ffp_get_duration_l(ffp)*/) {
        is->ic_start_time = 0;
    }
    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    is->max_frame_duration = 10.0;
    is->realtime = is_realtime(ic);
    av_em_freep(&ffp->input_filename);
    ffp->input_filename = strdup(is->filename);
    /* open the streams */
    if (play_format->stream_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        ret = stream_component_open(ffp, play_format->stream_index[AVMEDIA_TYPE_AUDIO]);
        if (ret < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "stream component open audio failed.\n");
        }
    }
    if (play_format->stream_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(ffp, play_format->stream_index[AVMEDIA_TYPE_VIDEO]);
        if (ret < 0) {
            av_em_log(NULL, AV_LOG_ERROR, "stream component open video failed.\n");
        }
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;
    ijkmeta_set_avformat_context_l(ffp->meta, ic);
    ffp->stat.bit_rate = ic->bit_rate;
    if (play_format->stream_index[AVMEDIA_TYPE_VIDEO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_VIDEO_STREAM, play_format->stream_index[AVMEDIA_TYPE_VIDEO]);
    if (play_format->stream_index[AVMEDIA_TYPE_AUDIO] >= 0)
        ijkmeta_set_int64_l(ffp->meta, IJKM_KEY_AUDIO_STREAM, play_format->stream_index[AVMEDIA_TYPE_AUDIO]);
    av_em_free(play_format);
    if (is->video_stream < 0 && is->audio_stream < 0) {
        av_em_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        is->abort_request = 1;
        ffp_notify_msg1(ffp, FFP_MSG_ERROR_UNSUPPORTED_FORMAT);
        ret = -1;
        goto fail;
    }
    if (is->audio_stream >= 0) {
        is->audioq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->audioq;
    } else if (is->video_stream >= 0) {
        is->videoq.is_buffer_indicator = 1;
        is->buffer_indicator_queue = &is->videoq;
    } else {
        assert("invalid streams");
    }

    if (ffp->infinite_buffer < 0 && is->realtime)
        ffp->infinite_buffer = 1;
    
#if 0
    if (!ffp->start_on_prepared)
        toggle_pause(ffp, 1);
#else
    if (!ffp->start_on_prepared){
        if(ffp->view_video_first_frame && is->video_st){
            ffp_set_mute_audio(ffp, 1);
        }
        else{
            toggle_pause(ffp, 1);
        }
        av_em_log(NULL, AV_LOG_INFO, "auto play false, view first video frame:%d\n", ffp->view_video_first_frame);
    }
           
#endif
    if (is->video_st && is->video_st->codecpar) {
        AVEMCodecParameters *codecpar = is->video_st->codecpar;
        ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, ffp_get_video_rotate_degrees(ffp));
        ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, codecpar->width, codecpar->height);
        ffp_notify_msg3(ffp, FFP_MSG_SAR_CHANGED, codecpar->sample_aspect_ratio.num, codecpar->sample_aspect_ratio.den);
    }
    ffp->prepared = true;
    ffp_notify_msg1(ffp, FFP_MSG_PREPARED);
    
#if 0 // auto play loading buffer enable 2020/12/18
    if (!ffp->start_on_prepared) {
        while (is->pause_req && !is->abort_request) {
            SDL_Delay(100);
        }
    }
#endif
    if (ffp->auto_resume) {
        ffp_notify_msg1(ffp, FFP_REQ_START);
        ffp->auto_resume = 0;
    }
    /* offset should be seeked*/
    if (ffp->seek_at_start > 0) {
        ffp_seek_to_l(ffp, ffp->seek_at_start);
    } else {
        //avformat_seek_file(is->ic, -1, 0, 0, 0, AVSEEK_FLAG_BYTE);
    }
    for (;;) {
        if (is->abort_request)
            break;
        SDL_LockMutex(ffp->change_source_lock);
        if (ffp->b_change_source) {
            av_em_log(NULL, AV_LOG_INFO, "change video source");
            ffp->b_change_source = 0;
            flush_all_packets_frames(ffp);
            if (ffp->cur_format) {
                av_em_log(NULL, AV_LOG_INFO, "avformat context addr:%p\n", ffp->cur_format->ic);
                source_format = ffp->cur_format;
                ffp->cur_format = NULL;
                SDL_UnlockMutex(ffp->change_source_lock);
                ret = do_change_video_source_internal(ffp, source_format, !is->prepared_source);
                av_em_freep(&ffp->input_filename);
                ffp->input_filename = strdup(source_format->filename);
                is->prepared_source = 1;
                ic = source_format->ic;
                avformat_em_flush(ic);
                if (source_format->fileType == FFP_PLAY_MODE_VOD_MP4) {
                    //av_seek_frame(ic, -1, 0, 0);
                    avformat_em_seek_file(ic, -1, INT_MIN, 0, INT_MAX, 0);
                } else if (source_format->fileType == FFP_PLAY_MODE_VOD_FLV){
                    avio_em_fast_seek_begin(ic->pb);
                }
                ffp->play_mode = source_format->fileType;
                av_em_free(source_format);
                source_format = NULL;
            } else {
                ffplay_format_t *ic_format = NULL;
                source_play_type = ffp->new_video_type;
                SDL_UnlockMutex(ffp->change_source_lock);
                int64_t before_time = ijk_get_timems();
                ffp->prepare_source_abort = 0;
                ret = prepare_source_internal(ffp, NULL, ffp->new_video_path, ffp->new_video_type, NULL, &ic_format);
                av_em_log(NULL, AV_LOG_INFO, "prepare source takes time:%lld.\n", ijk_get_timems() - before_time);
                if (ret < 0 || !ic_format) {
                    ffp_notify_msg2(ffp, FFP_MSG_ERROR_CONNECT_FAILD, ret);
                    goto standby;
                    //break;
                }
                ret = do_change_video_source_internal(ffp, ic_format, !is->prepared_source);
                is->prepared_source = 0;
                ic = ic_format->ic;
                ffp->play_mode = ffp->new_video_type;
                av_em_freep(&ffp->input_filename);
                ffp->input_filename = strdup(ffp->new_video_path);
                av_em_free(ic_format);
            }
            if (ret < 0) {
                break;
            }
            prev_io_tick_counter = 0;
            completed = 0;
            //ffp_start_l(ffp);
            is->is_seeking = 0;
            is->seek_req = 0;
            ffp_notify_msg1(ffp, FFP_CHANGE_VIDEO_SOURCE_SUCCESS);
            av_em_log(NULL, AV_LOG_INFO, "change video source success.\n");
        } else {
            SDL_UnlockMutex(ffp->change_source_lock);
        }

        if (is->seek_req && !is->is_seeking) {
            av_em_log(NULL, AV_LOG_INFO, "seek video source");
            is->is_seeking = 1;
            ret = do_read_seek_internal(ffp, is);
            if (ret == AVERROR_EOF) {
                av_em_log(NULL, AV_LOG_WARNING, "seek to end of stream.\n");
                force_completed = 1;
            } else if (ret < 0) {
                ffp->error = is->ic->pb->error;
                if (is->ic->pb->error == AVERROR(ETIMEDOUT)) {
                    ffp_set_network_disconnect(ffp);
                }
            } else {
                force_completed = 0;
                completed = 0;
                if(is->video_st){
                    is->is_seek_find_next_frame = 1;
                }
            }
        }

        if (is->queue_attachments_req) {
            if (is->video_st && (is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                AVEMPacket copy;
                if ((ret = av_em_copy_packet(&copy, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, &copy);
                packet_queue_put_nullpacket(&is->videoq, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (ffp->infinite_buffer<1 && !is->seek_req &&
              (is->audioq.size + is->videoq.size > ffp->dcc.max_buffer_size
                || (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq, MIN_FRAMES)
                        && stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq, MIN_FRAMES)) || is->pause_buffering )) {
            if (!is->eof) {
                ffp_toggle_buffering(ffp, 0);
            }
            /* wait 10 ms */
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        ret = check_play_complete_internal(ffp, completed, wait_mutex, force_completed);
        force_completed = 0;
        if (ret == AVERROR_EOF) {
            av_em_log(NULL, AV_LOG_INFO, "read all frames completed ok.\n");
            SDL_LockMutex(wait_mutex);
            // infinite wait may block shutdown
standby:
            while (!is->abort_request && !is->seek_req && !ffp->b_change_source) {
                SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 100);
            }
            SDL_UnlockMutex(wait_mutex);
            if (ffp->b_change_source || is->seek_req) {
                continue;
            }
            goto fail;
        } else if (ret == 0) {
            completed = 1;
            continue;
        } else if (ret == 1) {
            //play complete, loop play from begin.
            prev_io_tick_counter = 0;
        }
        pkt->flags = 0;
        if (is->eof) {
            usleep(10 * 1000);
            continue;
        }
        //av_em_log(NULL, AV_LOG_INFO, "will enter av read frame.\n");
        
        ret = av_em_read_frame(ic, pkt);
        if (ret < 0) {
            int pb_eof = 0;
            int pb_error = 0;
            //av_em_log(NULL, AV_LOG_INFO, "read frame failed.\n");
            if (is_ffp_in_live_mode(ffp)) {
                int retcode = -ret;
                av_em_log(NULL, AV_LOG_WARNING, "av read frame error:%c%c%c%c, pb error:%d.\n", \
                          retcode & 0xFF, (retcode >> 8) & 0xFF, (retcode >> 16) & 0xFF, (retcode >> 24) & 0xFF, ic->pb->error);
#if 0
                reconnect_count++;
                if (reconnect_count >= ffp->reconnect_count) {
                    av_em_log(NULL, AV_LOG_ERROR, "av read frame error, net disconnect.\n");
                    ffp_set_network_disconnect(ffp);
                    ic->pb->error = 0;
                    ic->pb->eof_reached = 0;
                    is->abort_request = 1;
                    ffp_notify_msg1(ffp, FFP_MSG_ERROR_NET_DISCONNECT);
                    goto fail;
                }
#else
                if(reconnect_count){
                    av_em_log(NULL, AV_LOG_ERROR, "av read frame error, net disconnect.\n");
                    ffp_set_network_disconnect(ffp);
                    ic->pb->error = 0;
                    ic->pb->eof_reached = 0;
                    is->abort_request = 1;
                    ffp_notify_msg1(ffp, FFP_MSG_ERROR_NET_DISCONNECT);
                    goto fail;
                }
                else{
                    reconnect_count++;
                    av_em_log(NULL, AV_LOG_ERROR, "av read frame error, net reconnect...\n");
                    ffp_notify_msg1(ffp, FFP_MSG_WARN_RECONNECT);
                    ffp_change_video_source(ffp, ffp->input_filename, ffp->play_mode);
                }
#endif
                SDL_LockMutex(wait_mutex);
                SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
                SDL_UnlockMutex(wait_mutex);
                ffp_statistic_l(ffp);

                continue;
            } else if (ret == AVERROR_INVALIDDATA && is_ffp_in_vod_mode(ffp)) {
                ffp_set_network_disconnect(ffp);
            } else if (ffp->play_mode == FFP_PLAY_MODE_LOCAL_VIDEO) {
                //av_em_log(NULL, AV_LOG_ERROR, "av read frame error, read local file frame failed.\n");
            }

            if ((ret == AVERROR_EOF || avio_em_feof(ic->pb)) && !is->eof) {
                if (ic->pb->error != AVERROR(ETIMEDOUT) && ic->pb->error != AVERROR_EXIT) {
                    pb_eof = 1;
                }
                // check error later
            }
            if (ic->pb && ic->pb->error && ic->pb->error != AVERROR(ETIMEDOUT)) {
                if (ic->pb->error == AVERROR_EXIT && is->abort_request) {
                    pb_eof = 1;
                    pb_error = ic->pb->error;
                }
            }
            
            if (ret == AVERROR_EXIT) {
                if (is->abort_request) {
                    pb_eof = 1;
                    pb_error = AVERROR_EXIT;
                }
            }

            if (pb_eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                is->eof = 1;
            }
            if (pb_error) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                is->eof = 1;
                ffp->error = pb_error;
               
                av_em_log(ffp, AV_LOG_ERROR, "av_read_frame error: %x(%c,%c,%c,%c): %s\n", ffp->error,
                      (char) (0xff & (ffp->error >> 24)),
                      (char) (0xff & (ffp->error >> 16)),
                      (char) (0xff & (ffp->error >> 8)),
                      (char) (0xff & (ffp->error)),
                      ffp_get_error_string(ffp->error));
                 break;
            } else {
                ffp->error = ic->pb->error;
                if (ffp->error == AVERROR(ETIMEDOUT) && ffp_is_paused_l(ffp)) {
                    //ffp->last_readpos = (int)ffp_get_current_position_l(ffp);
                    ffp_set_network_disconnect(ffp);
                }
            }
            if (is->eof) {
                ffp_toggle_buffering(ffp, 0);
                SDL_Delay(100);
            }
            ic->pb->error = 0;
            ic->pb->eof_reached = 0;
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            ffp_statistic_l(ffp);
            continue;
        } else {
            reconnect_count = 0;
            is->eof = 0;
            is->network_disconnect = 0;
            is->reconnect_retry_count = 0;
        }
        if (pkt->flags & AV_PKT_FLAG_DISCONTINUITY) {
            if (is->audio_stream >= 0) {
                packet_queue_put(&is->audioq, &flush_pkt);
            }
            if (is->video_stream >= 0) {
                packet_queue_put(&is->videoq, &flush_pkt);
            }
        }
        else if(is->is_seek_find_next_frame){
            if(pkt->stream_index == is->video_stream && pkt->flags == AV_PKT_FLAG_KEY){
                is->is_seek_find_next_frame = 0;
            }
            else{
                if(pkt->stream_index == is->video_stream && pkt->flags != AV_PKT_FLAG_KEY){
                    av_em_log(NULL, AV_LOG_INFO, "seek no key frame:%s\n", __func__);
                    av_em_packet_unref(pkt);
                    continue;
                }
            }
        }

        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = ffp->duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_em_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0) / 1000000
                <= ((double)ffp->duration / 1000000);
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
           // av_em_log(NULL, AV_LOG_INFO, "write audio packet, pts:%lld, size:%d\n", pkt->pts, pkt->size);
            if (!is->is_seeking || milliseconds_to_fftime(pkt->pts * av_em_q2d(is->audio_st->time_base) * 1000) > is->seek_pos ) {
                packet_queue_put(&is->audioq, pkt);
            }
            else {
               // av_em_log(NULL, AV_LOG_INFO, "audio pts is lower than seek target, target:%lld, pts:%lld.\n", is->seek_pos, milliseconds_to_fftime(pkt->pts * av_em_q2d(is->audio_st->time_base) * 1000));
            }

            //packet_queue_put(&is->audioq, pkt);
            //SDL_SpeedSampler3Add(&ffp->stat.audio_bitrate_sampler, pkt->pts, pkt->size);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st && (is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))) {
            //修复部分隔行扫描视频返回pts==AV_NOPTS_VALUE 导致部分华为手机上硬解异常的bug
            if(pkt_ts != AV_NOPTS_VALUE){
                is->is_last_dts = pkt->dts;
                is->is_last_pts = pkt->pts;
            }
            else{
                pkt->dts = is->is_last_dts;
                pkt->pts = is->is_last_pts;
            }
           // av_em_log(NULL, AV_LOG_INFO, "write video packet, pts:%lld, dts:%lld. \n", pkt->pts, pkt->dts);
            packet_queue_put(&is->videoq, pkt);
            //SDL_SpeedSampler3Add(&ffp->stat.video_bitrate_sampler, pkt->pts, pkt->size);
        } else {
            av_em_packet_unref(pkt);
        }
        ffp_statistic_l(ffp);
        if (ffp->packet_buffering) {
            io_tick_counter = SDL_GetTickHR();
            if (prev_io_tick_counter <= 0 && !is_ffp_in_live_mode(ffp)) {
                if ((is->audio_stream < 0 || packet_queue_get_count(&is->audioq) > 20) && (is->video_stream < 0 || packet_queue_get_count(&is->videoq) > 20))
                {
                    prev_io_tick_counter = io_tick_counter;
                    ffp_toggle_buffering(ffp, 0);
                }
            } else if (abs((int)(io_tick_counter - prev_io_tick_counter)) > BUFFERING_CHECK_PER_MILLISECONDS) {
                prev_io_tick_counter = io_tick_counter;
                ffp_check_buffering_l(ffp);
            }
        }
    }
    ret = 0;
 fail:
    if (!ffp->prepared || !is->abort_request) {
        ffp->last_error = last_error;
        ffp_notify_msg2(ffp, FFP_MSG_ERROR, last_error);
    }
    ffp_notify_msg1(ffp, FFP_MSG_EXIT_READ_THREAD);
    SDL_DestroyMutex(wait_mutex);
    av_em_log(NULL, AV_LOG_INFO, "exit thread:%s\n", __func__);
    return 0;
}
    
int ffp_change_video_source(FFPlayer *ffp, char *path, int playType)
{
    int ret = 0;
    SDL_LockMutex(ffp->prepared_lock);
    for (int i = 0; i < FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE; i++) {
        ffp->prepared_source[i].play_after_prepared = 0;
    }
    ret = ffp_change_video_source_internal(ffp, path, playType);
    SDL_UnlockMutex(ffp->prepared_lock);
    return ret;
}

static int video_refresh_thread(void *arg);
static VideoState *stream_open(FFPlayer *ffp, const char *filename, AVEMInputFormat *iformat)
{
    assert(!ffp->is);
    VideoState *is;

    is = av_em_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    is->filename = av_em_strdup(filename);
    if (!is->filename)
        goto fail;
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;
    is->buffering_start_ms = -1;
    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, ffp->pictq_size, 1) < 0)
        goto fail;
#ifdef FFP_MERGE
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
#endif
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
#ifdef FFP_MERGE
        packet_queue_init(&is->subtitleq) < 0)
#else
        0)
#endif
        goto fail;

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    is->audio_volume = SDL_MIX_MAXVOLUME;
    is->muted = 0;
    is->av_sync_type = ffp->av_sync_type;
    is->pause_buffering = 0;
    is->network_disconnect = 0;
    is->prepared_source = 0;
    is->reconnect_retry_count = 0;
    is->play_mutex = SDL_CreateMutex();
    ffp->is = is;
   // is->pause_req = !ffp->start_on_prepared;

    is->video_refresh_tid = SDL_CreateThreadEx(&is->_video_refresh_tid, video_refresh_thread, ffp, "ff_vout");
    if (!is->video_refresh_tid) {
        av_em_freep(&ffp->is);
        return NULL;
    }

    is->read_tid = SDL_CreateThreadEx(&is->_read_tid, read_thread, ffp, "ff_read");
    if (!is->read_tid) {
        av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
fail:
        is->abort_request = true;
        if (is->video_refresh_tid)
            SDL_WaitThread(is->video_refresh_tid, NULL);
        stream_close(ffp);
        return NULL;
    }
    return is;
}

// FFP_MERGE: stream_cycle_channel
// FFP_MERGE: toggle_full_screen
// FFP_MERGE: toggle_audio_display
// FFP_MERGE: refresh_loop_wait_event
// FFP_MERGE: event_loop
// FFP_MERGE: opt_frame_size
// FFP_MERGE: opt_width
// FFP_MERGE: opt_height
// FFP_MERGE: opt_format
// FFP_MERGE: opt_frame_pix_fmt
// FFP_MERGE: opt_sync
// FFP_MERGE: opt_seek
// FFP_MERGE: opt_duration
// FFP_MERGE: opt_show_mode
// FFP_MERGE: opt_input_file
// FFP_MERGE: opt_codec
// FFP_MERGE: dummy
// FFP_MERGE: options
// FFP_MERGE: show_usage
// FFP_MERGE: show_help_default
static int video_refresh_thread(void *arg)
{
    FFPlayer *ffp = arg;
    VideoState *is = ffp->is;
    double remaining_time = 0.0;
    while (!is->abort_request) {
        if (remaining_time > 0.0) {
            av_em_usleep((int)(int64_t)(remaining_time * 1000000.0));
        }
        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh)){
            video_refresh(ffp, &remaining_time);
        }
        
        //av_em_log(NULL, AV_LOG_FATAL, "video refresh remain %f\n", remaining_time * 1000000.0);
    }

    return 0;
}

static int lockmgr(void **mtx, enum AVLockOp op)
{
    switch (op) {
    case AV_LOCK_CREATE:
        *mtx = SDL_CreateMutex();
        if (!*mtx) {
            av_em_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
            return 1;
        }
        return 0;
    case AV_LOCK_OBTAIN:
        return !!SDL_LockMutex(*mtx);
    case AV_LOCK_RELEASE:
        return !!SDL_UnlockMutex(*mtx);
    case AV_LOCK_DESTROY:
        SDL_DestroyMutex(*mtx);
        return 0;
    }
    return 1;
}

// FFP_MERGE: main

/*****************************************************************************
 * end last line in ffplay.c
 ****************************************************************************/

static bool g_ffmpeg_global_inited = false;

inline static int log_level_av_to_ijk(int av_level)
{
    int ijk_level = IJK_LOG_VERBOSE;
    if      (av_level <= AV_LOG_PANIC)      ijk_level = IJK_LOG_FATAL;
    else if (av_level <= AV_LOG_FATAL)      ijk_level = IJK_LOG_FATAL;
    else if (av_level <= AV_LOG_ERROR)      ijk_level = IJK_LOG_ERROR;
    else if (av_level <= AV_LOG_WARNING)    ijk_level = IJK_LOG_WARN;
    else if (av_level <= AV_LOG_INFO)       ijk_level = IJK_LOG_INFO;
    // AV_LOG_VERBOSE means detailed info
    else if (av_level <= AV_LOG_VERBOSE)    ijk_level = IJK_LOG_INFO;
    else if (av_level <= AV_LOG_DEBUG)      ijk_level = IJK_LOG_DEBUG;
    else if (av_level <= AV_LOG_TRACE)      ijk_level = IJK_LOG_VERBOSE;
    else                                    ijk_level = IJK_LOG_VERBOSE;
    return ijk_level;
}

inline static int log_level_ijk_to_av(int ijk_level)
{
    int av_level = IJK_LOG_VERBOSE;
    if      (ijk_level >= IJK_LOG_SILENT)   av_level = AV_LOG_QUIET;
    else if (ijk_level >= IJK_LOG_FATAL)    av_level = AV_LOG_FATAL;
    else if (ijk_level >= IJK_LOG_ERROR)    av_level = AV_LOG_ERROR;
    else if (ijk_level >= IJK_LOG_WARN)     av_level = AV_LOG_WARNING;
    else if (ijk_level >= IJK_LOG_INFO)     av_level = AV_LOG_INFO;
    // AV_LOG_VERBOSE means detailed info
    else if (ijk_level >= IJK_LOG_DEBUG)    av_level = AV_LOG_DEBUG;
    else if (ijk_level >= IJK_LOG_VERBOSE)  av_level = AV_LOG_TRACE;
    else if (ijk_level >= IJK_LOG_DEFAULT)  av_level = AV_LOG_TRACE;
    else if (ijk_level >= IJK_LOG_UNKNOWN)  av_level = AV_LOG_TRACE;
    else                                    av_level = AV_LOG_TRACE;
    return av_level;
}

static void ffp_log_callback_brief(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > av_em_log_get_level())
        return;

    int ffplv __unused = log_level_av_to_ijk(level);
    VLOG(ffplv, IJK_LOG_TAG, fmt, vl);
}

static void ffp_log_callback_report(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > av_em_log_get_level())
        return;

    int ffplv __unused = log_level_av_to_ijk(level);

    va_list vl2;
    char line[1024];
    static int print_prefix = 1;

    va_copy(vl2, vl);
    // av_em_log_default_callback(ptr, level, fmt, vl);
    av_em_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

    ALOG(ffplv, IJK_LOG_TAG, "%s", line);
}

int ijkav_register_all(void);
void ffp_global_init()
{
    if (g_ffmpeg_global_inited)
        return;

    /* register all codecs, demux and protocols */
    avcodec_em_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_em_register_all();

    ijkav_register_all();

    avformat_em_network_init();

    av_em_lockmgr_register(lockmgr);
    av_em_log_set_callback(ffp_log_callback_brief);
    av_em_log_set_level(AV_LOG_INFO);
    av_em_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t *)&flush_pkt;

    g_ffmpeg_global_inited = true;
}

void ffp_global_uninit()
{
    if (!g_ffmpeg_global_inited)
        return;

    av_em_lockmgr_register(NULL);

    // FFP_MERGE: uninit_opts

    avformat_em_network_deinit();

    g_ffmpeg_global_inited = false;
}

void ffp_global_set_log_report(int use_report)
{
    if (use_report) {
        av_em_log_set_callback(ffp_log_callback_report);
    } else {
        av_em_log_set_callback(ffp_log_callback_brief);
    }
}
    
void ffp_set_mute_audio(FFPlayer *ffp, int onoff)
{
    if (ffp->is) {
        ffp->is->muted = onoff;
    }
}

void ffp_global_set_log_level(int log_level)
{
    int av_level = log_level_ijk_to_av(log_level);
    av_em_log_set_level(av_level);
}

static ijk_inject_callback s_inject_callback = NULL;
int inject_callback(void *opaque, int type, void *data, size_t data_size)
{
    if (s_inject_callback)
        return s_inject_callback(opaque, type, data, data_size);
    return 0;
}

void ffp_global_set_inject_callback(ijk_inject_callback cb)
{
    s_inject_callback = cb;
}

void ffp_set_video_frame_callback(FFPlayer *ffp, ijk_present_video_frame_callback cb)
{
    ffp->video_present_callback = cb;
}

void ffp_set_audio_frame_callback(FFPlayer *ffp, ijk_present_audio_frame_callback cb)
{
    ffp->audio_present_callback = cb;
}

void ffp_io_stat_register(void (*cb)(const char *url, int type, int bytes))
{
    // avijk_io_stat_register(cb);
}

void ffp_io_stat_complete_register(void (*cb)(const char *url,
                                              int64_t read_bytes, int64_t total_size,
                                              int64_t elpased_time, int64_t total_duration))
{
    // avijk_io_stat_complete_register(cb);
}

static const char *ffp_context_to_name(void *ptr)
{
    return "FFPlayer";
}


static void *ffp_context_child_next(void *obj, void *prev)
{
    return NULL;
}

static const AVEMClass *ffp_context_child_class_next(const AVEMClass *prev)
{
    return NULL;
}

const AVEMClass ffp_context_class = {
    .class_name       = "FFPlayer",
    .item_name        = ffp_context_to_name,
    .option           = ffp_context_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = ffp_context_child_next,
    .child_class_next = ffp_context_child_class_next,
};

static int ffp_prepare_video_source_thread(void *arg)
{
    FFPlayer *ffp = (FFPlayer *)arg;
    int i = 0;int ret = 0;
    ffplay_format_t *avformat = NULL;
    AVEMFormatContext *ic = NULL;
    for (;;) {
        for (i = 0; i < FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE; i++) {
            SDL_LockMutex(ffp->prepared_lock);
            ic = ffp->prepared_source[i].ic;
            if (ic == FFP_PREPARE_VIDEO_SOURCE_FLAG) {
                ffp->prepared_source[i].ic = FFP_PREPARING_VIDEO_SOURCE_FLAG;
            }
            SDL_UnlockMutex(ffp->prepared_lock);
            if (ic == FFP_PREPARE_VIDEO_SOURCE_FLAG) {
                avformat = NULL;
                ret = prepare_source_internal(ffp, NULL, ffp->prepared_source[i].filename, ffp->prepared_source[i].fileType, NULL, &avformat);
                SDL_LockMutex(ffp->prepared_lock);
                if (ret < 0 || avformat == NULL) {
                    av_em_log(NULL, AV_LOG_ERROR, "prepare this video source failed.\n");
                    if (ffp->prepared_source[i].play_after_prepared) {
                        ffp_notify_msg2(ffp, FFP_MSG_ERROR_CONNECT_FAILD, ret);
                        ffp->prepared_source[i].play_after_prepared = 0;
                    }
                    ffp->prepared_source[i].ic = FFP_PREPARE_VIDEO_SOURCE_FLAG;
                    SDL_UnlockMutex(ffp->prepared_lock);
                    continue;
                }
                if (ffp->prepared_source[i].ic != FFP_PREPARING_VIDEO_SOURCE_FLAG) {
                    av_em_log(NULL, AV_LOG_INFO, "this index is delete now.\n");
                    if (ffp->prepared_source[i].ic != NULL) {
                        av_em_log(NULL, AV_LOG_ERROR, "fatal error!!! ic is modify by unknown condition, ic:%p.\n", ffp->prepared_source[i].ic);
                    }
                    avformat_em_close_input(&avformat->ic);
                    av_em_free(avformat);
                    SDL_UnlockMutex(ffp->prepared_lock);
                    continue;
                }
                avformat->fileType = ffp->prepared_source[i].fileType;
                if (ffp->prepared_source[i].play_after_prepared) {
                    av_em_log(NULL, AV_LOG_INFO, "%s:play after prepared, index:%d.\n", __func__, i);
                    ffp->prepared_source[i].play_after_prepared = 0;
                    ffp->prepared_source[i].ic = avformat->ic;
                    //ffp->prepared_source[i].ic = NULL;
                    //ffp->prepared_source_count--;
                    SDL_LockMutex(ffp->change_source_lock);
                    ffp->cur_format = avformat;
                    ffp->b_change_source = 1;
                    SDL_UnlockMutex(ffp->change_source_lock);
                } else {
                    memcpy(&ffp->prepared_source[i], avformat, sizeof(ffplay_format_t));
                    av_em_free(avformat);
                }
                SDL_UnlockMutex(ffp->prepared_lock);
            }
        }
        SDL_LockMutex(ffp->prepared_lock);
        if (ffp->prepare_abort) {
            SDL_UnlockMutex(ffp->prepared_lock);
            break;
        }
        SDL_CondWait(ffp->prepare_cond, ffp->prepared_lock);
        if (ffp->prepare_abort) {
            SDL_UnlockMutex(ffp->prepared_lock);
            break;
        }
        SDL_UnlockMutex(ffp->prepared_lock);
    }
    av_em_log(NULL, AV_LOG_INFO, "exit thread:%s.\n", __func__);
    return 0;
}

FFPlayer *ffp_create()
{
    av_em_log(NULL, AV_LOG_INFO, "av_em_version_info: %s\n", av_em_version_info());
    FFPlayer* ffp = (FFPlayer*) av_em_mallocz(sizeof(FFPlayer));
    if (!ffp)
        return NULL;

    msg_queue_init(&ffp->msg_queue);
    ffp->af_mutex = SDL_CreateMutex();
    ffp->vf_mutex = SDL_CreateMutex();

    ffp_reset_internal(ffp);
    ffp->av_class = &ffp_context_class;
    ffp->meta = ijkmeta_create();
    ffp->play_mode = FFP_PLAY_MODE_VOD_FLV;
    av_em_opt_set_defaults(ffp);

    ffp->prepared_source_count = 0;
    memset(ffp->prepared_source, 0, FFP_PLAY_MAX_PREPARED_VIDEO_SOURCE * sizeof(ffplay_format_t));
    ffp->prepared_lock = SDL_CreateMutex();
    ffp->change_source_lock = SDL_CreateMutex();
    ffp->reconfigure_mutex = SDL_CreateMutex();
    ffp->reconfigure_cond = SDL_CreateCond();
    ffp->prepare_cond = SDL_CreateCond();
    ffp->prepare_thread = SDL_CreateThreadEx(&ffp->_prepare_thread, ffp_prepare_video_source_thread, ffp, "prepared thread");
    return ffp;
}

void  ffp_set_play_mode(FFPlayer *ffp, const int mode)
{
    ffp->play_mode = mode;
}
    
void ffp_set_play_channel(FFPlayer *ffp, int channel_mode)
{
    ffp->play_channel_mode = channel_mode;
}

void  ffp_set_record_status(FFPlayer *ffp, const int record_status)
{
    ffp->is_recording = record_status;
}

void ffp_destroy(FFPlayer *ffp)
{
    av_em_log(NULL, AV_LOG_INFO, "ffp_destroy_ffplayer");
    if (!ffp)
        return;

    if (ffp->is) {
        av_em_log(NULL, AV_LOG_WARNING, "ffp_destroy_ffplayer: force stream_close()");
        stream_close(ffp);
        ffp->is = NULL;
    }
    SDL_VoutFreeP(&ffp->vout);
    SDL_AoutFreeP(&ffp->aout);
    ffpipenode_free_p(&ffp->node_vdec);
    ffpipeline_free_p(&ffp->pipeline);
    ijkmeta_destroy_p(&ffp->meta);
    ffsonic_free_p(ffp->sonic_handle);
    SDL_LockMutex(ffp->prepared_lock);
    ffp->prepare_abort = 1;
    SDL_UnlockMutex(ffp->prepared_lock);
    SDL_CondSignal(ffp->prepare_cond);
    SDL_WaitThread(ffp->prepare_thread, NULL);
    SDL_DestroyCondP(&ffp->prepare_cond);
    SDL_DestroyMutexP(&ffp->prepared_lock);
    for (int i = 0; i < ffp->prepared_source_count; i++) {
        if (ffp->prepared_source[i].ic && ffp->prepared_source[i].ic != FFP_PREPARE_VIDEO_SOURCE_FLAG && ffp->prepared_source[i].ic != FFP_PREPARING_VIDEO_SOURCE_FLAG) {
            avformat_em_close_input(&ffp->prepared_source[i].ic);
            ffp->prepared_source[i].ic = NULL;
        }
    }
    ffp_reset_internal(ffp);
    SDL_DestroyMutexP(&ffp->af_mutex);
    SDL_DestroyMutexP(&ffp->vf_mutex);
    msg_queue_destroy(&ffp->msg_queue);
    SDL_DestroyMutexP(&ffp->change_source_lock);
    SDL_DestroyMutexP(&ffp->reconfigure_mutex);
    SDL_DestroyCondP(&ffp->reconfigure_cond);
    av_em_free(ffp);
    av_em_log(NULL, AV_LOG_INFO, "ffp_destroy_success");
}

void ffp_destroy_p(FFPlayer **pffp)
{
    if (!pffp)
        return;

    ffp_destroy(*pffp);
    *pffp = NULL;
}

static AVEMDictionary **ffp_get_opt_dict(FFPlayer *ffp, int opt_category)
{
    assert(ffp);

    switch (opt_category) {
        case FFP_OPT_CATEGORY_FORMAT:   return &ffp->format_opts;
        case FFP_OPT_CATEGORY_CODEC:    return &ffp->codec_opts;
        case FFP_OPT_CATEGORY_SWS:      return &ffp->sws_dict;
        case FFP_OPT_CATEGORY_PLAYER:   return &ffp->player_opts;
        case FFP_OPT_CATEGORY_SWR:      return &ffp->swr_opts;
        default:
            av_em_log(ffp, AV_LOG_ERROR, "unknown option category %d\n", opt_category);
            return NULL;
    }
}

static void ffp_set_playback_async_statistic(FFPlayer *ffp, int64_t buf_backwards, int64_t buf_forwards, int64_t buf_capacity);
static int app_func_event(AVApplicationContext *h, int message ,void *data, size_t size)
{
    if (!h || !h->opaque || !data)
        return 0;

    FFPlayer *ffp = (FFPlayer *)h->opaque;
    if (!ffp->inject_opaque)
        return 0;
    if (message == AVAPP_EVENT_IO_TRAFFIC && sizeof(AVAppIOTraffic) == size) {
        AVAppIOTraffic *event = (AVAppIOTraffic *)(intptr_t)data;
        if (event->bytes > 0)
            SDL_SpeedSampler2Add(&ffp->stat.tcp_read_sampler, event->bytes);
    } else if (message == AVAPP_EVENT_ASYNC_STATISTIC && sizeof(AVAppAsyncStatistic) == size) {
        AVAppAsyncStatistic *statistic =  (AVAppAsyncStatistic *) (intptr_t)data;
        ffp->stat.buf_backwards = statistic->buf_backwards;
        ffp->stat.buf_forwards = statistic->buf_forwards;
        ffp->stat.buf_capacity = statistic->buf_capacity;
    }
    return inject_callback(ffp->inject_opaque, message , data, size);
}

void *ffp_set_inject_opaque(FFPlayer *ffp, void *opaque)
{
    if (!ffp)
        return NULL;

    void *previous_opaque = ffp->inject_opaque;
    
    ffp->inject_opaque = opaque;
    if (opaque) {
        av_em_application_closep(&ffp->app_ctx);
        av_em_application_open(&ffp->app_ctx, ffp);
        ffp_set_option_int(ffp, FFP_OPT_CATEGORY_FORMAT, "ijkapplication", (int64_t)(intptr_t)ffp->app_ctx);

        ffp->app_ctx->func_on_app_event = app_func_event;
    }
    return previous_opaque;
}

void ffp_set_option(FFPlayer *ffp, int opt_category, const char *name, const char *value)
{
    if (!ffp)
        return;

    AVEMDictionary **dict = ffp_get_opt_dict(ffp, opt_category);
    av_em_dict_set(dict, name, value, 0);
}

void ffp_set_option_int(FFPlayer *ffp, int opt_category, const char *name, int64_t value)
{
    if (!ffp)
        return;

    AVEMDictionary **dict = ffp_get_opt_dict(ffp, opt_category);
    av_em_dict_set_int(dict, name, value, 0);
}

void ffp_set_overlay_format(FFPlayer *ffp, int chroma_fourcc)
{
    switch (chroma_fourcc) {
        case SDL_FCC__GLES2:
        case SDL_FCC_I420:
        case SDL_FCC_YV12:
        case SDL_FCC_RV16:
        case SDL_FCC_RV24:
        case SDL_FCC_RV32:
            ffp->overlay_format = chroma_fourcc;
            break;
#ifdef __APPLE__
        case SDL_FCC_I444P10LE:
            ffp->overlay_format = chroma_fourcc;
            break;
#endif
        default:
            av_em_log(ffp, AV_LOG_ERROR, "ffp_set_overlay_format: unknown chroma fourcc: %d\n", chroma_fourcc);
            break;
    }
}

int ffp_get_video_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!codec_info)
        return -1;

    // FIXME: not thread-safe
    if (ffp->video_codec_info) {
        *codec_info = strdup(ffp->video_codec_info);
    } else {
        *codec_info = NULL;
    }
    return 0;
}

int ffp_get_audio_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!codec_info)
        return -1;

    // FIXME: not thread-safe
    if (ffp->audio_codec_info) {
        *codec_info = strdup(ffp->audio_codec_info);
    } else {
        *codec_info = NULL;
    }
    return 0;
}

static void ffp_show_dict(FFPlayer *ffp, const char *tag, AVEMDictionary *dict)
{
    AVEMDictionaryEntry *t = NULL;

    while ((t = av_em_dict_get(dict, "", t, AV_DICT_IGNORE_SUFFIX))) {
        av_em_log(ffp, AV_LOG_INFO, "%-*s: %-*s = %s\n", 12, tag, 28, t->key, t->value);
    }
}

#define FFP_VERSION_MODULE_NAME_LENGTH 13
static void ffp_show_version_str(FFPlayer *ffp, const char *module, const char *version)
{
        av_em_log(ffp, AV_LOG_INFO, "%-*s: %s\n", FFP_VERSION_MODULE_NAME_LENGTH, module, version);
}

static void ffp_show_version_int(FFPlayer *ffp, const char *module, unsigned version)
{
    av_em_log(ffp, AV_LOG_INFO, "%-*s: %u.%u.%u\n",
           FFP_VERSION_MODULE_NAME_LENGTH, module,
           (unsigned int)IJKVERSION_GET_MAJOR(version),
           (unsigned int)IJKVERSION_GET_MINOR(version),
           (unsigned int)IJKVERSION_GET_MICRO(version));
}

int ffp_prepare_async_l(FFPlayer *ffp, const char *file_name)
{
    assert(ffp);
    assert(!ffp->is);
    assert(file_name);
    ffp->prepared_timems = ijk_get_timems();

    /* there is a length limit in avformat */

    av_em_log(NULL, AV_LOG_INFO, "===== versions =====\n");
    ffp_show_version_str(ffp, "FFmpeg",         av_em_version_info());
    ffp_show_version_int(ffp, "libavutil",      avutil_em_version());
    ffp_show_version_int(ffp, "libavcodec",     avcodec_em_version());
    ffp_show_version_int(ffp, "libavformat",    avformat_em_version());
    ffp_show_version_int(ffp, "libswscale",     em_swscale_version());
    ffp_show_version_int(ffp, "libswresample",  em_swresample_version());
    av_em_log(NULL, AV_LOG_INFO, "===== options =====\n");
    ffp_show_dict(ffp, "player-opts", ffp->player_opts);
    ffp_show_dict(ffp, "format-opts", ffp->format_opts);
    ffp_show_dict(ffp, "codec-opts ", ffp->codec_opts);
    ffp_show_dict(ffp, "sws-opts   ", ffp->sws_dict);
    ffp_show_dict(ffp, "swr-opts   ", ffp->swr_opts);
    av_em_log(NULL, AV_LOG_INFO, "===================\n");

    av_em_opt_set_dict(ffp, &ffp->player_opts);
    if (!ffp->aout) {
        ffp->aout = ffpipeline_open_audio_output(ffp->pipeline, ffp);
        if (!ffp->aout)
            return -1;
        ffp->aout->caller_opaque = ffp->vout->callerOpaque;
    }

#if CONFIG_AVFILTER
    if (ffp->vfilter0) {
        GROW_ARRAY(ffp->vfilters_list, ffp->nb_vfilters);
        ffp->vfilters_list[ffp->nb_vfilters - 1] = ffp->vfilter0;
    }
#endif
    av_em_log(NULL, AV_LOG_INFO, "before stream open takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
    VideoState *is = stream_open(ffp, file_name, NULL);
    av_em_log(NULL, AV_LOG_INFO, "after stream open takes time:%lld.\n", ijk_get_timems() - ffp->prepared_timems);
    if (!is) {
        av_em_log(NULL, AV_LOG_WARNING, "ffp_prepare_async_l: stream_open failed OOM");
        return EIJK_OUT_OF_MEMORY;
    }

    ffp->is = is;
    ffp->input_filename = av_em_strdup(file_name);
    return 0;
}

int ffp_start_from_offset(FFPlayer *ffp, int64_t offset)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;
            
    ffp->auto_resume = 1;
    ffp_toggle_buffering(ffp, 1);
    ffp_seek_to_offset(ffp, offset);
    return 0;
}

int ffp_start_from_l(FFPlayer *ffp, long msec)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    ffp->auto_resume = 1;
    ffp_toggle_buffering(ffp, 1);
    ffp_seek_to_l(ffp, msec);
    return 0;
}

int ffp_start_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    toggle_pause(ffp, 0);
    return 0;
}

int ffp_pause_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    toggle_pause(ffp, 1);
    return 0;
}

int ffp_standby_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;
    toggle_standby(ffp, 1);
    return 0;
}

int ffp_is_paused_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return 1;

    return is->paused;
}

int ffp_stop_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (is) {
        is->abort_request = 1;
        msg_queue_abort(&ffp->msg_queue);
    }

    ffp->prepare_source_abort = 1;
    return 0;
}

int ffp_wait_stop_l(FFPlayer *ffp)
{
    assert(ffp);

    if (ffp->is) {
        ffp_stop_l(ffp);
        stream_close(ffp);
        ffp->is = NULL;
    }
    return 0;
}

int ffp_seek_to_l(FFPlayer *ffp, long msec)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;

    int64_t seek_pos = milliseconds_to_fftime(msec);
    int64_t start_time = is->ic->start_time;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        seek_pos += start_time;

    // FIXME: 9 seek by bytes
    // FIXME: 9 seek out of range
    // FIXME: 9 seekable
    av_em_log(ffp, AV_LOG_DEBUG, "stream_seek %"PRId64"(%d) + %"PRId64", \n", seek_pos, (int)msec, start_time);
    stream_seek(is, seek_pos, 0, 0);
    ffp_toggle_buffering(ffp, 1);
    ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);
    return 0;
}

int  ffp_seek_to_offset(FFPlayer *ffp, int64_t offset)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is)
        return EIJK_NULL_IS_PTR;
    
    
    // FIXME: 9 seek by bytes
    // FIXME: 9 seek out of range
    // FIXME: 9 seekable
    av_em_log(ffp, AV_LOG_DEBUG, "stream_seek to offset:%lld\n", offset);
    stream_seek(is, offset, 0, 1);
    ffp_toggle_buffering(ffp, 1);
    ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);
    return 0;
                
}

long ffp_get_current_position_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is || !is->ic)
        return 0;

    int64_t start_time = is->ic->start_time;
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        start_diff = fftime_to_milliseconds(start_time);

    int64_t pos = 0;
    double pos_clock = get_master_clock(is);
    if (isnan(pos_clock)) {
        pos = fftime_to_milliseconds(is->seek_pos);
    } else {
        pos = pos_clock * 1000;
    }

    // If using REAL time and not ajusted, then return the real pos as calculated from the stream
    // the use case for this is primarily when using a custom non-seekable data source that starts
    // with a buffer that is NOT the start of the stream.  We want the get_current_position to
    // return the time in the stream, and not the player's internal clock.
    if (ffp->no_time_adjust) {
        return (long)pos;
    }

    if (pos < 0 || pos < start_diff)
        return 0;

    int64_t adjust_pos = pos - start_diff;
    return (long)adjust_pos;
}

long ffp_get_duration_l(FFPlayer *ffp)
{
    assert(ffp);
    VideoState *is = ffp->is;
    if (!is || !is->ic)
        return 0;

    int64_t duration = fftime_to_milliseconds(is->ic->duration);
    if (duration < 0)
        return 0;

    return (long)duration;
}

long ffp_get_playable_duration_l(FFPlayer *ffp)
{
    assert(ffp);
    if (!ffp)
        return 0;

    return (long)ffp->playable_duration_ms;
}

void ffp_set_loop(FFPlayer *ffp, int loop)
{
    assert(ffp);
    if (!ffp)
        return;
    ffp->loop = loop;
}

int ffp_get_loop(FFPlayer *ffp)
{
    assert(ffp);
    if (!ffp)
        return 1;
    return ffp->loop;
}

int ffp_packet_queue_init(PacketQueue *q)
{
    return packet_queue_init(q);
}

void ffp_packet_queue_destroy(PacketQueue *q)
{
    return packet_queue_destroy(q);
}

void ffp_packet_queue_abort(PacketQueue *q)
{
    return packet_queue_abort(q);
}

void ffp_packet_queue_start(PacketQueue *q)
{
    return packet_queue_start(q);
}

void ffp_packet_queue_flush(PacketQueue *q)
{
    return packet_queue_flush(q);
}

int ffp_packet_queue_get(PacketQueue *q, AVEMPacket *pkt, int block, int *serial)
{
    return packet_queue_get(q, pkt, block, serial);
}

int ffp_packet_queue_get_or_buffering(FFPlayer *ffp, PacketQueue *q, AVEMPacket *pkt, int *serial, int *finished)
{
    return packet_queue_get_or_buffering(ffp, q, pkt, serial, finished);
}

int ffp_packet_queue_put(PacketQueue *q, AVEMPacket *pkt)
{
    return packet_queue_put(q, pkt);
}

bool ffp_is_flush_packet(AVEMPacket *pkt)
{
    if (!pkt)
        return false;

    return pkt->data == flush_pkt.data;
}

Frame *ffp_frame_queue_peek_writable(FrameQueue *f)
{
    return frame_queue_peek_writable(f);
}

void ffp_frame_queue_empty(FrameQueue *f)
{
    return frame_queue_empty(f);
}
    
void ffp_frame_queue_push(FrameQueue *f)
{
    return frame_queue_push(f);
}

int ffp_queue_picture(FFPlayer *ffp, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    return queue_picture(ffp, src_frame, pts, duration, pos, serial);
}

int ffp_get_master_sync_type(VideoState *is)
{
    return get_master_sync_type(is);
}

double ffp_get_master_clock(VideoState *is)
{
    return get_master_clock(is);
}

void ffp_toggle_buffering_l(FFPlayer *ffp, int buffering_on)
{
    if (!ffp->packet_buffering)
        return;

    VideoState *is = ffp->is;
    if (buffering_on && !is->buffering_on) {
        av_em_log(ffp, AV_LOG_DEBUG, "ffp_toggle_buffering_l: start\n");
        is->buffering_start_ms = ijk_get_timems();
        is->buffering_on = 1;
        stream_update_pause_l(ffp);
        ffp_notify_msg1(ffp, FFP_MSG_BUFFERING_START);
        /*if (ffp->error && ffp->error == AVERROR(ETIMEDOUT)) {
            ffp->last_readpos = ffp->last_readpos = (int)ffp_get_current_position_l(ffp);//(int)ic->pb->pos;
            //ffp_notify_msg2(ffp, FFP_MSG_ERROR_NET_DISCONNECT, ffp->last_readpos);
            ffp_set_network_disconnect(ffp);
        }*/
    } else if (!buffering_on && is->buffering_on){
        if (is_ffp_in_live_mode(ffp) && is->buffering_start_ms != -1 && ijk_get_timems() - is->buffering_start_ms > 1000) {
            ffp_change_video_source(ffp, ffp->input_filename, ffp->play_mode);
            return;
        }
        av_em_log(ffp, AV_LOG_WARNING, "ffp_toggle_buffering_l: end\n");
        is->buffering_on = 0;
        stream_update_pause_l(ffp);
        ffp_notify_msg1(ffp, FFP_MSG_BUFFERING_END);
    }
}

void ffp_toggle_buffering(FFPlayer *ffp, int start_buffering)
{
    SDL_LockMutex(ffp->is->play_mutex);
    ffp_toggle_buffering_l(ffp, start_buffering);
    SDL_UnlockMutex(ffp->is->play_mutex);
}

void ffp_track_statistic_l(FFPlayer *ffp, AVEMStream *st, PacketQueue *q, FFTrackCacheStatistic *cache)
{
    assert(cache);

    if (q) {
        cache->bytes   = q->size;
        cache->packets = q->nb_packets;
    }

    if (st && st->time_base.den > 0 && st->time_base.num > 0) {
        cache->duration = q->duration * av_em_q2d(st->time_base) * 1000;
    }
}

void ffp_audio_statistic_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    ffp_track_statistic_l(ffp, is->audio_st, &is->audioq, &ffp->stat.audio_cache);
}

void ffp_video_statistic_l(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    ffp_track_statistic_l(ffp, is->video_st, &is->videoq, &ffp->stat.video_cache);
}

void ffp_statistic_l(FFPlayer *ffp)
{
    ffp_audio_statistic_l(ffp);
    ffp_video_statistic_l(ffp);
}

void ffp_check_buffering_l(FFPlayer *ffp)
{
    VideoState *is            = ffp->is;
    if (is->buffer_indicator_queue && is->buffer_indicator_queue->nb_packets > 0) {
        if (   (is->audioq.nb_packets > MIN_MIN_FRAMES || is->audio_stream < 0 || is->audioq.abort_request)
               && (is->videoq.nb_packets > MIN_MIN_FRAMES || is->video_stream < 0 || is->videoq.abort_request)) {
            ffp_toggle_buffering(ffp, 0);
        }
    }
}

int ffp_video_thread(FFPlayer *ffp)
{
    return ffplay_video_thread(ffp);
}

void ffp_set_video_codec_info(FFPlayer *ffp, const char *module, const char *codec)
{
    av_em_freep(&ffp->video_codec_info);
    ffp->video_codec_info = av_em_asprintf("%s, %s", module ? module : "", codec ? codec : "");
    av_em_log(ffp, AV_LOG_INFO, "VideoCodec: %s\n", ffp->video_codec_info);
}

void ffp_set_audio_codec_info(FFPlayer *ffp, const char *module, const char *codec)
{
    av_em_freep(&ffp->audio_codec_info);
    ffp->audio_codec_info = av_em_asprintf("%s, %s", module ? module : "", codec ? codec : "");
    av_em_log(ffp, AV_LOG_INFO, "AudioCodec: %s\n", ffp->audio_codec_info);
}

void ffp_set_playback_rate(FFPlayer *ffp, float rate)
{
    if (!ffp)
        return;

    ffp->pf_playback_rate = rate;
    ffp->pf_playback_rate_changed = 1;
}

int ffp_get_video_rotate_degrees(FFPlayer *ffp)
{
    VideoState *is = ffp->is;
    if (!is)
        return 0;

    int theta  = abs((int)((int64_t)round(fabs(get_rotation(is->video_st))) % 360));
    switch (theta) {
        case 0:
        case 90:
        case 180:
        case 270:
            break;
        case 360:
            theta = 0;
            break;
        default:
            ALOGW("Unknown rotate degress: %d\n", theta);
            theta = 0;
            break;
    }

    return theta;
}

int ffp_set_stream_selected(FFPlayer *ffp, int stream, int selected)
{
    VideoState        *is = ffp->is;
    AVEMFormatContext   *ic = NULL;
    AVEMCodecParameters *codecpar = NULL;
    if (!is)
        return -1;
    ic = is->ic;
    if (!ic)
        return -1;

    if (stream < 0 || stream >= ic->nb_streams) {
        av_em_log(ffp, AV_LOG_ERROR, "invalid stream index %d >= stream number (%d)\n", stream, ic->nb_streams);
        return -1;
    }

    codecpar = ic->streams[stream]->codecpar;

    if (selected) {
        switch (codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                if (stream != is->video_stream && is->video_stream >= 0)
                    stream_component_close(ffp, is->video_stream);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (stream != is->audio_stream && is->audio_stream >= 0)
                    stream_component_close(ffp, is->audio_stream);
                break;
            default:
                av_em_log(ffp, AV_LOG_ERROR, "select invalid stream %d of video type %d\n", stream, codecpar->codec_type);
                return -1;
        }
        return stream_component_open(ffp, stream);
    } else {
        switch (codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                if (stream == is->video_stream)
                    stream_component_close(ffp, is->video_stream);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (stream == is->audio_stream)
                    stream_component_close(ffp, is->audio_stream);
                break;
            default:
                av_em_log(ffp, AV_LOG_ERROR, "select invalid stream %d of audio type %d\n", stream, codecpar->codec_type);
                return -1;
        }
        return 0;
    }
}

float ffp_get_property_float(FFPlayer *ffp, int id, float default_value)
{
    switch (id) {
        case FFP_PROP_FLOAT_VIDEO_DECODE_FRAMES_PER_SECOND:
            return ffp ? ffp->stat.vdps : default_value;
        case FFP_PROP_FLOAT_VIDEO_OUTPUT_FRAMES_PER_SECOND:
            return ffp ? ffp->stat.vfps : default_value;
        case FFP_PROP_FLOAT_PLAYBACK_RATE:
            return ffp ? ffp->pf_playback_rate : default_value;
        case FFP_PROP_FLOAT_AVDELAY:
            return ffp ? ffp->stat.avdelay : default_value;
        case FFP_PROP_FLOAT_AVDIFF:
            return ffp ? ffp->stat.avdiff : default_value;
        default:
            return default_value;
    }
}

void ffp_set_property_float(FFPlayer *ffp, int id, float value)
{
    switch (id) {
        case FFP_PROP_FLOAT_PLAYBACK_RATE:
            ffp_set_playback_rate(ffp, value);
        default:
            return;
    }
}

int64_t ffp_get_property_int64(FFPlayer *ffp, int id, int64_t default_value)
{
    switch (id) {
        case FFP_PROP_INT64_SELECTED_VIDEO_STREAM:
            if (!ffp || !ffp->is)
                return default_value;
            return ffp->is->video_stream;
        case FFP_PROP_INT64_SELECTED_AUDIO_STREAM:
            if (!ffp || !ffp->is)
                return default_value;
            return ffp->is->audio_stream;
        case FFP_PROP_INT64_VIDEO_DECODER:
            if (!ffp)
                return default_value;
            return ffp->stat.vdec_type;
        case FFP_PROP_INT64_AUDIO_DECODER:
            return FFP_PROPV_DECODER_AVCODEC;

        case FFP_PROP_INT64_VIDEO_CACHED_DURATION:
            if (!ffp)
                return default_value;
            return ffp->stat.video_cache.duration;
        case FFP_PROP_INT64_AUDIO_CACHED_DURATION:
            if (!ffp)
                return default_value;
            return ffp->stat.audio_cache.duration;
        case FFP_PROP_INT64_VIDEO_CACHED_BYTES:
            if (!ffp)
                return default_value;
            return ffp->stat.video_cache.bytes;
        case FFP_PROP_INT64_AUDIO_CACHED_BYTES:
            if (!ffp)
                return default_value;
            return ffp->stat.audio_cache.bytes;
        case FFP_PROP_INT64_VIDEO_CACHED_PACKETS:
            if (!ffp)
                return default_value;
            return ffp->stat.video_cache.packets;
        case FFP_PROP_INT64_AUDIO_CACHED_PACKETS:
            if (!ffp)
                return default_value;
            return ffp->stat.audio_cache.packets;
        case FFP_PROP_INT64_BIT_RATE:
            return ffp ? ffp->stat.bit_rate : default_value;
        case FFP_PROP_INT64_TCP_SPEED:
            return ffp ? SDL_SpeedSampler2GetSpeed(&ffp->stat.tcp_read_sampler) : default_value;
        case FFP_PROP_INT64_VIDEO_BITRATE:
            return ffp ? SDL_SpeedSampler3GetSpeed(&ffp->stat.video_bitrate_sampler) : default_value;
        case FFP_PROP_INT64_AUDIO_BITRATE:
            return ffp ? SDL_SpeedSampler3GetSpeed(&ffp->stat.audio_bitrate_sampler) : default_value;
        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_BACKWARDS:
            if (!ffp)
                return default_value;
            return ffp->stat.buf_backwards;
        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_FORWARDS:
            if (!ffp)
                return default_value;
            return ffp->stat.buf_forwards;
        case FFP_PROP_INT64_ASYNC_STATISTIC_BUF_CAPACITY:
            if (!ffp)
                return default_value;
            return ffp->stat.buf_capacity;
        case FFP_PROP_INT64_LATEST_SEEK_LOAD_DURATION:
            return ffp ? ffp->stat.latest_seek_load_duration : default_value;
        default:
            return default_value;
    }
}

void ffp_set_property_int64(FFPlayer *ffp, int id, int64_t value)
{
    switch (id) {
        // case FFP_PROP_INT64_SELECTED_VIDEO_STREAM:
        // case FFP_PROP_INT64_SELECTED_AUDIO_STREAM:
        default:
            break;
    }
}

IjkMediaMeta *ffp_get_meta_l(FFPlayer *ffp)
{
    if (!ffp)
        return NULL;

    return ffp->meta;
}
