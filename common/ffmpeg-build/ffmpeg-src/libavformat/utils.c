/*
 * various utility functions for use within FFmpeg
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdarg.h>
#include <stdint.h>

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/time_internal.h"
#include "libavutil/timestamp.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/internal.h"
#include "libavcodec/raw.h"

#include "audiointerleave.h"
#include "avformat.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "internal.h"
#include "metadata.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "riff.h"
#include "url.h"

#include "libavutil/ffversion.h"
const char av_em_format_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

/**
 * @file
 * various utility functions for use within FFmpeg
 */

unsigned avformat_em_version(void)
{
    av_assert0(LIBAVFORMAT_VERSION_MICRO >= 100);
    return LIBAVFORMAT_VERSION_INT;
}

const char *avformat_em_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avformat_em_license(void)
{
#define LICENSE_PREFIX "libavformat license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

#define RELATIVE_TS_BASE (INT64_MAX - (1LL<<48))

static int is_relative(int64_t ts) {
    return ts > (RELATIVE_TS_BASE - (1LL<<48));
}

/**
 * Wrap a given time stamp, if there is an indication for an overflow
 *
 * @param st stream
 * @param timestamp the time stamp to wrap
 * @return resulting time stamp
 */
static int64_t wrap_timestamp(AVEMStream *st, int64_t timestamp)
{
    if (st->pts_wrap_behavior != AV_PTS_WRAP_IGNORE &&
        st->pts_wrap_reference != AV_NOPTS_VALUE && timestamp != AV_NOPTS_VALUE) {
        if (st->pts_wrap_behavior == AV_PTS_WRAP_ADD_OFFSET &&
            timestamp < st->pts_wrap_reference)
            return timestamp + (1ULL << st->pts_wrap_bits);
        else if (st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET &&
            timestamp >= st->pts_wrap_reference)
            return timestamp - (1ULL << st->pts_wrap_bits);
    }
    return timestamp;
}

MAKE_ACCESSORS(AVEMStream, stream, AVEMRational, r_frame_rate)
MAKE_ACCESSORS(AVEMStream, stream, char *, recommended_encoder_configuration)
MAKE_ACCESSORS(AVEMFormatContext, format, AVEMCodec *, video_codec)
MAKE_ACCESSORS(AVEMFormatContext, format, AVEMCodec *, audio_codec)
MAKE_ACCESSORS(AVEMFormatContext, format, AVEMCodec *, subtitle_codec)
MAKE_ACCESSORS(AVEMFormatContext, format, AVEMCodec *, data_codec)
MAKE_ACCESSORS(AVEMFormatContext, format, int, metadata_header_padding)
MAKE_ACCESSORS(AVEMFormatContext, format, void *, opaque)
MAKE_ACCESSORS(AVEMFormatContext, format, av_format_control_message, control_message_cb)
#if FF_API_OLD_OPEN_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
MAKE_ACCESSORS(AVEMFormatContext, format, AVOpenCallback, open_cb)
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int64_t av_em_stream_get_end_pts(const AVEMStream *st)
{
    if (st->priv_pts) {
        return st->priv_pts->val;
    } else
        return AV_NOPTS_VALUE;
}

struct AVEMCodecParserContext *av_em_stream_get_parser(const AVEMStream *st)
{
    return st->parser;
}

void av_em_format_inject_global_side_data(AVEMFormatContext *s)
{
    int i;
    s->internal->inject_global_side_data = 1;
    for (i = 0; i < s->nb_streams; i++) {
        AVEMStream *st = s->streams[i];
        st->inject_global_side_data = 1;
    }
}

int em_copy_whiteblacklists(AVEMFormatContext *dst, AVEMFormatContext *src)
{
    av_assert0(!dst->codec_whitelist &&
               !dst->format_whitelist &&
               !dst->protocol_whitelist &&
               !dst->protocol_blacklist);
    dst-> codec_whitelist = av_em_strdup(src->codec_whitelist);
    dst->format_whitelist = av_em_strdup(src->format_whitelist);
    dst->protocol_whitelist = av_em_strdup(src->protocol_whitelist);
    dst->protocol_blacklist = av_em_strdup(src->protocol_blacklist);
    if (   (src-> codec_whitelist && !dst-> codec_whitelist)
        || (src->  format_whitelist && !dst->  format_whitelist)
        || (src->protocol_whitelist && !dst->protocol_whitelist)
        || (src->protocol_blacklist && !dst->protocol_blacklist)) {
        av_em_log(dst, AV_LOG_ERROR, "Failed to duplicate black/whitelist\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

static const AVEMCodec *find_decoder(AVEMFormatContext *s, AVEMStream *st, enum AVEMCodecID codec_id)
{
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    if (st->codec->codec)
        return st->codec->codec;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (s->video_codec)    return s->video_codec;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (s->audio_codec)    return s->audio_codec;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (s->subtitle_codec) return s->subtitle_codec;
        break;
    }

    return avcodec_em_find_decoder(codec_id);
}

int av_em_format_get_probe_score(const AVEMFormatContext *s)
{
    return s->probe_score;
}

/* an arbitrarily chosen "sane" max packet size -- 50M */
#define SANE_CHUNK_SIZE (50000000)

int emio_limit(AVEMIOContext *s, int size)
{
    if (s->maxsize>= 0) {
        int64_t remaining= s->maxsize - avio_em_tell(s);
        if (remaining < size) {
            int64_t newsize = avio_em_size(s);
            if (!s->maxsize || s->maxsize<newsize)
                s->maxsize = newsize - !newsize;
            remaining= s->maxsize - avio_em_tell(s);
            remaining= FFMAX(remaining, 0);
        }

        if (s->maxsize>= 0 && remaining+1 < size) {
            av_em_log(NULL, remaining ? AV_LOG_ERROR : AV_LOG_DEBUG, "Truncating packet of size %d to %"PRId64"\n", size, remaining+1);
            size = remaining+1;
        }
    }
    return size;
}

/* Read the data in sane-sized chunks and append to pkt.
 * Return the number of bytes read or an error. */
static int append_packet_chunked(AVEMIOContext *s, AVEMPacket *pkt, int size)
{
    int64_t orig_pos   = pkt->pos; // av_em_grow_packet might reset pos
    int orig_size      = pkt->size;
    int ret;

    do {
        int prev_size = pkt->size;
        int read_size;

        /* When the caller requests a lot of data, limit it to the amount
         * left in file or SANE_CHUNK_SIZE when it is not known. */
        read_size = size;
        if (read_size > SANE_CHUNK_SIZE/10) {
            read_size = emio_limit(s, read_size);
            // If filesize/maxsize is unknown, limit to SANE_CHUNK_SIZE
            if (s->maxsize < 0)
                read_size = FFMIN(read_size, SANE_CHUNK_SIZE);
        }

        ret = av_em_grow_packet(pkt, read_size);
        if (ret < 0)
            break;

        ret = avio_em_read(s, pkt->data + prev_size, read_size);
        if (ret != read_size) {
            av_em_shrink_packet(pkt, prev_size + FFMAX(ret, 0));
            break;
        }

        size -= read_size;
    } while (size > 0);
    if (size > 0)
        pkt->flags |= AV_PKT_FLAG_CORRUPT;

    pkt->pos = orig_pos;
    if (!pkt->size)
        av_em_packet_unref(pkt);
    return pkt->size > orig_size ? pkt->size - orig_size : ret;
}

int av_em_get_packet(AVEMIOContext *s, AVEMPacket *pkt, int size)
{
    av_em_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->pos  = avio_em_tell(s);

    return append_packet_chunked(s, pkt, size);
}

int av_em_append_packet(AVEMIOContext *s, AVEMPacket *pkt, int size)
{
    if (!pkt->size)
        return av_em_get_packet(s, pkt, size);
    return append_packet_chunked(s, pkt, size);
}

int av_em_filename_number_test(const char *filename)
{
    char buf[1024];
    return filename &&
           (av_em_get_frame_filename(buf, sizeof(buf), filename, 1) >= 0);
}

static int set_codec_from_probe_data(AVEMFormatContext *s, AVEMStream *st,
                                     AVEMProbeData *pd)
{
    static const struct {
        const char *name;
        enum AVEMCodecID id;
        enum AVEMMediaType type;
    } fmt_id_type[] = {
        { "aac",       AV_CODEC_ID_AAC,        AVMEDIA_TYPE_AUDIO },
        { "ac3",       AV_CODEC_ID_AC3,        AVMEDIA_TYPE_AUDIO },
        { "dts",       AV_CODEC_ID_DTS,        AVMEDIA_TYPE_AUDIO },
        { "dvbsub",    AV_CODEC_ID_DVB_SUBTITLE,AVMEDIA_TYPE_SUBTITLE },
        { "dvbtxt",    AV_CODEC_ID_DVB_TELETEXT,AVMEDIA_TYPE_SUBTITLE },
        { "eac3",      AV_CODEC_ID_EAC3,       AVMEDIA_TYPE_AUDIO },
        { "h264",      AV_CODEC_ID_H264,       AVMEDIA_TYPE_VIDEO },
        { "hevc",      AV_CODEC_ID_HEVC,       AVMEDIA_TYPE_VIDEO },
        { "loas",      AV_CODEC_ID_AAC_LATM,   AVMEDIA_TYPE_AUDIO },
        { "m4v",       AV_CODEC_ID_MPEG4,      AVMEDIA_TYPE_VIDEO },
        { "mp3",       AV_CODEC_ID_MP3,        AVMEDIA_TYPE_AUDIO },
        { "mpegvideo", AV_CODEC_ID_MPEG2VIDEO, AVMEDIA_TYPE_VIDEO },
        { "truehd",    AV_CODEC_ID_TRUEHD,     AVMEDIA_TYPE_AUDIO },
        { 0 }
    };
    int score;
    AVEMInputFormat *fmt = av_em_probe_input_format3(pd, 1, &score);

    if (fmt && st->request_probe <= score) {
        int i;
        av_em_log(s, AV_LOG_DEBUG,
               "Probe with size=%d, packets=%d detected %s with score=%d\n",
               pd->buf_size, MAX_PROBE_PACKETS - st->probe_packets,
               fmt->name, score);
        for (i = 0; fmt_id_type[i].name; i++) {
            if (!strcmp(fmt->name, fmt_id_type[i].name)) {
                if (fmt_id_type[i].type != AVMEDIA_TYPE_AUDIO &&
                    st->codecpar->sample_rate)
                    continue;
                st->codecpar->codec_id   = fmt_id_type[i].id;
                st->codecpar->codec_type = fmt_id_type[i].type;
                st->internal->need_context_update = 1;
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
                st->codec->codec_type = st->codecpar->codec_type;
                st->codec->codec_id   = st->codecpar->codec_id;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
                return score;
            }
        }
    }
    return 0;
}

/************************************************************/
/* input media file */

int av_em_demuxer_open(AVEMFormatContext *ic) {
    int err;

    if (ic->format_whitelist && av_em_match_list(ic->iformat->name, ic->format_whitelist, ',') <= 0) {
        av_em_log(ic, AV_LOG_ERROR, "Format not on whitelist \'%s\'\n", ic->format_whitelist);
        return AVERROR(EINVAL);
    }

    if (ic->iformat->read_header) {
        err = ic->iformat->read_header(ic);
        if (err < 0)
            return err;
    }

    if (ic->pb && !ic->internal->data_offset)
        ic->internal->data_offset = avio_em_tell(ic->pb);

    return 0;
}

/* Open input file and probe the format if necessary. */
static int init_input(AVEMFormatContext *s, const char *filename,
                      AVEMDictionary **options)
{
    int ret;
    AVEMProbeData pd = { filename, NULL, 0 };
    int score = AVPROBE_SCORE_RETRY;

    if (s->pb) {
        s->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (!s->iformat)
            return av_em_probe_input_buffer2(s->pb, &s->iformat, filename,
                                         s, 0, s->format_probesize);
        else if (s->iformat->flags & AVFMT_NOFILE)
            av_em_log(s, AV_LOG_WARNING, "Custom AVEMIOContext makes no sense and "
                                      "will be ignored with AVFMT_NOFILE format.\n");
        return 0;
    }

    if ((s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
        (!s->iformat && (s->iformat = av_em_probe_input_format2(&pd, 0, &score))))
        return score;

    if ((ret = s->io_open(s, &s->pb, filename, AVIO_FLAG_READ | s->avio_flags, options)) < 0)
        return ret;

    if (s->iformat)
        return 0;
    return av_em_probe_input_buffer2(s->pb, &s->iformat, filename,
                                 s, 0, s->format_probesize);
}

static int add_to_pktbuf(AVEMPacketList **packet_buffer, AVEMPacket *pkt,
                         AVEMPacketList **plast_pktl, int ref)
{
    AVEMPacketList *pktl = av_em_mallocz(sizeof(AVEMPacketList));
    int ret;

    if (!pktl)
        return AVERROR(ENOMEM);

    if (ref) {
        if ((ret = av_em_packet_ref(&pktl->pkt, pkt)) < 0) {
            av_em_free(pktl);
            return ret;
        }
    } else {
        pktl->pkt = *pkt;
    }

    if (*packet_buffer)
        (*plast_pktl)->next = pktl;
    else
        *packet_buffer = pktl;

    /* Add the packet in the buffered packet list. */
    *plast_pktl = pktl;
    return 0;
}

int avformat_em_queue_attached_pictures(AVEMFormatContext *s)
{
    int i, ret;
    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC &&
            s->streams[i]->discard < AVDISCARD_ALL) {
            if (s->streams[i]->attached_pic.size <= 0) {
                av_em_log(s, AV_LOG_WARNING,
                    "Attached picture on stream %d has invalid size, "
                    "ignoring\n", i);
                continue;
            }

            ret = add_to_pktbuf(&s->internal->raw_packet_buffer,
                                &s->streams[i]->attached_pic,
                                &s->internal->raw_packet_buffer_end, 1);
            if (ret < 0)
                return ret;
        }
    return 0;
}

static int update_stream_avctx(AVEMFormatContext *s)
{
    int i, ret;
    for (i = 0; i < s->nb_streams; i++) {
        AVEMStream *st = s->streams[i];

        if (!st->internal->need_context_update)
            continue;

        /* update internal codec context, for the parser */
        ret = avcodec_em_parameters_to_context(st->internal->avctx, st->codecpar);
        if (ret < 0)
            return ret;

#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        /* update deprecated public codec context */
        ret = avcodec_em_parameters_to_context(st->codec, st->codecpar);
        if (ret < 0)
            return ret;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        st->internal->need_context_update = 0;
    }
    return 0;
}


int avformat_em_open_input(AVEMFormatContext **ps, const char *filename,
                        AVEMInputFormat *fmt, AVEMDictionary **options)
{
    AVEMFormatContext *s = *ps;
    int i, ret = 0;
    AVEMDictionary *tmp = NULL;
    AVEMDictionary *tmp2 = NULL;
    ID3v2ExtraMeta *id3v2_extra_meta = NULL;

    if (!s && !(s = avformat_em_alloc_context()))
        return AVERROR(ENOMEM);
    if (!s->av_class) {
        av_em_log(NULL, AV_LOG_ERROR, "Input context has not been properly allocated by avformat_em_alloc_context() and is not NULL either\n");
        return AVERROR(EINVAL);
    }
    if (fmt)
        s->iformat = fmt;

    if (options)
        av_em_dict_copy(&tmp, *options, 0);

    if (s->pb) // must be before any goto fail
        s->flags |= AVFMT_FLAG_CUSTOM_IO;

    if ((ret = av_em_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    if ((ret = init_input(s, filename, &tmp)) < 0)
        goto fail;
    s->probe_score = ret;

    if (!s->protocol_whitelist && s->pb && s->pb->protocol_whitelist) {
        s->protocol_whitelist = av_em_strdup(s->pb->protocol_whitelist);
        if (!s->protocol_whitelist) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->protocol_blacklist && s->pb && s->pb->protocol_blacklist) {
        s->protocol_blacklist = av_em_strdup(s->pb->protocol_blacklist);
        if (!s->protocol_blacklist) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (s->format_whitelist && av_em_match_list(s->iformat->name, s->format_whitelist, ',') <= 0) {
        av_em_log(s, AV_LOG_ERROR, "Format not on whitelist \'%s\'\n", s->format_whitelist);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avio_em_skip(s->pb, s->skip_initial_bytes);

    /* Check filename in case an image number is expected. */
    if (s->iformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_em_filename_number_test(filename)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    s->duration = s->start_time = AV_NOPTS_VALUE;
    av_em_strlcpy(s->filename, filename ? filename : "", sizeof(s->filename));

    /* Allocate private data. */
    if (s->iformat->priv_data_size > 0) {
        if (!(s->priv_data = av_em_mallocz(s->iformat->priv_data_size))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (s->iformat->priv_class) {
            *(const AVEMClass **) s->priv_data = s->iformat->priv_class;
            av_em_opt_set_defaults(s->priv_data);
            if ((ret = av_em_opt_set_dict(s->priv_data, &tmp)) < 0)
                goto fail;
        }
    }

    /* e.g. AVFMT_NOFILE formats will not have a AVEMIOContext */
    if (s->pb)
        em_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, 0);

    if (!(s->flags&AVFMT_FLAG_PRIV_OPT)) {
        if (s->iformat->read_header2) {
            if (options)
                av_em_dict_copy(&tmp2, *options, 0);

            if ((ret = s->iformat->read_header2(s, &tmp2)) < 0)
                goto fail;
        } else if (s->iformat->read_header && (ret = s->iformat->read_header(s)) < 0)
            goto fail;
    }

    if (id3v2_extra_meta) {
        if (!strcmp(s->iformat->name, "mp3") || !strcmp(s->iformat->name, "aac") ||
            !strcmp(s->iformat->name, "tta")) {
            if ((ret = ff_id3v2_parse_apic(s, &id3v2_extra_meta)) < 0)
                goto fail;
        } else
            av_em_log(s, AV_LOG_DEBUG, "demuxer does not support additional id3 data, skipping\n");
    }
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);

    if ((ret = avformat_em_queue_attached_pictures(s)) < 0)
        goto fail;

    if (!(s->flags&AVFMT_FLAG_PRIV_OPT) && s->pb && !s->internal->data_offset)
        s->internal->data_offset = avio_em_tell(s->pb);

    s->internal->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;

    update_stream_avctx(s);

    for (i = 0; i < s->nb_streams; i++)
        s->streams[i]->internal->orig_codec_id = s->streams[i]->codecpar->codec_id;

    if (options) {
        av_em_dict_free(options);
        *options = tmp;
        av_em_dict_free(&tmp2);
    }
    *ps = s;
    return 0;

fail:
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
    av_em_dict_free(&tmp);
    av_em_dict_free(&tmp2);
    if (s->pb && !(s->flags & AVFMT_FLAG_CUSTOM_IO))
        avio_em_closep(&s->pb);
    avformat_em_free_context(s);
    *ps = NULL;
    return ret;
}

/*******************************************************/

static void force_codec_ids(AVEMFormatContext *s, AVEMStream *st)
{
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (s->video_codec_id)
            st->codecpar->codec_id = s->video_codec_id;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (s->audio_codec_id)
            st->codecpar->codec_id = s->audio_codec_id;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (s->subtitle_codec_id)
            st->codecpar->codec_id = s->subtitle_codec_id;
        break;
    }
}

static int probe_codec(AVEMFormatContext *s, AVEMStream *st, const AVEMPacket *pkt)
{
    if (st->request_probe>0) {
        AVEMProbeData *pd = &st->probe_data;
        int end;
        av_em_log(s, AV_LOG_DEBUG, "probing stream %d pp:%d\n", st->index, st->probe_packets);
        --st->probe_packets;

        if (pkt) {
            uint8_t *new_buf = av_em_realloc(pd->buf, pd->buf_size+pkt->size+AVPROBE_PADDING_SIZE);
            if (!new_buf) {
                av_em_log(s, AV_LOG_WARNING,
                       "Failed to reallocate probe buffer for stream %d\n",
                       st->index);
                goto no_packet;
            }
            pd->buf = new_buf;
            memcpy(pd->buf + pd->buf_size, pkt->data, pkt->size);
            pd->buf_size += pkt->size;
            memset(pd->buf + pd->buf_size, 0, AVPROBE_PADDING_SIZE);
        } else {
no_packet:
            st->probe_packets = 0;
            if (!pd->buf_size) {
                av_em_log(s, AV_LOG_WARNING,
                       "nothing to probe for stream %d\n", st->index);
            }
        }

        end=    s->internal->raw_packet_buffer_remaining_size <= 0
                || st->probe_packets<= 0;

        if (end || av_log2(pd->buf_size) != av_log2(pd->buf_size - pkt->size)) {
            int score = set_codec_from_probe_data(s, st, pd);
            if (    (st->codecpar->codec_id != AV_CODEC_ID_NONE && score > AVPROBE_SCORE_STREAM_RETRY)
                || end) {
                pd->buf_size = 0;
                av_em_freep(&pd->buf);
                st->request_probe = -1;
                if (st->codecpar->codec_id != AV_CODEC_ID_NONE) {
                    av_em_log(s, AV_LOG_DEBUG, "probed stream %d\n", st->index);
                } else
                    av_em_log(s, AV_LOG_WARNING, "probed stream %d failed\n", st->index);
            }
            force_codec_ids(s, st);
        }
    }
    return 0;
}

static int update_wrap_reference(AVEMFormatContext *s, AVEMStream *st, int stream_index, AVEMPacket *pkt)
{
    int64_t ref = pkt->dts;
    int i, pts_wrap_behavior;
    int64_t pts_wrap_reference;
    AVEMProgram *first_program;

    if (ref == AV_NOPTS_VALUE)
        ref = pkt->pts;
    if (st->pts_wrap_reference != AV_NOPTS_VALUE || st->pts_wrap_bits >= 63 || ref == AV_NOPTS_VALUE || !s->correct_ts_overflow)
        return 0;
    ref &= (1LL << st->pts_wrap_bits)-1;

    // reference time stamp should be 60 s before first time stamp
    pts_wrap_reference = ref - av_em_rescale(60, st->time_base.den, st->time_base.num);
    // if first time stamp is not more than 1/8 and 60s before the wrap point, subtract rather than add wrap offset
    pts_wrap_behavior = (ref < (1LL << st->pts_wrap_bits) - (1LL << st->pts_wrap_bits-3)) ||
        (ref < (1LL << st->pts_wrap_bits) - av_em_rescale(60, st->time_base.den, st->time_base.num)) ?
        AV_PTS_WRAP_ADD_OFFSET : AV_PTS_WRAP_SUB_OFFSET;

    first_program = av_em_find_program_from_stream(s, NULL, stream_index);

    if (!first_program) {
        int default_stream_index = av_em_find_default_stream_index(s);
        if (s->streams[default_stream_index]->pts_wrap_reference == AV_NOPTS_VALUE) {
            for (i = 0; i < s->nb_streams; i++) {
                if (av_em_find_program_from_stream(s, NULL, i))
                    continue;
                s->streams[i]->pts_wrap_reference = pts_wrap_reference;
                s->streams[i]->pts_wrap_behavior = pts_wrap_behavior;
            }
        }
        else {
            st->pts_wrap_reference = s->streams[default_stream_index]->pts_wrap_reference;
            st->pts_wrap_behavior = s->streams[default_stream_index]->pts_wrap_behavior;
        }
    }
    else {
        AVEMProgram *program = first_program;
        while (program) {
            if (program->pts_wrap_reference != AV_NOPTS_VALUE) {
                pts_wrap_reference = program->pts_wrap_reference;
                pts_wrap_behavior = program->pts_wrap_behavior;
                break;
            }
            program = av_em_find_program_from_stream(s, program, stream_index);
        }

        // update every program with differing pts_wrap_reference
        program = first_program;
        while (program) {
            if (program->pts_wrap_reference != pts_wrap_reference) {
                for (i = 0; i<program->nb_stream_indexes; i++) {
                    s->streams[program->stream_index[i]]->pts_wrap_reference = pts_wrap_reference;
                    s->streams[program->stream_index[i]]->pts_wrap_behavior = pts_wrap_behavior;
                }

                program->pts_wrap_reference = pts_wrap_reference;
                program->pts_wrap_behavior = pts_wrap_behavior;
            }
            program = av_em_find_program_from_stream(s, program, stream_index);
        }
    }
    return 1;
}

int em_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    int ret, i, err;
    AVEMStream *st;

    for (;;) {
        AVEMPacketList *pktl = s->internal->raw_packet_buffer;

        if (pktl) {
            *pkt = pktl->pkt;
            st   = s->streams[pkt->stream_index];
            if (s->internal->raw_packet_buffer_remaining_size <= 0)
                if ((err = probe_codec(s, st, NULL)) < 0)
                    return err;
            if (st->request_probe <= 0) {
                s->internal->raw_packet_buffer                 = pktl->next;
                s->internal->raw_packet_buffer_remaining_size += pkt->size;
                av_em_free(pktl);
                return 0;
            }
        }

        pkt->data = NULL;
        pkt->size = 0;
        av_em_init_packet(pkt);
        ret = s->iformat->read_packet(s, pkt);
        if (ret < 0) {
            /* Some demuxers return FFERROR_REDO when they consume
               data and discard it (ignored streams, junk, extradata).
               We must re-call the demuxer to get the real packet. */
            if (ret == FFERROR_REDO)
                continue;
            if (!pktl || ret == AVERROR(EAGAIN))
                return ret;
            for (i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->probe_packets)
                    if ((err = probe_codec(s, st, NULL)) < 0)
                        return err;
                av_assert0(st->request_probe <= 0);
            }
            continue;
        }
        
        if (!pkt->buf) {
            AVEMPacket tmp = { 0 };
            ret = av_em_packet_ref(&tmp, pkt);
            if (ret < 0)
                return ret;
            *pkt = tmp;
        }

        if ((s->flags & AVFMT_FLAG_DISCARD_CORRUPT) &&
            (pkt->flags & AV_PKT_FLAG_CORRUPT)) {
            av_em_log(s, AV_LOG_WARNING,
                   "Dropped corrupted packet (stream = %d)\n",
                   pkt->stream_index);
            av_em_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index >= (unsigned)s->nb_streams) {
            av_em_log(s, AV_LOG_ERROR, "Invalid stream index %d\n", pkt->stream_index);
            continue;
        }

        st = s->streams[pkt->stream_index];

        if (update_wrap_reference(s, st, pkt->stream_index, pkt) && st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET) {
            // correct first time stamps to negative values
            if (!is_relative(st->first_dts))
                st->first_dts = wrap_timestamp(st, st->first_dts);
            if (!is_relative(st->start_time))
                st->start_time = wrap_timestamp(st, st->start_time);
            if (!is_relative(st->cur_dts))
                st->cur_dts = wrap_timestamp(st, st->cur_dts);
        }
        
        pkt->dts = wrap_timestamp(st, pkt->dts);
        pkt->pts = wrap_timestamp(st, pkt->pts);

        force_codec_ids(s, st);

        /* TODO: audio: time filter; video: frame reordering (pts != dts) */
        if (s->use_wallclock_as_timestamps)
            pkt->dts = pkt->pts = av_em_rescale_q(av_em_gettime(), AV_TIME_BASE_Q, st->time_base);

        if (!pktl && st->request_probe <= 0)
            return ret;

        err = add_to_pktbuf(&s->internal->raw_packet_buffer, pkt,
                            &s->internal->raw_packet_buffer_end, 0);
        if (err)
            return err;
        s->internal->raw_packet_buffer_remaining_size -= pkt->size;

        if ((err = probe_codec(s, st, pkt)) < 0)
            return err;
    }
}


/**********************************************************/

static int determinable_frame_size(AVEMCodecContext *avctx)
{
    if (/*avctx->codec_id == AV_CODEC_ID_AAC ||*/
        avctx->codec_id == AV_CODEC_ID_MP1 ||
        avctx->codec_id == AV_CODEC_ID_MP2 ||
        avctx->codec_id == AV_CODEC_ID_MP3/* ||
        avctx->codec_id == AV_CODEC_ID_CELT*/)
        return 1;
    return 0;
}

/**
 * Return the frame duration in seconds. Return 0 if not available.
 */
void em_compute_frame_duration(AVEMFormatContext *s, int *pnum, int *pden, AVEMStream *st,
                               AVEMCodecParserContext *pc, AVEMPacket *pkt)
{
    AVEMRational codec_framerate = s->iformat ? st->internal->avctx->framerate :
                                              av_em_mul_q(av_em_inv_q(st->internal->avctx->time_base), (AVEMRational){1, st->internal->avctx->ticks_per_frame});
    int frame_size, sample_rate;

#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    if ((!codec_framerate.den || !codec_framerate.num) && st->codec->time_base.den && st->codec->time_base.num)
        codec_framerate = av_em_mul_q(av_em_inv_q(st->codec->time_base), (AVEMRational){1, st->codec->ticks_per_frame});
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    *pnum = 0;
    *pden = 0;
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (st->r_frame_rate.num && !pc && s->iformat) {
            *pnum = st->r_frame_rate.den;
            *pden = st->r_frame_rate.num;
        } else if (st->time_base.num * 1000LL > st->time_base.den) {
            *pnum = st->time_base.num;
            *pden = st->time_base.den;
        } else if (codec_framerate.den * 1000LL > codec_framerate.num) {
            av_assert0(st->internal->avctx->ticks_per_frame);
            av_em_reduce(pnum, pden,
                      codec_framerate.den,
                      codec_framerate.num * (int64_t)st->internal->avctx->ticks_per_frame,
                      INT_MAX);

            if (pc && pc->repeat_pict) {
                av_assert0(s->iformat); // this may be wrong for interlaced encoding but its not used for that case
                av_em_reduce(pnum, pden,
                          (*pnum) * (1LL + pc->repeat_pict),
                          (*pden),
                          INT_MAX);
            }
            /* If this codec can be interlaced or progressive then we need
             * a parser to compute duration of a packet. Thus if we have
             * no parser in such case leave duration undefined. */
            if (st->internal->avctx->ticks_per_frame > 1 && !pc)
                *pnum = *pden = 0;
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (st->internal->avctx_inited) {
            frame_size = av_em_get_audio_frame_duration(st->internal->avctx, pkt->size);
            sample_rate = st->internal->avctx->sample_rate;
        } else {
            frame_size = av_em_get_audio_frame_duration2(st->codecpar, pkt->size);
            sample_rate = st->codecpar->sample_rate;
        }
        if (frame_size <= 0 || sample_rate <= 0)
            break;
        *pnum = frame_size;
        *pden = sample_rate;
        break;
    default:
        break;
    }
}

static int is_intra_only(enum AVEMCodecID id)
{
    const AVEMCodecDescriptor *d = avcodec_em_descriptor_get(id);
    if (!d)
        return 0;
    if (d->type == AVMEDIA_TYPE_VIDEO && !(d->props & AV_CODEC_PROP_INTRA_ONLY))
        return 0;
    return 1;
}

static int has_decode_delay_been_guessed(AVEMStream *st)
{
    if (st->codecpar->codec_id != AV_CODEC_ID_H264) return 1;
    if (!st->info) // if we have left find_stream_info then nb_decoded_frames won't increase anymore for stream copy
        return 1;
#if CONFIG_H264_DECODER
    if (st->internal->avctx->has_b_frames &&
       avpriv_em_h264_has_num_reorder_frames(st->internal->avctx) == st->internal->avctx->has_b_frames)
        return 1;
#endif
    if (st->internal->avctx->has_b_frames<3)
        return st->nb_decoded_frames >= 7;
    else if (st->internal->avctx->has_b_frames<4)
        return st->nb_decoded_frames >= 18;
    else
        return st->nb_decoded_frames >= 20;
}

static AVEMPacketList *get_next_pkt(AVEMFormatContext *s, AVEMStream *st, AVEMPacketList *pktl)
{
    if (pktl->next)
        return pktl->next;
    if (pktl == s->internal->packet_buffer_end)
        return s->internal->parse_queue;
    return NULL;
}

static int64_t select_from_pts_buffer(AVEMStream *st, int64_t *pts_buffer, int64_t dts) {
    int onein_oneout = st->codecpar->codec_id != AV_CODEC_ID_H264 &&
                       st->codecpar->codec_id != AV_CODEC_ID_HEVC;

    if(!onein_oneout) {
        int delay = st->internal->avctx->has_b_frames;
        int i;

        if (dts == AV_NOPTS_VALUE) {
            int64_t best_score = INT64_MAX;
            for (i = 0; i<delay; i++) {
                if (st->pts_reorder_error_count[i]) {
                    int64_t score = st->pts_reorder_error[i] / st->pts_reorder_error_count[i];
                    if (score < best_score) {
                        best_score = score;
                        dts = pts_buffer[i];
                    }
                }
            }
        } else {
            for (i = 0; i<delay; i++) {
                if (pts_buffer[i] != AV_NOPTS_VALUE) {
                    int64_t diff =  FFABS(pts_buffer[i] - dts)
                                    + (uint64_t)st->pts_reorder_error[i];
                    diff = FFMAX(diff, st->pts_reorder_error[i]);
                    st->pts_reorder_error[i] = diff;
                    st->pts_reorder_error_count[i]++;
                    if (st->pts_reorder_error_count[i] > 250) {
                        st->pts_reorder_error[i] >>= 1;
                        st->pts_reorder_error_count[i] >>= 1;
                    }
                }
            }
        }
    }

    if (dts == AV_NOPTS_VALUE)
        dts = pts_buffer[0];

    return dts;
}

/**
 * Updates the dts of packets of a stream in pkt_buffer, by re-ordering the pts
 * of the packets in a window.
 */
static void update_dts_from_pts(AVEMFormatContext *s, int stream_index,
                                AVEMPacketList *pkt_buffer)
{
    AVEMStream *st       = s->streams[stream_index];
    int delay          = st->internal->avctx->has_b_frames;
    int i;

    int64_t pts_buffer[MAX_REORDER_DELAY+1];

    for (i = 0; i<MAX_REORDER_DELAY+1; i++)
        pts_buffer[i] = AV_NOPTS_VALUE;

    for (; pkt_buffer; pkt_buffer = get_next_pkt(s, st, pkt_buffer)) {
        if (pkt_buffer->pkt.stream_index != stream_index)
            continue;

        if (pkt_buffer->pkt.pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
            pts_buffer[0] = pkt_buffer->pkt.pts;
            for (i = 0; i<delay && pts_buffer[i] > pts_buffer[i + 1]; i++)
                FFSWAP(int64_t, pts_buffer[i], pts_buffer[i + 1]);

            pkt_buffer->pkt.dts = select_from_pts_buffer(st, pts_buffer, pkt_buffer->pkt.dts);
        }
    }
}

static void update_initial_timestamps(AVEMFormatContext *s, int stream_index,
                                      int64_t dts, int64_t pts, AVEMPacket *pkt)
{
    AVEMStream *st       = s->streams[stream_index];
    AVEMPacketList *pktl = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
    AVEMPacketList *pktl_it;

    uint64_t shift;

    if (st->first_dts != AV_NOPTS_VALUE ||
        dts           == AV_NOPTS_VALUE ||
        st->cur_dts   == AV_NOPTS_VALUE ||
        is_relative(dts))
        return;

    st->first_dts = dts - (st->cur_dts - RELATIVE_TS_BASE);
    st->cur_dts   = dts;
    shift         = (uint64_t)st->first_dts - RELATIVE_TS_BASE;

    if (is_relative(pts))
        pts += shift;

    for (pktl_it = pktl; pktl_it; pktl_it = get_next_pkt(s, st, pktl_it)) {
        if (pktl_it->pkt.stream_index != stream_index)
            continue;
        if (is_relative(pktl_it->pkt.pts))
            pktl_it->pkt.pts += shift;

        if (is_relative(pktl_it->pkt.dts))
            pktl_it->pkt.dts += shift;

        if (st->start_time == AV_NOPTS_VALUE && pktl_it->pkt.pts != AV_NOPTS_VALUE) {
            st->start_time = pktl_it->pkt.pts;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->sample_rate)
                st->start_time += av_em_rescale_q(st->skip_samples, (AVEMRational){1, st->codecpar->sample_rate}, st->time_base);
        }
    }

    if (has_decode_delay_been_guessed(st)) {
        update_dts_from_pts(s, stream_index, pktl);
    }

    if (st->start_time == AV_NOPTS_VALUE) {
        st->start_time = pts;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->sample_rate)
            st->start_time += av_em_rescale_q(st->skip_samples, (AVEMRational){1, st->codecpar->sample_rate}, st->time_base);
    }
}

static void update_initial_durations(AVEMFormatContext *s, AVEMStream *st,
                                     int stream_index, int duration)
{
    AVEMPacketList *pktl = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
    int64_t cur_dts    = RELATIVE_TS_BASE;

    if (st->first_dts != AV_NOPTS_VALUE) {
        if (st->update_initial_durations_done)
            return;
        st->update_initial_durations_done = 1;
        cur_dts = st->first_dts;
        for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
            if (pktl->pkt.stream_index == stream_index) {
                if (pktl->pkt.pts != pktl->pkt.dts  ||
                    pktl->pkt.dts != AV_NOPTS_VALUE ||
                    pktl->pkt.duration)
                    break;
                cur_dts -= duration;
            }
        }
        if (pktl && pktl->pkt.dts != st->first_dts) {
            av_em_log(s, AV_LOG_DEBUG, "first_dts %s not matching first dts %s (pts %s, duration %"PRId64") in the queue\n",
                   av_ts2str(st->first_dts), av_ts2str(pktl->pkt.dts), av_ts2str(pktl->pkt.pts), pktl->pkt.duration);
            return;
        }
        if (!pktl) {
            av_em_log(s, AV_LOG_DEBUG, "first_dts %s but no packet with dts in the queue\n", av_ts2str(st->first_dts));
            return;
        }
        pktl          = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
        st->first_dts = cur_dts;
    } else if (st->cur_dts != RELATIVE_TS_BASE)
        return;

    for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
        if (pktl->pkt.stream_index != stream_index)
            continue;
        if (pktl->pkt.pts == pktl->pkt.dts  &&
            (pktl->pkt.dts == AV_NOPTS_VALUE || pktl->pkt.dts == st->first_dts) &&
            !pktl->pkt.duration) {
            pktl->pkt.dts = cur_dts;
            if (!st->internal->avctx->has_b_frames)
                pktl->pkt.pts = cur_dts;
//            if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                pktl->pkt.duration = duration;
        } else
            break;
        cur_dts = pktl->pkt.dts + pktl->pkt.duration;
    }
    if (!pktl)
        st->cur_dts = cur_dts;
}

static void compute_pkt_fields(AVEMFormatContext *s, AVEMStream *st,
                               AVEMCodecParserContext *pc, AVEMPacket *pkt,
                               int64_t next_dts, int64_t next_pts)
{
    int num, den, presentation_delayed, delay, i;
    int64_t offset;
    AVEMRational duration;
    int onein_oneout = st->codecpar->codec_id != AV_CODEC_ID_H264 &&
                       st->codecpar->codec_id != AV_CODEC_ID_HEVC;

    if (s->flags & AVFMT_FLAG_NOFILLIN)
        return;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && pkt->dts != AV_NOPTS_VALUE) {
        if (pkt->dts == pkt->pts && st->last_dts_for_order_check != AV_NOPTS_VALUE) {
            if (st->last_dts_for_order_check <= pkt->dts) {
                st->dts_ordered++;
            } else {
                av_em_log(s, st->dts_misordered ? AV_LOG_DEBUG : AV_LOG_WARNING,
                       "DTS %"PRIi64" < %"PRIi64" out of order\n",
                       pkt->dts,
                       st->last_dts_for_order_check);
                st->dts_misordered++;
            }
            if (st->dts_ordered + st->dts_misordered > 250) {
                st->dts_ordered    >>= 1;
                st->dts_misordered >>= 1;
            }
        }

        st->last_dts_for_order_check = pkt->dts;
        if (st->dts_ordered < 8*st->dts_misordered && pkt->dts == pkt->pts)
            pkt->dts = AV_NOPTS_VALUE;
    }

    if ((s->flags & AVFMT_FLAG_IGNDTS) && pkt->pts != AV_NOPTS_VALUE)
        pkt->dts = AV_NOPTS_VALUE;

    if (pc && pc->pict_type == AV_PICTURE_TYPE_B
        && !st->internal->avctx->has_b_frames)
        //FIXME Set low_delay = 0 when has_b_frames = 1
        st->internal->avctx->has_b_frames = 1;

    /* do we have a video B-frame ? */
    delay = st->internal->avctx->has_b_frames;
    presentation_delayed = 0;

    /* XXX: need has_b_frame, but cannot get it if the codec is
     *  not initialized */
    if (delay &&
        pc && pc->pict_type != AV_PICTURE_TYPE_B)
        presentation_delayed = 1;

    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
        st->pts_wrap_bits < 63 &&
        pkt->dts - (1LL << (st->pts_wrap_bits - 1)) > pkt->pts) {
        if (is_relative(st->cur_dts) || pkt->dts - (1LL<<(st->pts_wrap_bits - 1)) > st->cur_dts) {
            pkt->dts -= 1LL << st->pts_wrap_bits;
        } else
            pkt->pts += 1LL << st->pts_wrap_bits;
    }

    /* Some MPEG-2 in MPEG-PS lack dts (issue #171 / input_file.mpg).
     * We take the conservative approach and discard both.
     * Note: If this is misbehaving for an H.264 file, then possibly
     * presentation_delayed is not set correctly. */
    if (delay == 1 && pkt->dts == pkt->pts &&
        pkt->dts != AV_NOPTS_VALUE && presentation_delayed) {
        av_em_log(s, AV_LOG_DEBUG, "invalid dts/pts combination %"PRIi64"\n", pkt->dts);
        if (    strcmp(s->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2")
             && strcmp(s->iformat->name, "flv")) // otherwise we discard correct timestamps for vc1-wmapro.ism
            pkt->dts = AV_NOPTS_VALUE;
    }

    duration = av_em_mul_q((AVEMRational) {pkt->duration, 1}, st->time_base);
    if (pkt->duration == 0) {
        em_compute_frame_duration(s, &num, &den, st, pc, pkt);
        if (den && num) {
            duration = (AVEMRational) {num, den};
            pkt->duration = av_em_rescale_rnd(1,
                                           num * (int64_t) st->time_base.den,
                                           den * (int64_t) st->time_base.num,
                                           AV_ROUND_DOWN);
        }
    }

    if (pkt->duration != 0 && (s->internal->packet_buffer || s->internal->parse_queue))
        update_initial_durations(s, st, pkt->stream_index, pkt->duration);

    /* Correct timestamps with byte offset if demuxers only have timestamps
     * on packet boundaries */
    if (pc && st->need_parsing == AVSTREAM_PARSE_TIMESTAMPS && pkt->size) {
        /* this will estimate bitrate based on this frame's duration and size */
        offset = av_em_rescale(pc->offset, pkt->duration, pkt->size);
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
    }

    /* This may be redundant, but it should not hurt. */
    if (pkt->dts != AV_NOPTS_VALUE &&
        pkt->pts != AV_NOPTS_VALUE &&
        pkt->pts > pkt->dts)
        presentation_delayed = 1;

    if (s->debug & FF_FDEBUG_TS)
        av_em_log(s, AV_LOG_TRACE,
            "IN delayed:%d pts:%s, dts:%s cur_dts:%s st:%d pc:%p duration:%"PRId64" delay:%d onein_oneout:%d\n",
            presentation_delayed, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(st->cur_dts),
            pkt->stream_index, pc, pkt->duration, delay, onein_oneout);

    /* Interpolate PTS and DTS if they are not present. We skip H264
     * currently because delay and has_b_frames are not reliably set. */
    if ((delay == 0 || (delay == 1 && pc)) &&
        onein_oneout) {
        if (presentation_delayed) {
            /* DTS = decompression timestamp */
            /* PTS = presentation timestamp */
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->last_IP_pts;
            update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->cur_dts;

            /* This is tricky: the dts must be incremented by the duration
             * of the frame we are displaying, i.e. the last I- or P-frame. */
            if (st->last_IP_duration == 0)
                st->last_IP_duration = pkt->duration;
            if (pkt->dts != AV_NOPTS_VALUE)
                st->cur_dts = pkt->dts + st->last_IP_duration;
            if (pkt->dts != AV_NOPTS_VALUE &&
                pkt->pts == AV_NOPTS_VALUE &&
                st->last_IP_duration > 0 &&
                ((uint64_t)st->cur_dts - (uint64_t)next_dts + 1) <= 2 &&
                next_dts != next_pts &&
                next_pts != AV_NOPTS_VALUE)
                pkt->pts = next_dts;

            st->last_IP_duration = pkt->duration;
            st->last_IP_pts      = pkt->pts;
            /* Cannot compute PTS if not present (we can compute it only
             * by knowing the future. */
        } else if (pkt->pts != AV_NOPTS_VALUE ||
                   pkt->dts != AV_NOPTS_VALUE ||
                   pkt->duration                ) {

            /* presentation is not delayed : PTS and DTS are the same */
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            update_initial_timestamps(s, pkt->stream_index, pkt->pts,
                                      pkt->pts, pkt);
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = st->cur_dts;
            pkt->dts = pkt->pts;
            if (pkt->pts != AV_NOPTS_VALUE)
                st->cur_dts = av_em_add_stable(st->time_base, pkt->pts, duration, 1);
        }
    }

    if (pkt->pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
        st->pts_buffer[0] = pkt->pts;
        for (i = 0; i<delay && st->pts_buffer[i] > st->pts_buffer[i + 1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i + 1]);

        if(has_decode_delay_been_guessed(st))
            pkt->dts = select_from_pts_buffer(st, st->pts_buffer, pkt->dts);
    }
    // We skipped it above so we try here.
    if (!onein_oneout)
        // This should happen on the first packet
        update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
    if (pkt->dts > st->cur_dts)
        st->cur_dts = pkt->dts;

    if (s->debug & FF_FDEBUG_TS)
        av_em_log(s, AV_LOG_TRACE, "OUTdelayed:%d/%d pts:%s, dts:%s cur_dts:%s\n",
            presentation_delayed, delay, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(st->cur_dts));

    /* update flags */
    if (is_intra_only(st->codecpar->codec_id))
        pkt->flags |= AV_PKT_FLAG_KEY;
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    if (pc)
        pkt->convergence_duration = pc->convergence_duration;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
}

static void free_packet_buffer(AVEMPacketList **pkt_buf, AVEMPacketList **pkt_buf_end)
{
    while (*pkt_buf) {
        AVEMPacketList *pktl = *pkt_buf;
        *pkt_buf = pktl->next;
        av_em_packet_unref(&pktl->pkt);
        av_em_freep(&pktl);
    }
    *pkt_buf_end = NULL;
}

/**
 * Parse a packet, add all split parts to parse_queue.
 *
 * @param pkt Packet to parse, NULL when flushing the parser at end of stream.
 */
static int parse_packet(AVEMFormatContext *s, AVEMPacket *pkt, int stream_index)
{
    AVEMPacket out_pkt = { 0 }, flush_pkt = { 0 };
    AVEMStream *st = s->streams[stream_index];
    uint8_t *data = pkt ? pkt->data : NULL;
    int size      = pkt ? pkt->size : 0;
    int ret = 0, got_output = 0;

    if (!pkt) {
        av_em_init_packet(&flush_pkt);
        pkt        = &flush_pkt;
        got_output = 1;
    } else if (!size && st->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        // preserve 0-size sync packets
        compute_pkt_fields(s, st, st->parser, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
    }

    while (size > 0 || (pkt == &flush_pkt && got_output)) {
        int len;
        int64_t next_pts = pkt->pts;
        int64_t next_dts = pkt->dts;

        av_em_init_packet(&out_pkt);
        len = av_em_parser_parse2(st->parser, st->internal->avctx,
                               &out_pkt.data, &out_pkt.size, data, size,
                               pkt->pts, pkt->dts, pkt->pos);

        pkt->pts = pkt->dts = AV_NOPTS_VALUE;
        pkt->pos = -1;
        /* increment read pointer */
        data += len;
        size -= len;

        got_output = !!out_pkt.size;

        if (!out_pkt.size)
            continue;

        if (pkt->side_data) {
            out_pkt.side_data       = pkt->side_data;
            out_pkt.side_data_elems = pkt->side_data_elems;
            pkt->side_data          = NULL;
            pkt->side_data_elems    = 0;
        }

        /* set the duration */
        out_pkt.duration = (st->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) ? pkt->duration : 0;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->internal->avctx->sample_rate > 0) {
                out_pkt.duration =
                    av_em_rescale_q_rnd(st->parser->duration,
                                     (AVEMRational) { 1, st->internal->avctx->sample_rate },
                                     st->time_base,
                                     AV_ROUND_DOWN);
            }
        }

        out_pkt.stream_index = st->index;
        out_pkt.pts          = st->parser->pts;
        out_pkt.dts          = st->parser->dts;
        out_pkt.pos          = st->parser->pos;

        if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW)
            out_pkt.pos = st->parser->frame_offset;

        if (st->parser->key_frame == 1 ||
            (st->parser->key_frame == -1 &&
             st->parser->pict_type == AV_PICTURE_TYPE_I))
            out_pkt.flags |= AV_PKT_FLAG_KEY;

        if (st->parser->key_frame == -1 && st->parser->pict_type ==AV_PICTURE_TYPE_NONE && (pkt->flags&AV_PKT_FLAG_KEY))
            out_pkt.flags |= AV_PKT_FLAG_KEY;

        compute_pkt_fields(s, st, st->parser, &out_pkt, next_dts, next_pts);

        ret = add_to_pktbuf(&s->internal->parse_queue, &out_pkt,
                            &s->internal->parse_queue_end, 1);
        av_em_packet_unref(&out_pkt);
        if (ret < 0)
            goto fail;
    }

    /* end of the stream => close and free the parser */
    if (pkt == &flush_pkt) {
        av_em_parser_close(st->parser);
        st->parser = NULL;
    }

fail:
    av_em_packet_unref(pkt);
    return ret;
}

static int read_from_packet_buffer(AVEMPacketList **pkt_buffer,
                                   AVEMPacketList **pkt_buffer_end,
                                   AVEMPacket      *pkt)
{
    AVEMPacketList *pktl;
    av_assert0(*pkt_buffer);
    pktl        = *pkt_buffer;
    *pkt        = pktl->pkt;
    *pkt_buffer = pktl->next;
    if (!pktl->next)
        *pkt_buffer_end = NULL;
    av_em_freep(&pktl);
    return 0;
}

static int64_t ts_to_samples(AVEMStream *st, int64_t ts)
{
    return av_em_rescale(ts, st->time_base.num * st->codecpar->sample_rate, st->time_base.den);
}

static int read_frame_internal(AVEMFormatContext *s, AVEMPacket *pkt)
{
    int ret = 0, i, got_packet = 0;
    AVEMDictionary *metadata = NULL;

    av_em_init_packet(pkt);

    while (!got_packet && !s->internal->parse_queue) {
        AVEMStream *st;
        AVEMPacket cur_pkt;

        /* read next packet */
        ret = em_read_packet(s, &cur_pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN))
                return ret;
            /* flush the parsers */
            
            for (i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->parser && st->need_parsing)
                    parse_packet(s, NULL, st->index);
            }
            /* all remaining packets are now in parse_queue =>
             * really terminate parsing */
            break;
        }
        
        ret = 0;
        st  = s->streams[cur_pkt.stream_index];

        /* update context if required */
        if (st->internal->need_context_update) {
            if (avcodec_em_is_open(st->internal->avctx)) {
                av_em_log(s, AV_LOG_DEBUG, "Demuxer context update while decoder is open, closing and trying to re-open\n");
                avcodec_em_close(st->internal->avctx);
                st->info->found_decoder = 0;
            }

            ret = avcodec_em_parameters_to_context(st->internal->avctx, st->codecpar);
            if (ret < 0)
                return ret;

            st->internal->need_context_update = 0;
        }

        if (cur_pkt.pts != AV_NOPTS_VALUE &&
            cur_pkt.dts != AV_NOPTS_VALUE &&
            cur_pkt.pts < cur_pkt.dts) {
            av_em_log(s, AV_LOG_WARNING,
                   "Invalid timestamps stream=%d, pts=%s, dts=%s, size=%d\n",
                   cur_pkt.stream_index,
                   av_ts2str(cur_pkt.pts),
                   av_ts2str(cur_pkt.dts),
                   cur_pkt.size);
        }
        if (s->debug & FF_FDEBUG_TS)
            av_em_log(s, AV_LOG_INFO,
                   "em_read_packet stream=%d, pts=%s, dts=%s, size=%d, duration=%"PRId64", flags=%d\n",
                   cur_pkt.stream_index,
                   av_ts2str(cur_pkt.pts),
                   av_ts2str(cur_pkt.dts),
                   cur_pkt.size, cur_pkt.duration, cur_pkt.flags);

        if (st->need_parsing && !st->parser && !(s->flags & AVFMT_FLAG_NOPARSE)) {
            st->parser = av_em_parser_init(st->codecpar->codec_id);
            if (!st->parser) {
                av_em_log(s, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_em_get_name(st->codecpar->codec_id));
                /* no parser available: just output the raw packets */
                st->need_parsing = AVSTREAM_PARSE_NONE;
            } else if (st->need_parsing == AVSTREAM_PARSE_HEADERS)
                st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
            else if (st->need_parsing == AVSTREAM_PARSE_FULL_ONCE)
                st->parser->flags |= PARSER_FLAG_ONCE;
            else if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW)
                st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
        }

        if (!st->need_parsing || !st->parser) {
            /* no parsing needed: we just output the packet as is */
            *pkt = cur_pkt;
            compute_pkt_fields(s, st, NULL, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
            if ((s->iformat->flags & AVFMT_GENERIC_INDEX) &&
                (pkt->flags & AV_PKT_FLAG_KEY) && pkt->dts != AV_NOPTS_VALUE) {
                em_reduce_index(s, st->index);
                av_em_add_index_entry(st, pkt->pos, pkt->dts,
                                   0, 0, AVINDEX_KEYFRAME);
            }
            got_packet = 1;
        } else if (st->discard < AVDISCARD_ALL) {
            if ((ret = parse_packet(s, &cur_pkt, cur_pkt.stream_index)) < 0)
                return ret;
            st->codecpar->sample_rate = st->internal->avctx->sample_rate;
            st->codecpar->bit_rate = st->internal->avctx->bit_rate;
            st->codecpar->channels = st->internal->avctx->channels;
            st->codecpar->channel_layout = st->internal->avctx->channel_layout;
            st->codecpar->codec_id = st->internal->avctx->codec_id;
        } else {
            /* free packet */
            av_em_packet_unref(&cur_pkt);
        }
        if (pkt->flags & AV_PKT_FLAG_KEY)
            st->skip_to_keyframe = 0;
        if (st->skip_to_keyframe) {
            av_em_packet_unref(&cur_pkt);
            if (got_packet) {
                *pkt = cur_pkt;
            }
            got_packet = 0;
        }
    }

    if (!got_packet && s->internal->parse_queue)
        ret = read_from_packet_buffer(&s->internal->parse_queue, &s->internal->parse_queue_end, pkt);

    if (ret >= 0) {
        AVEMStream *st = s->streams[pkt->stream_index];
        int discard_padding = 0;
        if (st->first_discard_sample && pkt->pts != AV_NOPTS_VALUE) {
            int64_t pts = pkt->pts - (is_relative(pkt->pts) ? RELATIVE_TS_BASE : 0);
            int64_t sample = ts_to_samples(st, pts);
            int duration = ts_to_samples(st, pkt->duration);
            int64_t end_sample = sample + duration;
            if (duration > 0 && end_sample >= st->first_discard_sample &&
                sample < st->last_discard_sample)
                discard_padding = FFMIN(end_sample - st->first_discard_sample, duration);
        }
        if (st->start_skip_samples && (pkt->pts == 0 || pkt->pts == RELATIVE_TS_BASE))
            st->skip_samples = st->start_skip_samples;
        if (st->skip_samples || discard_padding) {
            uint8_t *p = av_em_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
            if (p) {
                AV_WL32(p, st->skip_samples);
                AV_WL32(p + 4, discard_padding);
                av_em_log(s, AV_LOG_DEBUG, "demuxer injecting skip %d / discard %d\n", st->skip_samples, discard_padding);
            }
            st->skip_samples = 0;
        }

        if (st->inject_global_side_data) {
            for (i = 0; i < st->nb_side_data; i++) {
                AVEMPacketSideData *src_sd = &st->side_data[i];
                uint8_t *dst_data;

                if (av_em_packet_get_side_data(pkt, src_sd->type, NULL))
                    continue;

                dst_data = av_em_packet_new_side_data(pkt, src_sd->type, src_sd->size);
                if (!dst_data) {
                    av_em_log(s, AV_LOG_WARNING, "Could not inject global side data\n");
                    continue;
                }

                memcpy(dst_data, src_sd->data, src_sd->size);
            }
            st->inject_global_side_data = 0;
        }

        if (!(s->flags & AVFMT_FLAG_KEEP_SIDE_DATA))
            av_em_packet_merge_side_data(pkt);
    }

    av_em_opt_get_dict_val(s, "metadata", AV_OPT_SEARCH_CHILDREN, &metadata);
    if (metadata) {
        s->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
        av_em_dict_copy(&s->metadata, metadata, 0);
        av_em_dict_free(&metadata);
        av_em_opt_set_dict_val(s, "metadata", NULL, AV_OPT_SEARCH_CHILDREN);
    }

#if FF_API_LAVF_AVCTX
    update_stream_avctx(s);
#endif

    if (s->debug & FF_FDEBUG_TS)
        av_em_log(s, AV_LOG_INFO,
               "read_frame_internal stream=%d, pts=%s, dts=%s, "
               "size=%d, duration=%"PRId64", flags=%d\n",
               pkt->stream_index,
               av_ts2str(pkt->pts),
               av_ts2str(pkt->dts),
               pkt->size, pkt->duration, pkt->flags);

    return ret;
}

int av_em_read_frame(AVEMFormatContext *s, AVEMPacket *pkt)
{
    const int genpts = s->flags & AVFMT_FLAG_GENPTS;
    int eof = 0;
    int ret;
    AVEMStream *st;

    if (!genpts) {
        ret = s->internal->packet_buffer
              ? read_from_packet_buffer(&s->internal->packet_buffer,
                                        &s->internal->packet_buffer_end, pkt)
              : read_frame_internal(s, pkt);
        if (ret < 0)
            return ret;
        goto return_packet;
    }

    for (;;) {
        AVEMPacketList *pktl = s->internal->packet_buffer;

        if (pktl) {
            AVEMPacket *next_pkt = &pktl->pkt;

            if (next_pkt->dts != AV_NOPTS_VALUE) {
                int wrap_bits = s->streams[next_pkt->stream_index]->pts_wrap_bits;
                // last dts seen for this stream. if any of packets following
                // current one had no dts, we will set this to AV_NOPTS_VALUE.
                int64_t last_dts = next_pkt->dts;
                while (pktl && next_pkt->pts == AV_NOPTS_VALUE) {
                    if (pktl->pkt.stream_index == next_pkt->stream_index &&
                        (av_em_compare_mod(next_pkt->dts, pktl->pkt.dts, 2LL << (wrap_bits - 1)) < 0)) {
                        if (av_em_compare_mod(pktl->pkt.pts, pktl->pkt.dts, 2LL << (wrap_bits - 1))) {
                            // not B-frame
                            next_pkt->pts = pktl->pkt.dts;
                        }
                        if (last_dts != AV_NOPTS_VALUE) {
                            // Once last dts was set to AV_NOPTS_VALUE, we don't change it.
                            last_dts = pktl->pkt.dts;
                        }
                    }
                    pktl = pktl->next;
                }
                if (eof && next_pkt->pts == AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE) {
                    // Fixing the last reference frame had none pts issue (For MXF etc).
                    // We only do this when
                    // 1. eof.
                    // 2. we are not able to resolve a pts value for current packet.
                    // 3. the packets for this stream at the end of the files had valid dts.
                    next_pkt->pts = last_dts + next_pkt->duration;
                }
                pktl = s->internal->packet_buffer;
            }

            /* read packet from packet buffer, if there is data */
            st = s->streams[next_pkt->stream_index];
            if (!(next_pkt->pts == AV_NOPTS_VALUE && st->discard < AVDISCARD_ALL &&
                  next_pkt->dts != AV_NOPTS_VALUE && !eof)) {
                ret = read_from_packet_buffer(&s->internal->packet_buffer,
                                               &s->internal->packet_buffer_end, pkt);
                goto return_packet;
            }
        }

        ret = read_frame_internal(s, pkt);
        if (ret < 0) {
            if (pktl && ret != AVERROR(EAGAIN)) {
                eof = 1;
                continue;
            } else
                return ret;
        }

        ret = add_to_pktbuf(&s->internal->packet_buffer, pkt,
                            &s->internal->packet_buffer_end, 1);
        av_em_packet_unref(pkt);
        if (ret < 0)
            return ret;
    }

return_packet:

    st = s->streams[pkt->stream_index];
    if ((s->iformat->flags & AVFMT_GENERIC_INDEX) && pkt->flags & AV_PKT_FLAG_KEY) {
        em_reduce_index(s, st->index);
        av_em_add_index_entry(st, pkt->pos, pkt->dts, 0, 0, AVINDEX_KEYFRAME);
    }

    if (is_relative(pkt->dts))
        pkt->dts -= RELATIVE_TS_BASE;
    if (is_relative(pkt->pts))
        pkt->pts -= RELATIVE_TS_BASE;

    return ret;
}

/* XXX: suppress the packet queue */
static void flush_packet_queue(AVEMFormatContext *s)
{
    if (!s->internal)
        return;
    free_packet_buffer(&s->internal->parse_queue,       &s->internal->parse_queue_end);
    free_packet_buffer(&s->internal->packet_buffer,     &s->internal->packet_buffer_end);
    free_packet_buffer(&s->internal->raw_packet_buffer, &s->internal->raw_packet_buffer_end);

    s->internal->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;
}

/*******************************************************/
/* seek support */

int av_em_find_default_stream_index(AVEMFormatContext *s)
{
    int i;
    AVEMStream *st;
    int best_stream = 0;
    int best_score = INT_MIN;

    if (s->nb_streams <= 0)
        return -1;
    for (i = 0; i < s->nb_streams; i++) {
        int score = 0;
        st = s->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                score -= 400;
            if (st->codecpar->width && st->codecpar->height)
                score += 50;
            score+= 25;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codecpar->sample_rate)
                score += 50;
        }
        if (st->codec_info_nb_frames)
            score += 12;

        if (st->discard != AVDISCARD_ALL)
            score += 200;

        if (score > best_score) {
            best_score = score;
            best_stream = i;
        }
    }
    return best_stream;
}

/** Flush the frame reader. */
void em_read_frame_flush(AVEMFormatContext *s)
{
    AVEMStream *st;
    int i, j;

    flush_packet_queue(s);

    /* Reset read state for each stream. */
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        if (st->parser) {
            av_em_parser_close(st->parser);
            st->parser = NULL;
        }
        st->last_IP_pts = AV_NOPTS_VALUE;
        st->last_dts_for_order_check = AV_NOPTS_VALUE;
        if (st->first_dts == AV_NOPTS_VALUE)
            st->cur_dts = RELATIVE_TS_BASE;
        else
            /* We set the current DTS to an unspecified origin. */
            st->cur_dts = AV_NOPTS_VALUE;

        st->probe_packets = MAX_PROBE_PACKETS;

        for (j = 0; j < MAX_REORDER_DELAY + 1; j++)
            st->pts_buffer[j] = AV_NOPTS_VALUE;

        if (s->internal->inject_global_side_data)
            st->inject_global_side_data = 1;

        st->skip_samples = 0;
    }
}

void em_update_cur_dts(AVEMFormatContext *s, AVEMStream *ref_st, int64_t timestamp)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVEMStream *st = s->streams[i];

        st->cur_dts =
            av_em_rescale(timestamp,
                       st->time_base.den * (int64_t) ref_st->time_base.num,
                       st->time_base.num * (int64_t) ref_st->time_base.den);
    }
}

void em_reduce_index(AVEMFormatContext *s, int stream_index)
{
    AVEMStream *st             = s->streams[stream_index];
    unsigned int max_entries = s->max_index_size / sizeof(AVEMIndexEntry);

    if ((unsigned) st->nb_index_entries >= max_entries) {
        int i;
        for (i = 0; 2 * i < st->nb_index_entries; i++)
            st->index_entries[i] = st->index_entries[2 * i];
        st->nb_index_entries = i;
    }
}

int em_index_search_timestamp_nocheck(const AVEMIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags);


int em_add_index_entry(AVEMIndexEntry **index_entries,
                       int *nb_index_entries,
                       unsigned int *index_entries_allocated_size,
                       int64_t pos, int64_t timestamp,
                       int size, int distance, int flags)
{
    AVEMIndexEntry *entries, *ie;
    int index;int i = 0;
	static int x = 0;
    if ((unsigned) *nb_index_entries + 1 >= UINT_MAX / sizeof(AVEMIndexEntry))
        return -1;

    if (timestamp == AV_NOPTS_VALUE)
        return AVERROR(EINVAL);

    if (size < 0 || size > 0x3FFFFFFF)
        return AVERROR(EINVAL);

    if (is_relative(timestamp)) //FIXME this maintains previous behavior but we should shift by the correct offset once known
        timestamp -= RELATIVE_TS_BASE;

    entries = av_em_fast_realloc(*index_entries,
                              index_entries_allocated_size,
                              (*nb_index_entries + 1) *
                              sizeof(AVEMIndexEntry));
    if (!entries)
        return -1;

    *index_entries = entries;

    index = em_index_search_timestamp_nocheck(*index_entries, *nb_index_entries,
                                      timestamp, AVSEEK_FLAG_ANY);

    if (index < 0) {
        index = (*nb_index_entries)++;
        ie    = &entries[index];
        av_assert0(index == 0 || ie[-1].timestamp < timestamp);
    } else {
        ie = &entries[index];
        if (ie->timestamp != timestamp) {
            if (ie->timestamp <= timestamp)
                return -1;
            memmove(entries + index + 1, entries + index,
                    sizeof(AVEMIndexEntry) * (*nb_index_entries - index));
        } else if (ie->pos == pos && distance < ie->min_distance)
            // do not reduce the distance
            distance = ie->min_distance;
    }

    ie->pos          = pos;
    ie->timestamp    = timestamp;
    ie->min_distance = distance;
    ie->size         = size;
    ie->flags        = flags;

#if 0
	/////////////dump entry/////////////
	//x++;

	//printf("==================entries dump=============.\n");
	printf("entries count:%d\n", *nb_index_entries);
	for (; i < *nb_index_entries; i+=(*nb_index_entries/10 > 0 ? *nb_index_entries/10 : 1)) {
		//if (x%10 == 0) {
		//	printf("====>%lld=\n", entries[i].timestamp);
		//}
		if (i> 0 && entries[i].timestamp < entries[i-1].timestamp) {
			printf("===================fatal error!!!!!timestamp is not asend,timestamp:%lld, index:%d==============.\n", timestamp, index);
		}
	}
	//printf("==================dump end================\n");
	///////////////////////////////////
#endif
	return index;
}

int av_em_add_index_entry(AVEMStream *st, int64_t pos, int64_t timestamp,
                       int size, int distance, int flags)
{
    timestamp = wrap_timestamp(st, timestamp);
    return em_add_index_entry(&st->index_entries, &st->nb_index_entries,
                              &st->index_entries_allocated_size, pos,
                              timestamp, size, distance, flags);
}

int em_index_search_timestamp(const AVEMIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags)
{
    int a, b, m;
    int64_t timestamp;

    a = -1;
    b = nb_entries;

    // Optimize appending index entries at the end.
    if (b && entries[b - 1].timestamp < wanted_timestamp)
        a = b - 1;

    while (b - a > 1) {
        m         = (a + b) >> 1;
        timestamp = entries[m].timestamp;
        if (timestamp >= wanted_timestamp)
            b = m;
        if (timestamp <= wanted_timestamp)
            a = m;
    }
	/*if (b > 0 && (entries[b].timestamp - wanted_timestamp) > 15000) {
		printf(" find entries timestamp is too far away from wanted timestamp\n");
		printf("timestamp:%lld, find timestamp:%lld, pre entries timestamp:%lld\n", \
			wanted_timestamp, entries[b].timestamp, entries[b -1].timestamp);
		return -2;
	}*/
    m = (flags & AVSEEK_FLAG_BACKWARD) ? a : b;

    if (!(flags & AVSEEK_FLAG_ANY))
        while (m >= 0 && m < nb_entries &&
               !(entries[m].flags & AVINDEX_KEYFRAME))
            m += (flags & AVSEEK_FLAG_BACKWARD) ? -1 : 1;

    if (m == nb_entries)
        return -1;
	
    return m;
}


int em_index_search_timestamp_nocheck(const AVEMIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags)
{
    int a, b, m;
    int64_t timestamp;

    a = -1;
    b = nb_entries;

    // Optimize appending index entries at the end.
    if (b && entries[b - 1].timestamp < wanted_timestamp)
        a = b - 1;

    while (b - a > 1) {
        m         = (a + b) >> 1;
        timestamp = entries[m].timestamp;
        if (timestamp >= wanted_timestamp)
            b = m;
        if (timestamp <= wanted_timestamp)
            a = m;
    }
    m = (flags & AVSEEK_FLAG_BACKWARD) ? a : b;

    if (!(flags & AVSEEK_FLAG_ANY))
        while (m >= 0 && m < nb_entries &&
               !(entries[m].flags & AVINDEX_KEYFRAME))
            m += (flags & AVSEEK_FLAG_BACKWARD) ? -1 : 1;

    if (m == nb_entries)
        return -1;
	
    return m;
}


void em_configure_buffers_for_index(AVEMFormatContext *s, int64_t time_tolerance)
{
    int ist1, ist2;
    int64_t pos_delta = 0;
    int64_t skip = 0;
    //We could use EMURLProtocol flags here but as many user applications do not use EMURLProtocols this would be unreliable
    const char *proto = avio_em_find_protocol_name(s->filename);

    if (!proto) {
        av_em_log(s, AV_LOG_INFO,
               "Protocol name not provided, cannot determine if input is local or "
               "a network protocol, buffers and access patterns cannot be configured "
               "optimally without knowing the protocol\n");
    }

    if (proto && !(strcmp(proto, "file") && strcmp(proto, "pipe") && strcmp(proto, "cache")))
        return;

    for (ist1 = 0; ist1 < s->nb_streams; ist1++) {
        AVEMStream *st1 = s->streams[ist1];
        for (ist2 = 0; ist2 < s->nb_streams; ist2++) {
            AVEMStream *st2 = s->streams[ist2];
            int i1, i2;

            if (ist1 == ist2)
                continue;

            for (i1 = i2 = 0; i1 < st1->nb_index_entries; i1++) {
                AVEMIndexEntry *e1 = &st1->index_entries[i1];
                int64_t e1_pts = av_em_rescale_q(e1->timestamp, st1->time_base, AV_TIME_BASE_Q);

                skip = FFMAX(skip, e1->size);
                for (; i2 < st2->nb_index_entries; i2++) {
                    AVEMIndexEntry *e2 = &st2->index_entries[i2];
                    int64_t e2_pts = av_em_rescale_q(e2->timestamp, st2->time_base, AV_TIME_BASE_Q);
                    if (e2_pts - e1_pts < time_tolerance)
                        continue;
                    pos_delta = FFMAX(pos_delta, e1->pos - e2->pos);
                    break;
                }
            }
        }
    }

    pos_delta *= 2;
    /* XXX This could be adjusted depending on protocol*/
    if (s->pb->buffer_size < pos_delta && pos_delta < (1<<24)) {
        av_em_log(s, AV_LOG_VERBOSE, "Reconfiguring buffers to size %"PRId64"\n", pos_delta);
        emio_set_buf_size(s->pb, pos_delta);
        s->pb->short_seek_threshold = FFMAX(s->pb->short_seek_threshold, pos_delta/2);
    }

    if (skip < (1<<23)) {
        s->pb->short_seek_threshold = FFMAX(s->pb->short_seek_threshold, skip);
    }
}

int av_em_index_search_timestamp(AVEMStream *st, int64_t wanted_timestamp, int flags)
{
    return em_index_search_timestamp(st->index_entries, st->nb_index_entries,
                                     wanted_timestamp, flags);
}

static int64_t ff_read_timestamp(AVEMFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit,
                                 int64_t (*read_timestamp)(struct AVEMFormatContext *, int , int64_t *, int64_t ))
{
    int64_t ts = read_timestamp(s, stream_index, ppos, pos_limit);
    if (stream_index >= 0)
        ts = wrap_timestamp(s->streams[stream_index], ts);
    return ts;
}

int em_seek_frame_binary(AVEMFormatContext *s, int stream_index,
                         int64_t target_ts, int flags)
{
    AVEMInputFormat *avif = s->iformat;
    int64_t av_uninit(pos_min), av_uninit(pos_max), pos, pos_limit;
    int64_t ts_min, ts_max, ts;
    int index;
    int64_t ret;
    AVEMStream *st;

    if (stream_index < 0)
        return -1;

    av_em_log(s, AV_LOG_TRACE, "read_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    ts_max =
    ts_min = AV_NOPTS_VALUE;
    pos_limit = -1; // GCC falsely says it may be uninitialized.

    st = s->streams[stream_index];
    if (st->index_entries) {
        AVEMIndexEntry *e;

        /* FIXME: Whole function must be checked for non-keyframe entries in
         * index case, especially read_timestamp(). */
        index = av_em_index_search_timestamp(st, target_ts,
                                          flags | AVSEEK_FLAG_BACKWARD);
        index = FFMAX(index, 0);
        e     = &st->index_entries[index];

        if (e->timestamp <= target_ts || e->pos == e->min_distance) {
            pos_min = e->pos;
            ts_min  = e->timestamp;
            av_em_log(s, AV_LOG_TRACE, "using cached pos_min=0x%"PRIx64" dts_min=%s\n",
                    pos_min, av_ts2str(ts_min));
        } else {
            av_assert1(index == 0);
        }

        index = av_em_index_search_timestamp(st, target_ts,
                                          flags & ~AVSEEK_FLAG_BACKWARD);
        av_assert0(index < st->nb_index_entries);
        if (index >= 0) {
            e = &st->index_entries[index];
            av_assert1(e->timestamp >= target_ts);
            pos_max   = e->pos;
            ts_max    = e->timestamp;
            pos_limit = pos_max - e->min_distance;
            av_em_log(s, AV_LOG_TRACE, "using cached pos_max=0x%"PRIx64" pos_limit=0x%"PRIx64
                    " dts_max=%s\n", pos_max, pos_limit, av_ts2str(ts_max));
        }
    }

    pos = em_gen_search(s, stream_index, target_ts, pos_min, pos_max, pos_limit,
                        ts_min, ts_max, flags, &ts, avif->read_timestamp);
    if (pos < 0)
        return -1;

    /* do the seek */
    if ((ret = avio_em_seek(s->pb, pos, SEEK_SET)) < 0)
        return ret;

    em_read_frame_flush(s);
    em_update_cur_dts(s, st, ts);

    return 0;
}

int em_find_last_ts(AVEMFormatContext *s, int stream_index, int64_t *ts, int64_t *pos,
                    int64_t (*read_timestamp)(struct AVEMFormatContext *, int , int64_t *, int64_t ))
{
    int64_t step = 1024;
    int64_t limit, ts_max;
    int64_t filesize = avio_em_size(s->pb);
    int64_t pos_max  = filesize - 1;
    do {
        limit = pos_max;
        pos_max = FFMAX(0, (pos_max) - step);
        ts_max  = ff_read_timestamp(s, stream_index,
                                    &pos_max, limit, read_timestamp);
        step   += step;
    } while (ts_max == AV_NOPTS_VALUE && 2*limit > step);
    if (ts_max == AV_NOPTS_VALUE)
        return -1;

    for (;;) {
        int64_t tmp_pos = pos_max + 1;
        int64_t tmp_ts  = ff_read_timestamp(s, stream_index,
                                            &tmp_pos, INT64_MAX, read_timestamp);
        if (tmp_ts == AV_NOPTS_VALUE)
            break;
        av_assert0(tmp_pos > pos_max);
        ts_max  = tmp_ts;
        pos_max = tmp_pos;
        if (tmp_pos >= filesize)
            break;
    }

    if (ts)
        *ts = ts_max;
    if (pos)
        *pos = pos_max;

    return 0;
}

int64_t em_gen_search(AVEMFormatContext *s, int stream_index, int64_t target_ts,
                      int64_t pos_min, int64_t pos_max, int64_t pos_limit,
                      int64_t ts_min, int64_t ts_max,
                      int flags, int64_t *ts_ret,
                      int64_t (*read_timestamp)(struct AVEMFormatContext *, int,
                                                int64_t *, int64_t))
{
    int64_t pos, ts;
    int64_t start_pos;
    int no_change;
    int ret;

    av_em_log(s, AV_LOG_TRACE, "gen_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    if (ts_min == AV_NOPTS_VALUE) {
        pos_min = s->internal->data_offset;
        ts_min  = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if (ts_min >= target_ts) {
        *ts_ret = ts_min;
        return pos_min;
    }

    if (ts_max == AV_NOPTS_VALUE) {
        if ((ret = em_find_last_ts(s, stream_index, &ts_max, &pos_max, read_timestamp)) < 0)
            return ret;
        pos_limit = pos_max;
    }

    if (ts_max <= target_ts) {
        *ts_ret = ts_max;
        return pos_max;
    }

    av_assert0(ts_min < ts_max);

    no_change = 0;
    while (pos_min < pos_limit) {
        av_em_log(s, AV_LOG_TRACE,
                "pos_min=0x%"PRIx64" pos_max=0x%"PRIx64" dts_min=%s dts_max=%s\n",
                pos_min, pos_max, av_ts2str(ts_min), av_ts2str(ts_max));
        av_assert0(pos_limit <= pos_max);

        if (no_change == 0) {
            int64_t approximate_keyframe_distance = pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = av_em_rescale(target_ts - ts_min, pos_max - pos_min,
                             ts_max - ts_min) +
                  pos_min - approximate_keyframe_distance;
        } else if (no_change == 1) {
            // bisection if interpolation did not change min / max pos last time
            pos = (pos_min + pos_limit) >> 1;
        } else {
            /* linear search if bisection failed, can only happen if there
             * are very few or no keyframes between min/max */
            pos = pos_min;
        }
        if (pos <= pos_min)
            pos = pos_min + 1;
        else if (pos > pos_limit)
            pos = pos_limit;
        start_pos = pos;

        // May pass pos_limit instead of -1.
        ts = ff_read_timestamp(s, stream_index, &pos, INT64_MAX, read_timestamp);
        if (pos == pos_max)
            no_change++;
        else
            no_change = 0;
        av_em_log(s, AV_LOG_TRACE, "%"PRId64" %"PRId64" %"PRId64" / %s %s %s"
                " target:%s limit:%"PRId64" start:%"PRId64" noc:%d\n",
                pos_min, pos, pos_max,
                av_ts2str(ts_min), av_ts2str(ts), av_ts2str(ts_max), av_ts2str(target_ts),
                pos_limit, start_pos, no_change);
        if (ts == AV_NOPTS_VALUE) {
            av_em_log(s, AV_LOG_ERROR, "read_timestamp() failed in the middle\n");
            return -1;
        }
        if (target_ts <= ts) {
            pos_limit = start_pos - 1;
            pos_max   = pos;
            ts_max    = ts;
        }
        if (target_ts >= ts) {
            pos_min = pos;
            ts_min  = ts;
        }
    }

    pos     = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;
    ts      = (flags & AVSEEK_FLAG_BACKWARD) ? ts_min  : ts_max;
#if 0
    pos_min = pos;
    ts_min  = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
    pos_min++;
    ts_max = ff_read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp);
    av_em_log(s, AV_LOG_TRACE, "pos=0x%"PRIx64" %s<=%s<=%s\n",
            pos, av_ts2str(ts_min), av_ts2str(target_ts), av_ts2str(ts_max));
#endif
    *ts_ret = ts;
    return pos;
}

static int seek_frame_byte(AVEMFormatContext *s, int stream_index,
                           int64_t pos, int flags)
{
    int64_t pos_min, pos_max;

    pos_min = s->internal->data_offset;
    pos_max = avio_em_size(s->pb) - 1;

    if (pos < pos_min)
        pos = pos_min;
    else if (pos > pos_max)
        pos = pos_max;

    avio_em_seek(s->pb, pos, SEEK_SET);

    s->io_repositioned = 1;

    return 0;
}

static int seek_frame_generic(AVEMFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    int index;
    int64_t ret;
	int64_t seek_pos;
    int64_t seek_pos1;
    int64_t duration;
    int io_seek_backword = 0;
    int seek_pts_lower_than_timestamp = 0;
    AVEMStream *st;
    AVEMIndexEntry *ie;
	AVEMDictionaryEntry *de;
    st = s->streams[stream_index];
    index = av_em_index_search_timestamp(st, timestamp, flags);
    if (s->duration > 0) {
        duration = av_em_rescale(s->duration,1000,AV_TIME_BASE);
    }
    if (index < 0 && st->nb_index_entries && timestamp < st->index_entries[0].timestamp)
        return -1;
    if (index < 0 || index == st->nb_index_entries - 1) {
        AVEMPacket pkt;
        int nonkey = 0;
		ie = &st->index_entries[st->nb_index_entries - 1];
		de = av_em_dict_get(s->metadata,"filesize",NULL,0);
		if (de && atoi(de->value) > 0 && s->duration > 0) {
            av_em_log(s, AV_LOG_INFO, "avio seek frame: filesize:%d, seek_pos:%lld, timestamp:%lld.\n", atoi(de->value), seek_pos, timestamp);
			seek_pos = (timestamp - FFMIN(duration / 30, 5000)) * atoi(de->value) / duration;
		}else if (s->bit_rate > 0){
			seek_pos = timestamp*s->bit_rate/8000;
		}else if (st->nb_index_entries) {
            av_assert0(st->index_entries);
			seek_pos = ie->pos;
        } else {
        	seek_pos = s->internal->data_offset;
        }
		
	retry:
        if ((ret = avio_em_seek(s->pb, seek_pos, SEEK_SET)) < 0)
            return ret;
        for (;;) {
            int read_status;
            do {
                read_status = av_em_read_frame(s, &pkt);
            } while (read_status == AVERROR(EAGAIN));
            if (read_status < 0)
                break;
            if (stream_index == pkt.stream_index) {
                if (pkt.dts > timestamp) {
                    //printf("pkt dts:%lld, timestmp:%lld\n", pkt.dts, timestamp);
                    if (!seek_pts_lower_than_timestamp) {
                        av_em_log(NULL, AV_LOG_INFO, "first seek pts is bigger than timestamp");
                        if (de) {
                          seek_pos1 = (int)((int64_t)atoi(de->value) * 5000 * 10 / (duration * 9));
                          seek_pos -= seek_pos1;
                          io_seek_backword = 1;
                          goto retry;
                        }
                        return 0;
                    }
                    if (pkt.flags & AV_PKT_FLAG_KEY) {
                        av_em_packet_unref(&pkt);
                        break;
                    }
                    if (nonkey++ > 1000 && st->codecpar->codec_id != AV_CODEC_ID_CDGRAPHICS) {
                        av_em_log(s, AV_LOG_ERROR,"seek_frame_generic failed as this stream seems to contain no keyframes after the target timestamp, %d non keyframes found\n", nonkey);
                        av_em_packet_unref(&pkt);
                        break;
                    }
                } else if (!io_seek_backword && de && atoi(de->value) > 0 && s->duration > 0 && timestamp - pkt.dts > 15000) {
                    seek_pos1 = (atoi(de->value) - seek_pos) * (timestamp - pkt.dts) * 9 / ((duration - pkt.dts) * 10);
                    seek_pos += seek_pos1;
                    av_em_log(s, AV_LOG_INFO, "after seek, pts is too far from timestamp, filesize:%d, seek_pos:%lld, dts:%lld, timestamp:%lld.\n", atoi(de->value), seek_pos, pkt.dts, timestamp);
                    if ((ret = avio_em_seek(s->pb, seek_pos, SEEK_SET)) < 0)
                        return ret;
                    seek_pts_lower_than_timestamp = 0;
                } else {
                    if (pkt.flags & AV_PKT_FLAG_KEY) {
                        seek_pts_lower_than_timestamp = 1;
                    }
                }
            }
            av_em_packet_unref(&pkt);
        }
        index = av_em_index_search_timestamp(st, timestamp, flags);
		if (index < 0 || FFABS(st->index_entries[index].timestamp - timestamp) > av_em_rescale_q(15, (AVEMRational){1, 1}, st->time_base)) {
			seek_pos = seek_pos*9/10;
            io_seek_backword = 1;
        	goto retry;
    	}
		ie = &st->index_entries[index];
		em_update_cur_dts(s, st, ie->timestamp);
    }else {
		ie = &st->index_entries[index];
    	if ((ret = avio_em_seek(s->pb, ie->pos, SEEK_SET)) < 0)
        	return ret;
    	em_update_cur_dts(s, st, ie->timestamp);
    }
	
    em_read_frame_flush(s);
    if (s->iformat->read_seek)
        if (s->iformat->read_seek(s, stream_index, timestamp, flags) >= 0)
            return 0;
    ///????
//    ie = &st->index_entries[index];
//    if ((ret = avio_em_seek(s->pb, ie->pos, SEEK_SET)) < 0)
//        return ret;
//    em_update_cur_dts(s, st, ie->timestamp);
    ///????
    return 0;
}

static int seek_frame_internal(AVEMFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    int ret;
    AVEMStream *st;
	printf("enter func:%s\n", __func__);
    if (flags & AVSEEK_FLAG_BYTE) {
        if (s->iformat->flags & AVFMT_NO_BYTE_SEEK)
            return -1;
        em_read_frame_flush(s);
        return seek_frame_byte(s, stream_index, timestamp, flags);
    }

    if (stream_index < 0) {
        stream_index = av_em_find_default_stream_index(s);
        if (stream_index < 0)
            return -1;

        st = s->streams[stream_index];
        /* timestamp for default must be expressed in AV_TIME_BASE units */
        timestamp = av_em_rescale(timestamp, st->time_base.den,
                               AV_TIME_BASE * (int64_t) st->time_base.num);
    }

    /* first, we try the format specific seek */
    if (s->iformat->read_seek) {
        em_read_frame_flush(s);
        ret = s->iformat->read_seek(s, stream_index, timestamp, flags);
    } else
        ret = -1;
    if (ret >= 0)
        return 0;
    if (ret == AVERROR_EOF) {
        return ret;
    }

    if (s->iformat->read_timestamp &&
        !(s->iformat->flags & AVFMT_NOBINSEARCH)) {
        em_read_frame_flush(s);
        return em_seek_frame_binary(s, stream_index, timestamp, flags);
    } else if (!(s->iformat->flags & AVFMT_NOGENSEARCH)) {
        em_read_frame_flush(s);
        return seek_frame_generic(s, stream_index, timestamp, flags);
    } else
        return -1;
}

int av_em_seek_frame(AVEMFormatContext *s, int stream_index,
                  int64_t timestamp, int flags)
{
    int ret;

    if (s->iformat->read_seek2 && !s->iformat->read_seek) {
        int64_t min_ts = INT64_MIN, max_ts = INT64_MAX;
        if ((flags & AVSEEK_FLAG_BACKWARD))
            max_ts = timestamp;
        else
            min_ts = timestamp;
        return avformat_em_seek_file(s, stream_index, min_ts, timestamp, max_ts,
                                  flags & ~AVSEEK_FLAG_BACKWARD);
    }

    ret = seek_frame_internal(s, stream_index, timestamp, flags);

    if (ret >= 0)
        ret = avformat_em_queue_attached_pictures(s);

    return ret;
}

int avformat_em_seek_file(AVEMFormatContext *s, int stream_index, int64_t min_ts,
                       int64_t ts, int64_t max_ts, int flags)
{
    if (min_ts > ts || max_ts < ts)
        return -1;
    if (stream_index < -1 || stream_index >= (int)s->nb_streams)
        return AVERROR(EINVAL);

    if (s->seek2any>0)
        flags |= AVSEEK_FLAG_ANY;
    flags &= ~AVSEEK_FLAG_BACKWARD;

    if (s->iformat->read_seek2) {
        int ret;
        em_read_frame_flush(s);

        if (stream_index == -1 && s->nb_streams == 1) {
            AVEMRational time_base = s->streams[0]->time_base;
            ts = av_em_rescale_q(ts, AV_TIME_BASE_Q, time_base);
            min_ts = av_em_rescale_rnd(min_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
            max_ts = av_em_rescale_rnd(max_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
        }

        ret = s->iformat->read_seek2(s, stream_index, min_ts,
                                     ts, max_ts, flags);

        if (ret >= 0)
            ret = avformat_em_queue_attached_pictures(s);
        return ret;
    }

    if (s->iformat->read_timestamp) {
        // try to seek via read_timestamp()
    }

    // Fall back on old API if new is not implemented but old is.
    // Note the old API has somewhat different semantics.
    if (s->iformat->read_seek || 1) {
        int dir = (ts - (uint64_t)min_ts > (uint64_t)max_ts - ts ? AVSEEK_FLAG_BACKWARD : 0);
        int ret = av_em_seek_frame(s, stream_index, ts, flags | dir);
        if (ret == AVERROR_EOF)
            return ret;
        if (ret<0 && ts != min_ts && max_ts != ts) {
            ret = av_em_seek_frame(s, stream_index, dir ? max_ts : min_ts, flags | dir);
            if (ret >= 0)
                ret = av_em_seek_frame(s, stream_index, ts, flags | (dir^AVSEEK_FLAG_BACKWARD));
        }
        return ret;
    }

    // try some generic seek like seek_frame_generic() but with new ts semantics
    return -1; //unreachable
}

int avformat_em_flush(AVEMFormatContext *s)
{
    em_read_frame_flush(s);
    return 0;
}

/*******************************************************/

/**
 * Return TRUE if the stream has accurate duration in any stream.
 *
 * @return TRUE if the stream has accurate duration for at least one component.
 */
static int has_duration(AVEMFormatContext *ic)
{
    int i;
    AVEMStream *st;

    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->duration != AV_NOPTS_VALUE)
            return 1;
    }
    if (ic->duration != AV_NOPTS_VALUE)
        return 1;
    return 0;
}

/**
 * Estimate the stream timings from the one of each components.
 *
 * Also computes the global bitrate if possible.
 */
static void update_stream_timings(AVEMFormatContext *ic)
{
    int64_t start_time, start_time1, start_time_text, end_time, end_time1;
    int64_t duration, duration1, filesize;
    int i;
    AVEMStream *st;
    AVEMProgram *p;

    start_time = INT64_MAX;
    start_time_text = INT64_MAX;
    end_time   = INT64_MIN;
    duration   = INT64_MIN;
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE && st->time_base.den) {
            start_time1 = av_em_rescale_q(st->start_time, st->time_base,
                                       AV_TIME_BASE_Q);
            if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE || st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
                if (start_time1 < start_time_text)
                    start_time_text = start_time1;
            } else
                start_time = FFMIN(start_time, start_time1);
            end_time1 = av_em_rescale_q_rnd(st->duration, st->time_base,
                                         AV_TIME_BASE_Q,
                                         AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            if (end_time1 != AV_NOPTS_VALUE && start_time1 <= INT64_MAX - end_time1) {
                end_time1 += start_time1;
                end_time = FFMAX(end_time, end_time1);
            }
            for (p = NULL; (p = av_em_find_program_from_stream(ic, p, i)); ) {
                if (p->start_time == AV_NOPTS_VALUE || p->start_time > start_time1)
                    p->start_time = start_time1;
                if (p->end_time < end_time1)
                    p->end_time = end_time1;
            }
        }
        if (st->duration != AV_NOPTS_VALUE) {
            duration1 = av_em_rescale_q(st->duration, st->time_base,
                                     AV_TIME_BASE_Q);
            duration  = FFMAX(duration, duration1);
        }
    }
    if (start_time == INT64_MAX || (start_time > start_time_text && start_time - start_time_text < AV_TIME_BASE))
        start_time = start_time_text;
    else if (start_time > start_time_text)
        av_em_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream starttime %f\n", start_time_text / (float)AV_TIME_BASE);

    if (start_time != INT64_MAX) {
        ic->start_time = start_time;
        if (end_time != INT64_MIN) {
            if (ic->nb_programs) {
                for (i = 0; i < ic->nb_programs; i++) {
                    p = ic->programs[i];
                    if (p->start_time != AV_NOPTS_VALUE && p->end_time > p->start_time)
                        duration = FFMAX(duration, p->end_time - p->start_time);
                }
            } else
                duration = FFMAX(duration, end_time - start_time);
        }
    }
    if (duration != INT64_MIN && duration > 0 && ic->duration == AV_NOPTS_VALUE) {
        ic->duration = duration;
    }
    if (ic->pb && (filesize = avio_em_size(ic->pb)) > 0 && ic->duration > 0) {
        /* compute the bitrate */
        double bitrate = (double) filesize * 8.0 * AV_TIME_BASE /
                         (double) ic->duration;
        if (bitrate >= 0 && bitrate <= INT64_MAX)
            ic->bit_rate = bitrate;
    }
}

static void fill_all_stream_timings(AVEMFormatContext *ic)
{
    int i;
    AVEMStream *st;

    update_stream_timings(ic);
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE) {
            if (ic->start_time != AV_NOPTS_VALUE)
                st->start_time = av_em_rescale_q(ic->start_time, AV_TIME_BASE_Q,
                                              st->time_base);
            if (ic->duration != AV_NOPTS_VALUE)
                st->duration = av_em_rescale_q(ic->duration, AV_TIME_BASE_Q,
                                            st->time_base);
        }
    }
}

static void estimate_timings_from_bit_rate(AVEMFormatContext *ic)
{
    int64_t filesize, duration;
    int i, show_warning = 0;
    AVEMStream *st;

    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate <= 0) {
        int64_t bit_rate = 0;
        for (i = 0; i < ic->nb_streams; i++) {
            st = ic->streams[i];
            if (st->codecpar->bit_rate <= 0 && st->internal->avctx->bit_rate > 0)
                st->codecpar->bit_rate = st->internal->avctx->bit_rate;
            if (st->codecpar->bit_rate > 0) {
                if (INT64_MAX - st->codecpar->bit_rate < bit_rate) {
                    bit_rate = 0;
                    break;
                }
                bit_rate += st->codecpar->bit_rate;
            } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && st->codec_info_nb_frames > 1) {
                // If we have a videostream with packets but without a bitrate
                // then consider the sum not known
                bit_rate = 0;
                break;
            }
        }
        ic->bit_rate = bit_rate;
    }

    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE &&
        ic->bit_rate != 0) {
        filesize = ic->pb ? avio_em_size(ic->pb) : 0;
        if (filesize > ic->internal->data_offset) {
            filesize -= ic->internal->data_offset;
            for (i = 0; i < ic->nb_streams; i++) {
                st      = ic->streams[i];
                if (   st->time_base.num <= INT64_MAX / ic->bit_rate
                    && st->duration == AV_NOPTS_VALUE) {
                    duration = av_em_rescale(8 * filesize, st->time_base.den,
                                          ic->bit_rate *
                                          (int64_t) st->time_base.num);
                    st->duration = duration;
                    show_warning = 1;
                }
            }
        }
    }
    if (show_warning)
        av_em_log(ic, AV_LOG_WARNING,
               "Estimating duration from bitrate, this may be inaccurate\n");
}

#define DURATION_MAX_READ_SIZE 250000LL
#define DURATION_MAX_RETRY 6

/* only usable for MPEG-PS streams */
static void estimate_timings_from_pts(AVEMFormatContext *ic, int64_t old_offset)
{
    AVEMPacket pkt1, *pkt = &pkt1;
    AVEMStream *st;
    int num, den, read_size, i, ret;
    int found_duration = 0;
    int is_end;
    int64_t filesize, offset, duration;
    int retry = 0;

    /* flush packet queue */
    flush_packet_queue(ic);

    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE &&
            st->first_dts == AV_NOPTS_VALUE &&
            st->codecpar->codec_type != AVMEDIA_TYPE_UNKNOWN)
            av_em_log(ic, AV_LOG_WARNING,
                   "start time for stream %d is not set in estimate_timings_from_pts\n", i);

        if (st->parser) {
            av_em_parser_close(st->parser);
            st->parser = NULL;
        }
    }

    av_em_opt_set(ic, "skip_changes", "1", AV_OPT_SEARCH_CHILDREN);
    /* estimate the end time (duration) */
    /* XXX: may need to support wrapping */
    filesize = ic->pb ? avio_em_size(ic->pb) : 0;
    do {
        is_end = found_duration;
        offset = filesize - (DURATION_MAX_READ_SIZE << retry);
        if (offset < 0)
            offset = 0;

        avio_em_seek(ic->pb, offset, SEEK_SET);
        read_size = 0;
        for (;;) {
            if (read_size >= DURATION_MAX_READ_SIZE << (FFMAX(retry - 1, 0)))
                break;

            do {
                ret = em_read_packet(ic, pkt);
            } while (ret == AVERROR(EAGAIN));
            if (ret != 0)
                break;
            read_size += pkt->size;
            st         = ic->streams[pkt->stream_index];
            if (pkt->pts != AV_NOPTS_VALUE &&
                (st->start_time != AV_NOPTS_VALUE ||
                 st->first_dts  != AV_NOPTS_VALUE)) {
                if (pkt->duration == 0) {
                    em_compute_frame_duration(ic, &num, &den, st, st->parser, pkt);
                    if (den && num) {
                        pkt->duration = av_em_rescale_rnd(1,
                                           num * (int64_t) st->time_base.den,
                                           den * (int64_t) st->time_base.num,
                                           AV_ROUND_DOWN);
                    }
                }
                duration = pkt->pts + pkt->duration;
                found_duration = 1;
                if (st->start_time != AV_NOPTS_VALUE)
                    duration -= st->start_time;
                else
                    duration -= st->first_dts;
                if (duration > 0) {
                    if (st->duration == AV_NOPTS_VALUE || st->info->last_duration<= 0 ||
                        (st->duration < duration && FFABS(duration - st->info->last_duration) < 60LL*st->time_base.den / st->time_base.num))
                        st->duration = duration;
                    st->info->last_duration = duration;
                }
            }
            av_em_packet_unref(pkt);
        }

        /* check if all audio/video streams have valid duration */
        if (!is_end) {
            is_end = 1;
            for (i = 0; i < ic->nb_streams; i++) {
                st = ic->streams[i];
                switch (st->codecpar->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                    case AVMEDIA_TYPE_AUDIO:
                        if (st->duration == AV_NOPTS_VALUE)
                            is_end = 0;
                }
            }
        }
    } while (!is_end &&
             offset &&
             ++retry <= DURATION_MAX_RETRY);

    av_em_opt_set(ic, "skip_changes", "0", AV_OPT_SEARCH_CHILDREN);

    /* warn about audio/video streams which duration could not be estimated */
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->duration == AV_NOPTS_VALUE) {
            switch (st->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_AUDIO:
                if (st->start_time != AV_NOPTS_VALUE || st->first_dts  != AV_NOPTS_VALUE) {
                    av_em_log(ic, AV_LOG_DEBUG, "stream %d : no PTS found at end of file, duration not set\n", i);
                } else
                    av_em_log(ic, AV_LOG_DEBUG, "stream %d : no TS found at start of file, duration not set\n", i);
            }
        }
    }
    fill_all_stream_timings(ic);

    avio_em_seek(ic->pb, old_offset, SEEK_SET);
    for (i = 0; i < ic->nb_streams; i++) {
        int j;

        st              = ic->streams[i];
        st->cur_dts     = st->first_dts;
        st->last_IP_pts = AV_NOPTS_VALUE;
        st->last_dts_for_order_check = AV_NOPTS_VALUE;
        for (j = 0; j < MAX_REORDER_DELAY + 1; j++)
            st->pts_buffer[j] = AV_NOPTS_VALUE;
    }
}

static void estimate_timings(AVEMFormatContext *ic, int64_t old_offset)
{
    int64_t file_size;

    /* get the file size, if possible */
    if (ic->iformat->flags & AVFMT_NOFILE) {
        file_size = 0;
    } else {
        file_size = avio_em_size(ic->pb);
        file_size = FFMAX(0, file_size);
    }

    if ((!strcmp(ic->iformat->name, "mpeg") ||
         !strcmp(ic->iformat->name, "mpegts")) &&
        file_size && ic->pb->seekable) {
        /* get accurate estimate from the PTSes */
        estimate_timings_from_pts(ic, old_offset);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_PTS;
    } else if (has_duration(ic)) {
        /* at least one component has timings - we use them for all
         * the components */
        fill_all_stream_timings(ic);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_STREAM;
    } else {
        /* less precise: use bitrate info */
        estimate_timings_from_bit_rate(ic);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_BITRATE;
    }
    update_stream_timings(ic);

    {
        int i;
        AVEMStream av_unused *st;
        for (i = 0; i < ic->nb_streams; i++) {
            st = ic->streams[i];
            av_em_log(ic, AV_LOG_TRACE, "stream %d: start_time: %0.3f duration: %0.3f\n", i,
                   (double) st->start_time * av_em_q2d(st->time_base),
                   (double) st->duration   * av_em_q2d(st->time_base));
        }
        av_em_log(ic, AV_LOG_TRACE,
                "format: start_time: %0.3f duration: %0.3f bitrate=%"PRId64" kb/s\n",
                (double) ic->start_time / AV_TIME_BASE,
                (double) ic->duration   / AV_TIME_BASE,
                (int64_t)ic->bit_rate / 1000);
    }
}

static int has_codec_parameters(AVEMStream *st, const char **errmsg_ptr)
{
    AVEMCodecContext *avctx = st->internal->avctx;

#define FAIL(errmsg) do {                                         \
        if (errmsg_ptr)                                           \
            *errmsg_ptr = errmsg;                                 \
        return 0;                                                 \
    } while (0)

    if (   avctx->codec_id == AV_CODEC_ID_NONE
        && avctx->codec_type != AVMEDIA_TYPE_DATA)
        FAIL("unknown codec");
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (!avctx->frame_size && determinable_frame_size(avctx))
            FAIL("unspecified frame size");
        if (st->info->found_decoder >= 0 &&
            avctx->sample_fmt == AV_SAMPLE_FMT_NONE)
            FAIL("unspecified sample format");
        if (!avctx->sample_rate)
            FAIL("unspecified sample rate");
        if (!avctx->channels)
            FAIL("unspecified number of channels");
        if (st->info->found_decoder >= 0 && !st->nb_decoded_frames && avctx->codec_id == AV_CODEC_ID_DTS)
            FAIL("no decodable DTS frames");
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (!avctx->width)
            FAIL("unspecified size");
        if (st->info->found_decoder >= 0 && avctx->pix_fmt == AV_PIX_FMT_NONE)
            FAIL("unspecified pixel format");
        if (st->codecpar->codec_id == AV_CODEC_ID_RV30 || st->codecpar->codec_id == AV_CODEC_ID_RV40)
            if (!st->sample_aspect_ratio.num && !st->codecpar->sample_aspect_ratio.num && !st->codec_info_nb_frames)
                FAIL("no frame in rv30/40 and no sar");
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE && !avctx->width)
            FAIL("unspecified size");
        break;
    case AVMEDIA_TYPE_DATA:
        if (avctx->codec_id == AV_CODEC_ID_NONE) return 1;
    }

    return 1;
}

/* returns 1 or 0 if or if not decoded data was returned, or a negative error */
static int try_decode_frame(AVEMFormatContext *s, AVEMStream *st, AVEMPacket *avpkt,
                            AVEMDictionary **options)
{
    AVEMCodecContext *avctx = st->internal->avctx;
    const AVEMCodec *codec;
    int got_picture = 1, ret = 0;
    AVFrame *frame = av_em_frame_alloc();
    AVSubtitle subtitle;
    AVEMPacket pkt = *avpkt;
    int do_skip_frame = 0;
    enum AVEMDiscard skip_frame;

    if (!frame)
        return AVERROR(ENOMEM);

    if (!avcodec_em_is_open(avctx) &&
        st->info->found_decoder <= 0 &&
        (st->codecpar->codec_id != -st->info->found_decoder || !st->codecpar->codec_id)) {
        AVEMDictionary *thread_opt = NULL;

        codec = find_decoder(s, st, st->codecpar->codec_id);

        if (!codec) {
            st->info->found_decoder = -st->codecpar->codec_id;
            ret                     = -1;
            goto fail;
        }

        /* Force thread count to 1 since the H.264 decoder will not extract
         * SPS and PPS to extradata during multi-threaded decoding. */
        av_em_dict_set(options ? options : &thread_opt, "threads", "1", 0);
        if (s->codec_whitelist)
            av_em_dict_set(options ? options : &thread_opt, "codec_whitelist", s->codec_whitelist, 0);
        ret = avcodec_em_open2(avctx, codec, options ? options : &thread_opt);
        if (!options)
            av_em_dict_free(&thread_opt);
        if (ret < 0) {
            st->info->found_decoder = -avctx->codec_id;
            goto fail;
        }
        st->info->found_decoder = 1;
    } else if (!st->info->found_decoder)
        st->info->found_decoder = 1;

    if (st->info->found_decoder < 0) {
        ret = -1;
        goto fail;
    }

    if (avpriv_em_codec_get_cap_skip_frame_fill_param(avctx->codec)) {
        do_skip_frame = 1;
        skip_frame = avctx->skip_frame;
        avctx->skip_frame = AVDISCARD_ALL;
    }

    while ((pkt.size > 0 || (!pkt.data && got_picture)) &&
           ret >= 0 &&
           (!has_codec_parameters(st, NULL) || !has_decode_delay_been_guessed(st) ||
            (!st->codec_info_nb_frames &&
             (avctx->codec->capabilities & AV_CODEC_CAP_CHANNEL_CONF)))) {
        got_picture = 0;
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            ret = avcodec_em_send_packet(avctx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                break;
            if (ret >= 0)
                pkt.size = 0;
            ret = avcodec_em_receive_frame(avctx, frame);
            if (ret >= 0)
                got_picture = 1;
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
        } else if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            ret = avcodec_em_decode_subtitle2(avctx, &subtitle,
                                           &got_picture, &pkt);
            if (ret >= 0)
                pkt.size = 0;
        }
        if (ret >= 0) {
            if (got_picture)
                st->nb_decoded_frames++;
            ret       = got_picture;
        }
    }

    if (!pkt.data && !got_picture)
        ret = -1;

fail:
    if (do_skip_frame) {
        avctx->skip_frame = skip_frame;
    }

    av_em_frame_free(&frame);
    return ret;
}

unsigned int em_codec_get_tag(const AVEMCodecTag *tags, enum AVEMCodecID id)
{
    while (tags->id != AV_CODEC_ID_NONE) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum AVEMCodecID em_codec_get_id(const AVEMCodecTag *tags, unsigned int tag)
{
    int i;
    for (i = 0; tags[i].id != AV_CODEC_ID_NONE; i++)
        if (tag == tags[i].tag)
            return tags[i].id;
    for (i = 0; tags[i].id != AV_CODEC_ID_NONE; i++)
        if (avpriv_em_toupper4(tag) == avpriv_em_toupper4(tags[i].tag))
            return tags[i].id;
    return AV_CODEC_ID_NONE;
}

enum AVEMCodecID em_get_pcm_codec_id(int bps, int flt, int be, int sflags)
{
    if (bps <= 0 || bps > 64)
        return AV_CODEC_ID_NONE;

    if (flt) {
        switch (bps) {
        case 32:
            return be ? AV_CODEC_ID_PCM_F32BE : AV_CODEC_ID_PCM_F32LE;
        case 64:
            return be ? AV_CODEC_ID_PCM_F64BE : AV_CODEC_ID_PCM_F64LE;
        default:
            return AV_CODEC_ID_NONE;
        }
    } else {
        bps  += 7;
        bps >>= 3;
        if (sflags & (1 << (bps - 1))) {
            switch (bps) {
            case 1:
                return AV_CODEC_ID_PCM_S8;
            case 2:
                return be ? AV_CODEC_ID_PCM_S16BE : AV_CODEC_ID_PCM_S16LE;
            case 3:
                return be ? AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
            case 4:
                return be ? AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
            default:
                return AV_CODEC_ID_NONE;
            }
        } else {
            switch (bps) {
            case 1:
                return AV_CODEC_ID_PCM_U8;
            case 2:
                return be ? AV_CODEC_ID_PCM_U16BE : AV_CODEC_ID_PCM_U16LE;
            case 3:
                return be ? AV_CODEC_ID_PCM_U24BE : AV_CODEC_ID_PCM_U24LE;
            case 4:
                return be ? AV_CODEC_ID_PCM_U32BE : AV_CODEC_ID_PCM_U32LE;
            default:
                return AV_CODEC_ID_NONE;
            }
        }
    }
}

unsigned int av_em_codec_get_tag(const AVEMCodecTag *const *tags, enum AVEMCodecID id)
{
    unsigned int tag;
    if (!av_em_codec_get_tag2(tags, id, &tag))
        return 0;
    return tag;
}

int av_em_codec_get_tag2(const AVEMCodecTag * const *tags, enum AVEMCodecID id,
                      unsigned int *tag)
{
    int i;
    for (i = 0; tags && tags[i]; i++) {
        const AVEMCodecTag *codec_tags = tags[i];
        while (codec_tags->id != AV_CODEC_ID_NONE) {
            if (codec_tags->id == id) {
                *tag = codec_tags->tag;
                return 1;
            }
            codec_tags++;
        }
    }
    return 0;
}

enum AVEMCodecID av_em_codec_get_id(const AVEMCodecTag *const *tags, unsigned int tag)
{
    int i;
    for (i = 0; tags && tags[i]; i++) {
        enum AVEMCodecID id = em_codec_get_id(tags[i], tag);
        if (id != AV_CODEC_ID_NONE)
            return id;
    }
    return AV_CODEC_ID_NONE;
}

static void compute_chapters_end(AVEMFormatContext *s)
{
    unsigned int i, j;
    int64_t max_time = 0;

    if (s->duration > 0 && s->start_time < INT64_MAX - s->duration)
        max_time = s->duration +
                       ((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time);

    for (i = 0; i < s->nb_chapters; i++)
        if (s->chapters[i]->end == AV_NOPTS_VALUE) {
            AVEMChapter *ch = s->chapters[i];
            int64_t end = max_time ? av_em_rescale_q(max_time, AV_TIME_BASE_Q,
                                                  ch->time_base)
                                   : INT64_MAX;

            for (j = 0; j < s->nb_chapters; j++) {
                AVEMChapter *ch1     = s->chapters[j];
                int64_t next_start = av_em_rescale_q(ch1->start, ch1->time_base,
                                                  ch->time_base);
                if (j != i && next_start > ch->start && next_start < end)
                    end = next_start;
            }
            ch->end = (end == INT64_MAX) ? ch->start : end;
        }
}

static int get_std_framerate(int i)
{
    if (i < 30*12)
        return (i + 1) * 1001;
    i -= 30*12;

    if (i < 30)
        return (i + 31) * 1001 * 12;
    i -= 30;

    if (i < 3)
        return ((const int[]) { 80, 120, 240})[i] * 1001 * 12;

    i -= 3;

    return ((const int[]) { 24, 30, 60, 12, 15, 48 })[i] * 1000 * 12;
}

/* Is the time base unreliable?
 * This is a heuristic to balance between quick acceptance of the values in
 * the headers vs. some extra checks.
 * Old DivX and Xvid often have nonsense timebases like 1fps or 2fps.
 * MPEG-2 commonly misuses field repeat flags to store different framerates.
 * And there are "variable" fps files this needs to detect as well. */
static int tb_unreliable(AVEMCodecContext *c)
{
    if (c->time_base.den >= 101LL * c->time_base.num ||
        c->time_base.den <    5LL * c->time_base.num ||
        // c->codec_tag == AV_RL32("DIVX") ||
        // c->codec_tag == AV_RL32("XVID") ||
        c->codec_tag == AV_RL32("mp4v") ||
        c->codec_id == AV_CODEC_ID_MPEG2VIDEO ||
        c->codec_id == AV_CODEC_ID_GIF ||
        c->codec_id == AV_CODEC_ID_HEVC ||
        c->codec_id == AV_CODEC_ID_H264)
        return 1;
    return 0;
}

int em_alloc_extradata(AVEMCodecParameters *par, int size)
{
    int ret;

    if (size < 0 || size >= INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        par->extradata = NULL;
        par->extradata_size = 0;
        return AVERROR(EINVAL);
    }
    par->extradata = av_em_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (par->extradata) {
        memset(par->extradata + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        par->extradata_size = size;
        ret = 0;
    } else {
        par->extradata_size = 0;
        ret = AVERROR(ENOMEM);
    }
    return ret;
}

int em_get_extradata(AVEMFormatContext *s, AVEMCodecParameters *par, AVEMIOContext *pb, int size)
{
    int ret = em_alloc_extradata(par, size);
    if (ret < 0)
        return ret;
    ret = avio_em_read(pb, par->extradata, size);
    if (ret != size) {
        av_em_freep(&par->extradata);
        par->extradata_size = 0;
        av_em_log(s, AV_LOG_ERROR, "Failed to read extradata of size %d\n", size);
        return ret < 0 ? ret : AVERROR_INVALIDDATA;
    }

    return ret;
}

int em_rfps_add_frame(AVEMFormatContext *ic, AVEMStream *st, int64_t ts)
{
    int i, j;
    int64_t last = st->info->last_dts;

    if (   ts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && ts > last
       && ts - (uint64_t)last < INT64_MAX) {
        double dts = (is_relative(ts) ?  ts - RELATIVE_TS_BASE : ts) * av_em_q2d(st->time_base);
        int64_t duration = ts - last;

        if (!st->info->duration_error)
            st->info->duration_error = av_em_mallocz(sizeof(st->info->duration_error[0])*2);
        if (!st->info->duration_error)
            return AVERROR(ENOMEM);

//         if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//             av_em_log(NULL, AV_LOG_ERROR, "%f\n", dts);
        for (i = 0; i<MAX_STD_TIMEBASES; i++) {
            if (st->info->duration_error[0][1][i] < 1e10) {
                int framerate = get_std_framerate(i);
                double sdts = dts*framerate/(1001*12);
                for (j= 0; j<2; j++) {
                    int64_t ticks = llrint(sdts+j*0.5);
                    double error= sdts - ticks + j*0.5;
                    st->info->duration_error[j][0][i] += error;
                    st->info->duration_error[j][1][i] += error*error;
                }
            }
        }
        st->info->duration_count++;
        st->info->rfps_duration_sum += duration;

        if (st->info->duration_count % 10 == 0) {
            int n = st->info->duration_count;
            for (i = 0; i<MAX_STD_TIMEBASES; i++) {
                if (st->info->duration_error[0][1][i] < 1e10) {
                    double a0     = st->info->duration_error[0][0][i] / n;
                    double error0 = st->info->duration_error[0][1][i] / n - a0*a0;
                    double a1     = st->info->duration_error[1][0][i] / n;
                    double error1 = st->info->duration_error[1][1][i] / n - a1*a1;
                    if (error0 > 0.04 && error1 > 0.04) {
                        st->info->duration_error[0][1][i] = 2e10;
                        st->info->duration_error[1][1][i] = 2e10;
                    }
                }
            }
        }

        // ignore the first 4 values, they might have some random jitter
        if (st->info->duration_count > 3 && is_relative(ts) == is_relative(last))
            st->info->duration_gcd = av_em_gcd(st->info->duration_gcd, duration);
    }
    if (ts != AV_NOPTS_VALUE)
        st->info->last_dts = ts;

    return 0;
}

void em_rfps_calculate(AVEMFormatContext *ic)
{
    int i, j;

    for (i = 0; i < ic->nb_streams; i++) {
        AVEMStream *st = ic->streams[i];

        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;
        // the check for tb_unreliable() is not completely correct, since this is not about handling
        // an unreliable/inexact time base, but a time base that is finer than necessary, as e.g.
        // ipmovie.c produces.
        if (tb_unreliable(st->internal->avctx) && st->info->duration_count > 15 && st->info->duration_gcd > FFMAX(1, st->time_base.den/(500LL*st->time_base.num)) && !st->r_frame_rate.num)
            av_em_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, st->time_base.den, st->time_base.num * st->info->duration_gcd, INT_MAX);
        if (st->info->duration_count>1 && !st->r_frame_rate.num
            && tb_unreliable(st->internal->avctx)) {
            int num = 0;
            double best_error= 0.01;
            AVEMRational ref_rate = st->r_frame_rate.num ? st->r_frame_rate : av_em_inv_q(st->time_base);

            for (j= 0; j<MAX_STD_TIMEBASES; j++) {
                int k;

                if (st->info->codec_info_duration &&
                    st->info->codec_info_duration*av_em_q2d(st->time_base) < (1001*11.5)/get_std_framerate(j))
                    continue;
                if (!st->info->codec_info_duration && get_std_framerate(j) < 1001*12)
                    continue;

                if (av_em_q2d(st->time_base) * st->info->rfps_duration_sum / st->info->duration_count < (1001*12.0 * 0.8)/get_std_framerate(j))
                    continue;

                for (k= 0; k<2; k++) {
                    int n = st->info->duration_count;
                    double a= st->info->duration_error[k][0][j] / n;
                    double error= st->info->duration_error[k][1][j]/n - a*a;

                    if (error < best_error && best_error> 0.000000001) {
                        best_error= error;
                        num = get_std_framerate(j);
                    }
                    if (error < 0.02)
                        av_em_log(ic, AV_LOG_DEBUG, "rfps: %f %f\n", get_std_framerate(j) / 12.0/1001, error);
                }
            }
            // do not increase frame rate by more than 1 % in order to match a standard rate.
            if (num && (!ref_rate.num || (double)num/(12*1001) < 1.01 * av_em_q2d(ref_rate)))
                av_em_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, num, 12*1001, INT_MAX);
        }
        if (   !st->avg_frame_rate.num
            && st->r_frame_rate.num && st->info->rfps_duration_sum
            && st->info->codec_info_duration <= 0
            && st->info->duration_count > 2
            && fabs(1.0 / (av_em_q2d(st->r_frame_rate) * av_em_q2d(st->time_base)) - st->info->rfps_duration_sum / (double)st->info->duration_count) <= 1.0
            ) {
            av_em_log(ic, AV_LOG_DEBUG, "Setting avg frame rate based on r frame rate\n");
            st->avg_frame_rate = st->r_frame_rate;
        }

        av_em_freep(&st->info->duration_error);
        st->info->last_dts = AV_NOPTS_VALUE;
        st->info->duration_count = 0;
        st->info->rfps_duration_sum = 0;
    }
}

int avformat_em_find_stream_info(AVEMFormatContext *ic, AVEMDictionary **options)
{
    int i, count = 0, ret = 0, j;
    int64_t read_size;
    AVEMStream *st;
    AVEMCodecContext *avctx;
    AVEMPacket pkt1, *pkt;
    int64_t old_offset  = avio_em_tell(ic->pb);
    // new streams might appear, no options for those
    int orig_nb_streams = ic->nb_streams;
    int flush_codecs;
    int64_t max_analyze_duration = ic->max_analyze_duration;
    int64_t max_stream_analyze_duration;
    int64_t max_subtitle_analyze_duration;
    int64_t probesize = ic->probesize;
    int eof_reached = 0;

    flush_codecs = probesize > 0;

    av_em_opt_set(ic, "skip_clear", "1", AV_OPT_SEARCH_CHILDREN);

    max_stream_analyze_duration = max_analyze_duration;
    max_subtitle_analyze_duration = max_analyze_duration;
    if (!max_analyze_duration) {
        max_stream_analyze_duration =
        max_analyze_duration        = 5*AV_TIME_BASE;
        max_subtitle_analyze_duration = 30*AV_TIME_BASE;
        if (!strcmp(ic->iformat->name, "flv"))
            max_stream_analyze_duration = 90*AV_TIME_BASE;
        if (!strcmp(ic->iformat->name, "mpeg") || !strcmp(ic->iformat->name, "mpegts"))
            max_stream_analyze_duration = 7*AV_TIME_BASE;
    }

    if (ic->pb)
        av_em_log(ic, AV_LOG_DEBUG, "Before avformat_em_find_stream_info() pos: %"PRId64" bytes read:%"PRId64" seeks:%d nb_streams:%d\n",
               avio_em_tell(ic->pb), ic->pb->bytes_read, ic->pb->seek_count, ic->nb_streams);
	//av_em_log(ic, AV_LOG_INFO, "1:nb_streams count:%d\n", ic->nb_streams);
    for (i = 0; i < ic->nb_streams; i++) {
        const AVEMCodec *codec;
        AVEMDictionary *thread_opt = NULL;
        st = ic->streams[i];
        avctx = st->internal->avctx;

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
            st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
/*            if (!st->time_base.num)
                st->time_base = */
            if (!avctx->time_base.num)
                avctx->time_base = st->time_base;
        }
        // only for the split stuff
        if (!st->parser && !(ic->flags & AVFMT_FLAG_NOPARSE) && st->request_probe <= 0) {
            st->parser = av_em_parser_init(st->codecpar->codec_id);
            if (st->parser) {
                if (st->need_parsing == AVSTREAM_PARSE_HEADERS) {
                    st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
                } else if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW) {
                    st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
                }
            } else if (st->need_parsing) {
                av_em_log(ic, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_em_get_name(st->codecpar->codec_id));
            }
        }

        /* check if the caller has overridden the codec id */
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        if (st->codec->codec_id != st->internal->orig_codec_id) {
            st->codecpar->codec_id   = st->codec->codec_id;
            st->codecpar->codec_type = st->codec->codec_type;
            st->internal->orig_codec_id = st->codec->codec_id;
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        if (st->codecpar->codec_id != st->internal->orig_codec_id)
            st->internal->orig_codec_id = st->codecpar->codec_id;

        ret = avcodec_em_parameters_to_context(avctx, st->codecpar);
        if (ret < 0)
            goto find_stream_info_err;
        if (st->request_probe <= 0)
            st->internal->avctx_inited = 1;

        codec = find_decoder(ic, st, st->codecpar->codec_id);

        /* Force thread count to 1 since the H.264 decoder will not extract
         * SPS and PPS to extradata during multi-threaded decoding. */
        av_em_dict_set(options ? &options[i] : &thread_opt, "threads", "1", 0);

        if (ic->codec_whitelist)
            av_em_dict_set(options ? &options[i] : &thread_opt, "codec_whitelist", ic->codec_whitelist, 0);

        /* Ensure that subtitle_header is properly set. */
        if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE
            && codec && !avctx->codec) {
            if (avcodec_em_open2(avctx, codec, options ? &options[i] : &thread_opt) < 0)
                av_em_log(ic, AV_LOG_WARNING,
                       "Failed to open codec in av_find_stream_info\n");
        }

        // Try to just open decoders, in case this is enough to get parameters.
        if (!has_codec_parameters(st, NULL) && st->request_probe <= 0) {
            if (codec && !avctx->codec)
                if (avcodec_em_open2(avctx, codec, options ? &options[i] : &thread_opt) < 0)
                    av_em_log(ic, AV_LOG_WARNING,
                           "Failed to open codec in av_find_stream_info\n");
        }
        if (!options)
            av_em_dict_free(&thread_opt);
    }
	//av_em_log(ic, AV_LOG_INFO, "2:nb_streams count:%d\n", ic->nb_streams);

    for (i = 0; i < ic->nb_streams; i++) {
#if FF_API_R_FRAME_RATE
        ic->streams[i]->info->last_dts = AV_NOPTS_VALUE;
#endif
        ic->streams[i]->info->fps_first_dts = AV_NOPTS_VALUE;
        ic->streams[i]->info->fps_last_dts  = AV_NOPTS_VALUE;
    }

    read_size = 0;
    for (;;) {
        int analyzed_all_streams;
        if (em_check_interrupt(&ic->interrupt_callback)) {
            ret = AVERROR_EXIT;
            av_em_log(ic, AV_LOG_DEBUG, "interrupted\n");
            break;
        }
		//av_em_log(ic, AV_LOG_INFO, "3:nb_streams count:%d\n", ic->nb_streams);

        /* check if one codec still needs to be handled */
        for (i = 0; i < ic->nb_streams; i++) {
            int fps_analyze_framecount = 20;

            st = ic->streams[i];
            if (!has_codec_parameters(st, NULL))
                break;
            /* If the timebase is coarse (like the usual millisecond precision
             * of mkv), we need to analyze more frames to reliably arrive at
             * the correct fps. */
            if (av_em_q2d(st->time_base) > 0.0005)
                fps_analyze_framecount *= 2;
            if (!tb_unreliable(st->internal->avctx))
                fps_analyze_framecount = 0;
            if (ic->fps_probe_size >= 0)
                fps_analyze_framecount = ic->fps_probe_size;
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                fps_analyze_framecount = 0;
            /* variable fps and no guess at the real fps */
            if (!(st->r_frame_rate.num && st->avg_frame_rate.num) &&
                st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                int count = (ic->iformat->flags & AVFMT_NOTIMESTAMPS) ?
                    st->info->codec_info_duration_fields/2 :
                    st->info->duration_count;
                if (count < fps_analyze_framecount)
                    break;
            }
            if (st->parser && st->parser->parser->split &&
                !st->codecpar->extradata)
                break;
            if (st->first_dts == AV_NOPTS_VALUE &&
                !(ic->iformat->flags & AVFMT_NOTIMESTAMPS) &&
                st->codec_info_nb_frames < ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) ? 1 : ic->max_ts_probe) &&
                (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                 st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))
                break;
        }
        analyzed_all_streams = 0;
        if (i == ic->nb_streams) {
            analyzed_all_streams = 1;
            /* NOTE: If the format has no header, then we need to read some
             * packets to get most of the streams, so we cannot stop here. */
            if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) {
                /* If we found the info for all the codecs, we can stop. */
                ret = count;
                av_em_log(ic, AV_LOG_DEBUG, "All info found\n");
                flush_codecs = 0;
                break;
            }
        }
        /* We did not get all the codec info, but we read too much data. */
        if (read_size >= probesize) {
            ret = count;
            av_em_log(ic, AV_LOG_DEBUG,
                   "Probe buffer size limit of %"PRId64" bytes reached\n", probesize);
            for (i = 0; i < ic->nb_streams; i++)
                if (!ic->streams[i]->r_frame_rate.num &&
                    ic->streams[i]->info->duration_count <= 1 &&
                    ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    strcmp(ic->iformat->name, "image2"))
                    av_em_log(ic, AV_LOG_WARNING,
                           "Stream #%d: not enough frames to estimate rate; "
                           "consider increasing probesize\n", i);
            break;
        }

        /* NOTE: A new stream can be added there if no header in file
         * (AVFMTCTX_NOHEADER). */
        ret = read_frame_internal(ic, &pkt1);
        if (ret == AVERROR(EAGAIN))
            continue;

        if (ret < 0) {
            /* EOF or error*/
            eof_reached = 1;
            break;
        }

        pkt = &pkt1;

        if (!(ic->flags & AVFMT_FLAG_NOBUFFER)) {
            ret = add_to_pktbuf(&ic->internal->packet_buffer, pkt,
                                &ic->internal->packet_buffer_end, 0);
            if (ret < 0)
                goto find_stream_info_err;
        }

        st = ic->streams[pkt->stream_index];
        if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            read_size += pkt->size;

        avctx = st->internal->avctx;
        if (!st->internal->avctx_inited) {
            ret = avcodec_em_parameters_to_context(avctx, st->codecpar);
            if (ret < 0)
                goto find_stream_info_err;
            st->internal->avctx_inited = 1;
        }

        if (pkt->dts != AV_NOPTS_VALUE && st->codec_info_nb_frames > 1) {
            /* check for non-increasing dts */
            if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
                st->info->fps_last_dts >= pkt->dts) {
                av_em_log(ic, AV_LOG_DEBUG,
                       "Non-increasing DTS in stream %d: packet %d with DTS "
                       "%"PRId64", packet %d with DTS %"PRId64"\n",
                       st->index, st->info->fps_last_dts_idx,
                       st->info->fps_last_dts, st->codec_info_nb_frames,
                       pkt->dts);
                st->info->fps_first_dts =
                st->info->fps_last_dts  = AV_NOPTS_VALUE;
            }
            /* Check for a discontinuity in dts. If the difference in dts
             * is more than 1000 times the average packet duration in the
             * sequence, we treat it as a discontinuity. */
            if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
                st->info->fps_last_dts_idx > st->info->fps_first_dts_idx &&
                (pkt->dts - st->info->fps_last_dts) / 1000 >
                (st->info->fps_last_dts     - st->info->fps_first_dts) /
                (st->info->fps_last_dts_idx - st->info->fps_first_dts_idx)) {
                av_em_log(ic, AV_LOG_WARNING,
                       "DTS discontinuity in stream %d: packet %d with DTS "
                       "%"PRId64", packet %d with DTS %"PRId64"\n",
                       st->index, st->info->fps_last_dts_idx,
                       st->info->fps_last_dts, st->codec_info_nb_frames,
                       pkt->dts);
                st->info->fps_first_dts =
                st->info->fps_last_dts  = AV_NOPTS_VALUE;
            }

            /* update stored dts values */
            if (st->info->fps_first_dts == AV_NOPTS_VALUE) {
                st->info->fps_first_dts     = pkt->dts;
                st->info->fps_first_dts_idx = st->codec_info_nb_frames;
            }
            st->info->fps_last_dts     = pkt->dts;
            st->info->fps_last_dts_idx = st->codec_info_nb_frames;
        }
        if (st->codec_info_nb_frames>1) {
            int64_t t = 0;
            int64_t limit;

            if (st->time_base.den > 0)
                t = av_em_rescale_q(st->info->codec_info_duration, st->time_base, AV_TIME_BASE_Q);
            if (st->avg_frame_rate.num > 0)
                t = FFMAX(t, av_em_rescale_q(st->codec_info_nb_frames, av_em_inv_q(st->avg_frame_rate), AV_TIME_BASE_Q));

            if (   t == 0
                && st->codec_info_nb_frames>30
                && st->info->fps_first_dts != AV_NOPTS_VALUE
                && st->info->fps_last_dts  != AV_NOPTS_VALUE)
                t = FFMAX(t, av_em_rescale_q(st->info->fps_last_dts - st->info->fps_first_dts, st->time_base, AV_TIME_BASE_Q));

            if (analyzed_all_streams)                                limit = max_analyze_duration;
            else if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) limit = max_subtitle_analyze_duration;
            else                                                     limit = max_stream_analyze_duration;

            if (t >= limit) {
                av_em_log(ic, AV_LOG_VERBOSE, "max_analyze_duration %"PRId64" reached at %"PRId64" microseconds st:%d\n",
                       limit,
                       t, pkt->stream_index);
                if (ic->flags & AVFMT_FLAG_NOBUFFER)
                    av_em_packet_unref(pkt);
                break;
            }
            if (pkt->duration) {
                if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE && pkt->pts != AV_NOPTS_VALUE && pkt->pts >= st->start_time) {
                    st->info->codec_info_duration = FFMIN(pkt->pts - st->start_time, st->info->codec_info_duration + pkt->duration);
                } else
                    st->info->codec_info_duration += pkt->duration;
                st->info->codec_info_duration_fields += st->parser && st->need_parsing && avctx->ticks_per_frame ==2 ? st->parser->repeat_pict + 1 : 2;
            }
        }
#if FF_API_R_FRAME_RATE
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            em_rfps_add_frame(ic, st, pkt->dts);
#endif
        if (st->parser && st->parser->parser->split && !avctx->extradata) {
            int i = st->parser->parser->split(avctx, pkt->data, pkt->size);
            if (i > 0 && i < FF_MAX_EXTRADATA_SIZE) {
                avctx->extradata_size = i;
                avctx->extradata      = av_em_mallocz(avctx->extradata_size +
                                                   AV_INPUT_BUFFER_PADDING_SIZE);
                if (!avctx->extradata)
                    return AVERROR(ENOMEM);
                memcpy(avctx->extradata, pkt->data,
                       avctx->extradata_size);
            }
        }

        /* If still no information, we try to open the codec and to
         * decompress the frame. We try to avoid that in most cases as
         * it takes longer and uses more memory. For MPEG-4, we need to
         * decompress for QuickTime.
         *
         * If AV_CODEC_CAP_CHANNEL_CONF is set this will force decoding of at
         * least one frame of codec data, this makes sure the codec initializes
         * the channel configuration and does not only trust the values from
         * the container. */
        try_decode_frame(ic, st, pkt,
                         (options && i < orig_nb_streams) ? &options[i] : NULL);

        if (ic->flags & AVFMT_FLAG_NOBUFFER)
            av_em_packet_unref(pkt);

        st->codec_info_nb_frames++;
        count++;
    }

    if (eof_reached) {
        int stream_index;
        for (stream_index = 0; stream_index < ic->nb_streams; stream_index++) {
            st = ic->streams[stream_index];
            avctx = st->internal->avctx;
            if (!has_codec_parameters(st, NULL)) {
                const AVEMCodec *codec = find_decoder(ic, st, st->codecpar->codec_id);
                if (codec && !avctx->codec) {
                    if (avcodec_em_open2(avctx, codec, (options && stream_index < orig_nb_streams) ? &options[stream_index] : NULL) < 0)
                        av_em_log(ic, AV_LOG_WARNING,
                            "Failed to open codec in av_find_stream_info\n");
                }
            }

            // EOF already reached while reading the stream above.
            // So continue with reoordering DTS with whatever delay we have.
            if (ic->internal->packet_buffer && !has_decode_delay_been_guessed(st)) {
                update_dts_from_pts(ic, stream_index, ic->internal->packet_buffer);
            }
        }
    }

    if (flush_codecs) {
        AVEMPacket empty_pkt = { 0 };
        int err = 0;
        av_em_init_packet(&empty_pkt);

        for (i = 0; i < ic->nb_streams; i++) {

            st = ic->streams[i];

            /* flush the decoders */
            if (st->info->found_decoder == 1) {
                do {
                    err = try_decode_frame(ic, st, &empty_pkt,
                                            (options && i < orig_nb_streams)
                                            ? &options[i] : NULL);
                } while (err > 0 && !has_codec_parameters(st, NULL));

                if (err < 0) {
                    av_em_log(ic, AV_LOG_INFO,
                        "decoding for stream %d failed\n", st->index);
                }
            }
        }
    }

    // close codecs which were opened in try_decode_frame()
    //av_em_log(ic, AV_LOG_INFO, "4:nb_streams count:%d\n", ic->nb_streams);
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        avcodec_em_close(st->internal->avctx);
    }

    em_rfps_calculate(ic);
	//av_em_log(ic, AV_LOG_INFO, "5:nb_streams count:%d\n", ic->nb_streams);

    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        avctx = st->internal->avctx;
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (avctx->codec_id == AV_CODEC_ID_RAWVIDEO && !avctx->codec_tag && !avctx->bits_per_coded_sample) {
                uint32_t tag= avcodec_em_pix_fmt_to_codec_tag(avctx->pix_fmt);
                if (avpriv_em_find_pix_fmt(avpriv_em_get_raw_pix_fmt_tags(), tag) == avctx->pix_fmt)
                    avctx->codec_tag= tag;
            }

            /* estimate average framerate if not set by demuxer */
            if (st->info->codec_info_duration_fields &&
                !st->avg_frame_rate.num &&
                st->info->codec_info_duration) {
                int best_fps      = 0;
                double best_error = 0.01;

                if (st->info->codec_info_duration        >= INT64_MAX / st->time_base.num / 2||
                    st->info->codec_info_duration_fields >= INT64_MAX / st->time_base.den ||
                    st->info->codec_info_duration        < 0)
                    continue;
                av_em_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                          st->info->codec_info_duration_fields * (int64_t) st->time_base.den,
                          st->info->codec_info_duration * 2 * (int64_t) st->time_base.num, 60000);

                /* Round guessed framerate to a "standard" framerate if it's
                 * within 1% of the original estimate. */
                for (j = 0; j < MAX_STD_TIMEBASES; j++) {
                    AVEMRational std_fps = { get_std_framerate(j), 12 * 1001 };
                    double error       = fabs(av_em_q2d(st->avg_frame_rate) /
                                              av_em_q2d(std_fps) - 1);

                    if (error < best_error) {
                        best_error = error;
                        best_fps   = std_fps.num;
                    }
                }
                if (best_fps)
                    av_em_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                              best_fps, 12 * 1001, INT_MAX);
            }

            if (!st->r_frame_rate.num) {
                if (    avctx->time_base.den * (int64_t) st->time_base.num
                    <= avctx->time_base.num * avctx->ticks_per_frame * (int64_t) st->time_base.den) {
                    st->r_frame_rate.num = avctx->time_base.den;
                    st->r_frame_rate.den = avctx->time_base.num * avctx->ticks_per_frame;
                } else {
                    st->r_frame_rate.num = st->time_base.den;
                    st->r_frame_rate.den = st->time_base.num;
                }
            }
            if (st->display_aspect_ratio.num && st->display_aspect_ratio.den) {
                AVEMRational hw_ratio = { avctx->height, avctx->width };
                st->sample_aspect_ratio = av_em_mul_q(st->display_aspect_ratio,
                                                   hw_ratio);
            }
        } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!avctx->bits_per_coded_sample)
                avctx->bits_per_coded_sample =
                    av_em_get_bits_per_sample(avctx->codec_id);
            // set stream disposition based on audio service type
            switch (avctx->audio_service_type) {
            case AV_AUDIO_SERVICE_TYPE_EFFECTS:
                st->disposition = AV_DISPOSITION_CLEAN_EFFECTS;
                break;
            case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED:
                st->disposition = AV_DISPOSITION_VISUAL_IMPAIRED;
                break;
            case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:
                st->disposition = AV_DISPOSITION_HEARING_IMPAIRED;
                break;
            case AV_AUDIO_SERVICE_TYPE_COMMENTARY:
                st->disposition = AV_DISPOSITION_COMMENT;
                break;
            case AV_AUDIO_SERVICE_TYPE_KARAOKE:
                st->disposition = AV_DISPOSITION_KARAOKE;
                break;
            }
        }
    }

    if (probesize)
        estimate_timings(ic, old_offset);

    av_em_opt_set(ic, "skip_clear", "0", AV_OPT_SEARCH_CHILDREN);

    if (ret >= 0 && ic->nb_streams)
        /* We could not have all the codec parameters before EOF. */
        ret = -1;
    for (i = 0; i < ic->nb_streams; i++) {
        const char *errmsg;
        st = ic->streams[i];

        /* if no packet was ever seen, update context now for has_codec_parameters */
        if (!st->internal->avctx_inited) {
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                st->codecpar->format == AV_SAMPLE_FMT_NONE)
                st->codecpar->format = st->internal->avctx->sample_fmt;
            ret = avcodec_em_parameters_to_context(st->internal->avctx, st->codecpar);
            if (ret < 0)
                goto find_stream_info_err;
        }
        if (!has_codec_parameters(st, &errmsg)) {
            char buf[256];
            avcodec_em_string(buf, sizeof(buf), st->internal->avctx, 0);
            av_em_log(ic, AV_LOG_WARNING,
                   "Could not find codec parameters for stream %d (%s): %s\n"
                   "Consider increasing the value for the 'analyzeduration' and 'probesize' options\n",
                   i, buf, errmsg);
        } else {
            ret = 0;
        }
    }

    compute_chapters_end(ic);

    /* update the stream parameters from the internal codec contexts */
		//av_em_log(ic, AV_LOG_INFO, "6:nb_streams count:%d\n", ic->nb_streams);
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];

        if (st->internal->avctx_inited) {
            int orig_w = st->codecpar->width;
            int orig_h = st->codecpar->height;
            ret = avcodec_em_parameters_from_context(st->codecpar, st->internal->avctx);
            if (ret < 0)
                goto find_stream_info_err;
            // The decoder might reduce the video size by the lowres factor.
            if (av_em_codec_get_lowres(st->internal->avctx) && orig_w) {
                st->codecpar->width = orig_w;
                st->codecpar->height = orig_h;
            }
        }

#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        ret = avcodec_em_parameters_to_context(st->codec, st->codecpar);
        if (ret < 0)
            goto find_stream_info_err;

        // The old API (AVEMStream.codec) "requires" the resolution to be adjusted
        // by the lowres factor.
        if (av_em_codec_get_lowres(st->internal->avctx) && st->internal->avctx->width) {
            av_em_codec_set_lowres(st->codec, av_em_codec_get_lowres(st->internal->avctx));
            st->codec->width = st->internal->avctx->width;
            st->codec->height = st->internal->avctx->height;
        }

        if (st->codec->codec_tag != MKTAG('t','m','c','d'))
            st->codec->time_base = st->internal->avctx->time_base;
        st->codec->framerate = st->avg_frame_rate;

        if (st->internal->avctx->subtitle_header) {
            st->codec->subtitle_header = av_em_alloc(st->internal->avctx->subtitle_header_size);
            if (!st->codec->subtitle_header)
                goto find_stream_info_err;
            st->codec->subtitle_header_size = st->internal->avctx->subtitle_header_size;
            memcpy(st->codec->subtitle_header, st->internal->avctx->subtitle_header,
                   st->codec->subtitle_header_size);
        }

        // Fields unavailable in AVEMCodecParameters
        st->codec->coded_width = st->internal->avctx->coded_width;
        st->codec->coded_height = st->internal->avctx->coded_height;
        st->codec->properties = st->internal->avctx->properties;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        st->internal->avctx_inited = 0;
    }

find_stream_info_err:
	//av_em_log(ic, AV_LOG_INFO, "7:nb_streams count:%d\n", ic->nb_streams);
    for (i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->info)
            av_em_freep(&st->info->duration_error);
        av_em_freep(&ic->streams[i]->info);
    }
    if (ic->pb)
        av_em_log(ic, AV_LOG_DEBUG, "After avformat_em_find_stream_info() pos: %"PRId64" bytes read:%"PRId64" seeks:%d frames:%d\n",
               avio_em_tell(ic->pb), ic->pb->bytes_read, ic->pb->seek_count, count);
    return ret;
}

AVEMProgram *av_em_find_program_from_stream(AVEMFormatContext *ic, AVEMProgram *last, int s)
{
    int i, j;

    for (i = 0; i < ic->nb_programs; i++) {
        if (ic->programs[i] == last) {
            last = NULL;
        } else {
            if (!last)
                for (j = 0; j < ic->programs[i]->nb_stream_indexes; j++)
                    if (ic->programs[i]->stream_index[j] == s)
                        return ic->programs[i];
        }
    }
    return NULL;
}

int av_em_find_best_stream(AVEMFormatContext *ic, enum AVEMMediaType type,
                        int wanted_stream_nb, int related_stream,
                        AVEMCodec **decoder_ret, int flags)
{
    int i, nb_streams = ic->nb_streams;
    int ret = AVERROR_STREAM_NOT_FOUND, best_count = -1, best_bitrate = -1, best_multiframe = -1, count, bitrate, multiframe;
    unsigned *program = NULL;
    const AVEMCodec *decoder = NULL, *best_decoder = NULL;

    if (related_stream >= 0 && wanted_stream_nb < 0) {
        AVEMProgram *p = av_em_find_program_from_stream(ic, NULL, related_stream);
        if (p) {
            program    = p->stream_index;
            nb_streams = p->nb_stream_indexes;
        }
    }
    for (i = 0; i < nb_streams; i++) {
        int real_stream_index = program ? program[i] : i;
        AVEMStream *st          = ic->streams[real_stream_index];
        AVEMCodecParameters *par = st->codecpar;
        if (par->codec_type != type)
            continue;
        if (wanted_stream_nb >= 0 && real_stream_index != wanted_stream_nb)
            continue;
        if (wanted_stream_nb != real_stream_index &&
            st->disposition & (AV_DISPOSITION_HEARING_IMPAIRED |
                               AV_DISPOSITION_VISUAL_IMPAIRED))
            continue;
        if (type == AVMEDIA_TYPE_AUDIO && !(par->channels && par->sample_rate))
            continue;
        if (decoder_ret) {
            decoder = find_decoder(ic, st, par->codec_id);
            if (!decoder) {
                if (ret < 0)
                    ret = AVERROR_DECODER_NOT_FOUND;
                continue;
            }
        }
        count = st->codec_info_nb_frames;
        bitrate = par->bit_rate;
        multiframe = FFMIN(5, count);
        if ((best_multiframe >  multiframe) ||
            (best_multiframe == multiframe && best_bitrate >  bitrate) ||
            (best_multiframe == multiframe && best_bitrate == bitrate && best_count >= count))
            continue;
        best_count   = count;
        best_bitrate = bitrate;
        best_multiframe = multiframe;
        ret          = real_stream_index;
        best_decoder = decoder;
        if (program && i == nb_streams - 1 && ret < 0) {
            program    = NULL;
            nb_streams = ic->nb_streams;
            /* no related stream found, try again with everything */
            i = 0;
        }
    }
    if (decoder_ret)
        *decoder_ret = (AVEMCodec*)best_decoder;
    return ret;
}

/*******************************************************/

int av_em_read_play(AVEMFormatContext *s)
{
    if (s->iformat->read_play)
        return s->iformat->read_play(s);
    if (s->pb)
        return avio_em_pause(s->pb, 0);
    return AVERROR(ENOSYS);
}

int av_em_read_pause(AVEMFormatContext *s)
{
    if (s->iformat->read_pause)
        return s->iformat->read_pause(s);
    if (s->pb)
        return avio_em_pause(s->pb, 1);
    return AVERROR(ENOSYS);
}

static void free_stream(AVEMStream **pst)
{
    AVEMStream *st = *pst;
    int i;

    if (!st)
        return;

    for (i = 0; i < st->nb_side_data; i++)
        av_em_freep(&st->side_data[i].data);
    av_em_freep(&st->side_data);

    if (st->parser)
        av_em_parser_close(st->parser);

    if (st->attached_pic.data)
        av_em_packet_unref(&st->attached_pic);

    if (st->internal) {
        avcodec_em_free_context(&st->internal->avctx);
        for (i = 0; i < st->internal->nb_bsfcs; i++) {
            av_em_bsf_free(&st->internal->bsfcs[i]);
            av_em_freep(&st->internal->bsfcs);
        }
    }
    av_em_freep(&st->internal);

    av_em_dict_free(&st->metadata);
    avcodec_em_parameters_free(&st->codecpar);
    av_em_freep(&st->probe_data.buf);
    av_em_freep(&st->index_entries);
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    av_em_freep(&st->codec->extradata);
    av_em_freep(&st->codec->subtitle_header);
    av_em_freep(&st->codec);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    av_em_freep(&st->priv_data);
    if (st->info)
        av_em_freep(&st->info->duration_error);
    av_em_freep(&st->info);
    av_em_freep(&st->recommended_encoder_configuration);
    av_em_freep(&st->priv_pts);

    av_em_freep(pst);
}

void em_free_stream(AVEMFormatContext *s, AVEMStream *st)
{
    av_assert0(s->nb_streams>0);
    av_assert0(s->streams[ s->nb_streams - 1 ] == st);

    free_stream(&s->streams[ --s->nb_streams ]);
}

void avformat_em_free_context(AVEMFormatContext *s)
{
    int i;

    if (!s)
        return;

    av_em_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_em_opt_free(s->priv_data);
    if (s->oformat && s->oformat->priv_class && s->priv_data)
        av_em_opt_free(s->priv_data);

    for (i = s->nb_streams - 1; i >= 0; i--)
        em_free_stream(s, s->streams[i]);


    for (i = s->nb_programs - 1; i >= 0; i--) {
        av_em_dict_free(&s->programs[i]->metadata);
        av_em_freep(&s->programs[i]->stream_index);
        av_em_freep(&s->programs[i]);
    }
    av_em_freep(&s->programs);
    av_em_freep(&s->priv_data);
    while (s->nb_chapters--) {
        av_em_dict_free(&s->chapters[s->nb_chapters]->metadata);
        av_em_freep(&s->chapters[s->nb_chapters]);
    }
    av_em_freep(&s->chapters);
    av_em_dict_free(&s->metadata);
    av_em_freep(&s->streams);
    av_em_freep(&s->internal);
    flush_packet_queue(s);
    av_em_free(s);
}

void avformat_em_close_input(AVEMFormatContext **ps)
{
    AVEMFormatContext *s;
    AVEMIOContext *pb;

    if (!ps || !*ps)
        return;

    s  = *ps;
    pb = s->pb;

    if ((s->iformat && strcmp(s->iformat->name, "image2") && s->iformat->flags & AVFMT_NOFILE) ||
        (s->flags & AVFMT_FLAG_CUSTOM_IO))
        pb = NULL;

    flush_packet_queue(s);

    if (s->iformat)
        if (s->iformat->read_close)
            s->iformat->read_close(s);

    avformat_em_free_context(s);

    *ps = NULL;

    avio_em_close(pb);
}

AVEMStream *avformat_em_new_stream(AVEMFormatContext *s, const AVEMCodec *c)
{
    AVEMStream *st;
    int i;
    AVEMStream **streams;

    if (s->nb_streams >= INT_MAX/sizeof(*streams))
        return NULL;
    streams = av_em_realloc_array(s->streams, s->nb_streams + 1, sizeof(*streams));
    if (!streams)
        return NULL;
    s->streams = streams;

    st = av_em_mallocz(sizeof(AVEMStream));
    if (!st)
        return NULL;
    if (!(st->info = av_em_mallocz(sizeof(*st->info)))) {
        av_em_free(st);
        return NULL;
    }
    st->info->last_dts = AV_NOPTS_VALUE;

#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    st->codec = avcodec_em_alloc_context3(c);
    if (!st->codec) {
        av_em_free(st->info);
        av_em_free(st);
        return NULL;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    st->internal = av_em_mallocz(sizeof(*st->internal));
    if (!st->internal)
        goto fail;

    st->codecpar = avcodec_em_parameters_alloc();
    if (!st->codecpar)
        goto fail;

    st->internal->avctx = avcodec_em_alloc_context3(NULL);
    if (!st->internal->avctx)
        goto fail;

    if (s->iformat) {
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        /* no default bitrate if decoding */
        st->codec->bit_rate = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        /* default pts setting is MPEG-like */
        avpriv_em_set_pts_info(st, 33, 1, 90000);
        /* we set the current DTS to 0 so that formats without any timestamps
         * but durations get some timestamps, formats with some unknown
         * timestamps have their first few packets buffered and the
         * timestamps corrected before they are returned to the user */
        st->cur_dts = RELATIVE_TS_BASE;
    } else {
        st->cur_dts = AV_NOPTS_VALUE;
    }

    st->index      = s->nb_streams;
    st->start_time = AV_NOPTS_VALUE;
    st->duration   = AV_NOPTS_VALUE;
    st->first_dts     = AV_NOPTS_VALUE;
    st->probe_packets = MAX_PROBE_PACKETS;
    st->pts_wrap_reference = AV_NOPTS_VALUE;
    st->pts_wrap_behavior = AV_PTS_WRAP_IGNORE;

    st->last_IP_pts = AV_NOPTS_VALUE;
    st->last_dts_for_order_check = AV_NOPTS_VALUE;
    for (i = 0; i < MAX_REORDER_DELAY + 1; i++)
        st->pts_buffer[i] = AV_NOPTS_VALUE;

    st->sample_aspect_ratio = (AVEMRational) { 0, 1 };

#if FF_API_R_FRAME_RATE
    st->info->last_dts      = AV_NOPTS_VALUE;
#endif
    st->info->fps_first_dts = AV_NOPTS_VALUE;
    st->info->fps_last_dts  = AV_NOPTS_VALUE;

    st->inject_global_side_data = s->internal->inject_global_side_data;

    st->internal->need_context_update = 1;

    s->streams[s->nb_streams++] = st;
    return st;
fail:
    free_stream(&st);
    return NULL;
}

AVEMProgram *av_em_new_program(AVEMFormatContext *ac, int id)
{
    AVEMProgram *program = NULL;
    int i;

    av_em_log(ac, AV_LOG_TRACE, "new_program: id=0x%04x\n", id);

    for (i = 0; i < ac->nb_programs; i++)
        if (ac->programs[i]->id == id)
            program = ac->programs[i];

    if (!program) {
        program = av_em_mallocz(sizeof(AVEMProgram));
        if (!program)
            return NULL;
        dynarray_add(&ac->programs, &ac->nb_programs, program);
        program->discard = AVDISCARD_NONE;
    }
    program->id = id;
    program->pts_wrap_reference = AV_NOPTS_VALUE;
    program->pts_wrap_behavior = AV_PTS_WRAP_IGNORE;

    program->start_time =
    program->end_time   = AV_NOPTS_VALUE;

    return program;
}

AVEMChapter *avpriv_em_new_chapter(AVEMFormatContext *s, int id, AVEMRational time_base,
                              int64_t start, int64_t end, const char *title)
{
    AVEMChapter *chapter = NULL;
    int i;

    if (end != AV_NOPTS_VALUE && start > end) {
        av_em_log(s, AV_LOG_ERROR, "Chapter end time %"PRId64" before start %"PRId64"\n", end, start);
        return NULL;
    }

    for (i = 0; i < s->nb_chapters; i++)
        if (s->chapters[i]->id == id)
            chapter = s->chapters[i];

    if (!chapter) {
        chapter = av_em_mallocz(sizeof(AVEMChapter));
        if (!chapter)
            return NULL;
        dynarray_add(&s->chapters, &s->nb_chapters, chapter);
    }
    av_em_dict_set(&chapter->metadata, "title", title, 0);
    chapter->id        = id;
    chapter->time_base = time_base;
    chapter->start     = start;
    chapter->end       = end;

    return chapter;
}

void av_em_program_add_stream_index(AVEMFormatContext *ac, int progid, unsigned idx)
{
    int i, j;
    AVEMProgram *program = NULL;
    void *tmp;

    if (idx >= ac->nb_streams) {
        av_em_log(ac, AV_LOG_ERROR, "stream index %d is not valid\n", idx);
        return;
    }

    for (i = 0; i < ac->nb_programs; i++) {
        if (ac->programs[i]->id != progid)
            continue;
        program = ac->programs[i];
        for (j = 0; j < program->nb_stream_indexes; j++)
            if (program->stream_index[j] == idx)
                return;

        tmp = av_em_realloc_array(program->stream_index, program->nb_stream_indexes+1, sizeof(unsigned int));
        if (!tmp)
            return;
        program->stream_index = tmp;
        program->stream_index[program->nb_stream_indexes++] = idx;
        return;
    }
}

uint64_t em_ntp_time(void)
{
    return (av_em_gettime() / 1000) * 1000 + NTP_OFFSET_US;
}

int av_em_get_frame_filename(char *buf, int buf_size, const char *path, int number)
{
    const char *p;
    char *q, buf1[20], c;
    int nd, len, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for (;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            do {
                nd = 0;
                while (av_isdigit(*p))
                    nd = nd * 10 + *p++ - '0';
                c = *p++;
            } while (av_isdigit(c));

            switch (c) {
            case '%':
                goto addchar;
            case 'd':
                if (percentd_found)
                    goto fail;
                percentd_found = 1;
                if (number < 0)
                    nd += 1;
                snprintf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } else {
addchar:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
fail:
    *q = '\0';
    return -1;
}

void av_em_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr, char *path, int path_size, const char *url)
{
    const char *p, *ls, *ls2, *at, *at2, *col, *brk;

    if (port_ptr)
        *port_ptr = -1;
    if (proto_size > 0)
        proto[0] = 0;
    if (authorization_size > 0)
        authorization[0] = 0;
    if (hostname_size > 0)
        hostname[0] = 0;
    if (path_size > 0)
        path[0] = 0;

    /* parse protocol */
    if ((p = strchr(url, ':'))) {
        av_em_strlcpy(proto, url, FFMIN(proto_size, p + 1 - url));
        p++; /* skip ':' */
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
    } else {
        /* no protocol means plain filename */
        av_em_strlcpy(path, url, path_size);
        return;
    }

    /* separate path from hostname */
    ls = strchr(p, '/');
    ls2 = strchr(p, '?');
    if (!ls)
        ls = ls2;
    else if (ls && ls2)
        ls = FFMIN(ls, ls2);
    if (ls)
        av_em_strlcpy(path, ls, path_size);
    else
        ls = &p[strlen(p)];  // XXX

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p) {
        /* authorization (user[:pass]@hostname) */
        at2 = p;
        while ((at = strchr(p, '@')) && at < ls) {
            av_em_strlcpy(authorization, at2,
                       FFMIN(authorization_size, at + 1 - at2));
            p = at + 1; /* skip '@' */
        }

        if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
            /* [host]:port */
            av_em_strlcpy(hostname, p + 1,
                       FFMIN(hostname_size, brk - p));
            if (brk[1] == ':' && port_ptr)
                *port_ptr = atoi(brk + 2);
        } else if ((col = strchr(p, ':')) && col < ls) {
            av_em_strlcpy(hostname, p,
                       FFMIN(col + 1 - p, hostname_size));
            if (port_ptr)
                *port_ptr = atoi(col + 1);
        } else
            av_em_strlcpy(hostname, p,
                       FFMIN(ls + 1 - p, hostname_size));
    }
}

char *em_data_to_hex(char *buff, const uint8_t *src, int s, int lowercase)
{
    int i;
    static const char hex_table_uc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'A', 'B',
                                           'C', 'D', 'E', 'F' };
    static const char hex_table_lc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'a', 'b',
                                           'c', 'd', 'e', 'f' };
    const char *hex_table = lowercase ? hex_table_lc : hex_table_uc;

    for (i = 0; i < s; i++) {
        buff[i * 2]     = hex_table[src[i] >> 4];
        buff[i * 2 + 1] = hex_table[src[i] & 0xF];
    }

    return buff;
}

int em_hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v   = 1;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;
        c = av_toupper((unsigned char) *p++);
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            break;
        v = (v << 4) | c;
        if (v & 0x100) {
            if (data)
                data[len] = v;
            len++;
            v = 1;
        }
    }
    return len;
}

void avpriv_em_set_pts_info(AVEMStream *s, int pts_wrap_bits,
                         unsigned int pts_num, unsigned int pts_den)
{
    AVEMRational new_tb;
    if (av_em_reduce(&new_tb.num, &new_tb.den, pts_num, pts_den, INT_MAX)) {
        if (new_tb.num != pts_num)
            av_em_log(NULL, AV_LOG_DEBUG,
                   "st:%d removing common factor %d from timebase\n",
                   s->index, pts_num / new_tb.num);
    } else
        av_em_log(NULL, AV_LOG_WARNING,
               "st:%d has too large timebase, reducing\n", s->index);

    if (new_tb.num <= 0 || new_tb.den <= 0) {
        av_em_log(NULL, AV_LOG_ERROR,
               "Ignoring attempt to set invalid timebase %d/%d for st:%d\n",
               new_tb.num, new_tb.den,
               s->index);
        return;
    }
    s->time_base     = new_tb;
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    av_em_codec_set_pkt_timebase(s->codec, new_tb);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    av_em_codec_set_pkt_timebase(s->internal->avctx, new_tb);
    s->pts_wrap_bits = pts_wrap_bits;
}

void em_parse_key_value(const char *str, em_parse_key_val_cb callback_get_buf,
                        void *context)
{
    const char *ptr = str;

    /* Parse key=value pairs. */
    for (;;) {
        const char *key;
        char *dest = NULL, *dest_end;
        int key_len, dest_len = 0;

        /* Skip whitespace and potential commas. */
        while (*ptr && (av_isspace(*ptr) || *ptr == ','))
            ptr++;
        if (!*ptr)
            break;

        key = ptr;

        if (!(ptr = strchr(key, '=')))
            break;
        ptr++;
        key_len = ptr - key;

        callback_get_buf(context, key, key_len, &dest, &dest_len);
        dest_end = dest + dest_len - 1;

        if (*ptr == '\"') {
            ptr++;
            while (*ptr && *ptr != '\"') {
                if (*ptr == '\\') {
                    if (!ptr[1])
                        break;
                    if (dest && dest < dest_end)
                        *dest++ = ptr[1];
                    ptr += 2;
                } else {
                    if (dest && dest < dest_end)
                        *dest++ = *ptr;
                    ptr++;
                }
            }
            if (*ptr == '\"')
                ptr++;
        } else {
            for (; *ptr && !(av_isspace(*ptr) || *ptr == ','); ptr++)
                if (dest && dest < dest_end)
                    *dest++ = *ptr;
        }
        if (dest)
            *dest = 0;
    }
}

int em_find_stream_index(AVEMFormatContext *s, int id)
{
    int i;
    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == id)
            return i;
    return -1;
}

int avformat_em_query_codec(const AVEMOutputFormat *ofmt, enum AVEMCodecID codec_id,
                         int std_compliance)
{
    if (ofmt) {
        unsigned int codec_tag;
        if (ofmt->query_codec)
            return ofmt->query_codec(codec_id, std_compliance);
        else if (ofmt->codec_tag)
            return !!av_em_codec_get_tag2(ofmt->codec_tag, codec_id, &codec_tag);
        else if (codec_id == ofmt->video_codec ||
                 codec_id == ofmt->audio_codec ||
                 codec_id == ofmt->subtitle_codec)
            return 1;
    }
    return AVERROR_PATCHWELCOME;
}

int avformat_em_network_init(void)
{
#if CONFIG_NETWORK
    int ret;
    em_network_inited_globally = 1;
    if ((ret = em_network_init()) < 0)
        return ret;
    if ((ret = em_tls_init()) < 0)
        return ret;
#endif
    return 0;
}

int avformat_em_network_deinit(void)
{
#if CONFIG_NETWORK
    em_network_close();
    em_tls_deinit();
    em_network_inited_globally = 0;
#endif
    return 0;
}

int em_add_param_change(AVEMPacket *pkt, int32_t channels,
                        uint64_t channel_layout, int32_t sample_rate,
                        int32_t width, int32_t height)
{
    uint32_t flags = 0;
    int size = 4;
    uint8_t *data;
    if (!pkt)
        return AVERROR(EINVAL);
    if (channels) {
        size  += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT;
    }
    if (channel_layout) {
        size  += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT;
    }
    if (sample_rate) {
        size  += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE;
    }
    if (width || height) {
        size  += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS;
    }
    data = av_em_packet_new_side_data(pkt, AV_PKT_DATA_PARAM_CHANGE, size);
    if (!data)
        return AVERROR(ENOMEM);
    bytestream_put_le32(&data, flags);
    if (channels)
        bytestream_put_le32(&data, channels);
    if (channel_layout)
        bytestream_put_le64(&data, channel_layout);
    if (sample_rate)
        bytestream_put_le32(&data, sample_rate);
    if (width || height) {
        bytestream_put_le32(&data, width);
        bytestream_put_le32(&data, height);
    }
    return 0;
}

AVEMRational av_em_guess_sample_aspect_ratio(AVEMFormatContext *format, AVEMStream *stream, AVFrame *frame)
{
    AVEMRational undef = {0, 1};
    AVEMRational stream_sample_aspect_ratio = stream ? stream->sample_aspect_ratio : undef;
    AVEMRational codec_sample_aspect_ratio  = stream && stream->codecpar ? stream->codecpar->sample_aspect_ratio : undef;
    AVEMRational frame_sample_aspect_ratio  = frame  ? frame->sample_aspect_ratio  : codec_sample_aspect_ratio;

    av_em_reduce(&stream_sample_aspect_ratio.num, &stream_sample_aspect_ratio.den,
               stream_sample_aspect_ratio.num,  stream_sample_aspect_ratio.den, INT_MAX);
    if (stream_sample_aspect_ratio.num <= 0 || stream_sample_aspect_ratio.den <= 0)
        stream_sample_aspect_ratio = undef;

    av_em_reduce(&frame_sample_aspect_ratio.num, &frame_sample_aspect_ratio.den,
               frame_sample_aspect_ratio.num,  frame_sample_aspect_ratio.den, INT_MAX);
    if (frame_sample_aspect_ratio.num <= 0 || frame_sample_aspect_ratio.den <= 0)
        frame_sample_aspect_ratio = undef;

    if (stream_sample_aspect_ratio.num)
        return stream_sample_aspect_ratio;
    else
        return frame_sample_aspect_ratio;
}

AVEMRational av_em_guess_frame_rate(AVEMFormatContext *format, AVEMStream *st, AVFrame *frame)
{
    AVEMRational fr = st->r_frame_rate;
    AVEMRational codec_fr = st->internal->avctx->framerate;
    AVEMRational   avg_fr = st->avg_frame_rate;

    if (avg_fr.num > 0 && avg_fr.den > 0 && fr.num > 0 && fr.den > 0 &&
        av_em_q2d(avg_fr) < 70 && av_em_q2d(fr) > 210) {
        fr = avg_fr;
    }


    if (st->internal->avctx->ticks_per_frame > 1) {
        if (   codec_fr.num > 0 && codec_fr.den > 0 &&
            (fr.num == 0 || av_em_q2d(codec_fr) < av_em_q2d(fr)*0.7 && fabs(1.0 - av_em_q2d(av_em_div_q(avg_fr, fr))) > 0.1))
            fr = codec_fr;
    }

    return fr;
}

int avformat_em_match_stream_specifier(AVEMFormatContext *s, AVEMStream *st,
                                    const char *spec)
{
    if (*spec <= '9' && *spec >= '0') /* opt:index */
        return strtol(spec, NULL, 0) == st->index;
    else if (*spec == 'v' || *spec == 'a' || *spec == 's' || *spec == 'd' ||
             *spec == 't' || *spec == 'V') { /* opt:[vasdtV] */
        enum AVEMMediaType type;
        int nopic = 0;

        switch (*spec++) {
        case 'v': type = AVMEDIA_TYPE_VIDEO;      break;
        case 'a': type = AVMEDIA_TYPE_AUDIO;      break;
        case 's': type = AVMEDIA_TYPE_SUBTITLE;   break;
        case 'd': type = AVMEDIA_TYPE_DATA;       break;
        case 't': type = AVMEDIA_TYPE_ATTACHMENT; break;
        case 'V': type = AVMEDIA_TYPE_VIDEO; nopic = 1; break;
        default:  av_assert0(0);
        }
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        if (type != st->codecpar->codec_type
           && (st->codecpar->codec_type != AVMEDIA_TYPE_UNKNOWN || st->codec->codec_type != type))
            return 0;
FF_ENABLE_DEPRECATION_WARNINGS
#else
        if (type != st->codecpar->codec_type)
            return 0;
#endif
        if (nopic && (st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return 0;
        if (*spec++ == ':') { /* possibly followed by :index */
            int i, index = strtol(spec, NULL, 0);
            for (i = 0; i < s->nb_streams; i++) {
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
                if ((s->streams[i]->codecpar->codec_type == type
                      || s->streams[i]->codec->codec_type == type
                    ) &&
                    !(nopic && (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) &&
                    index-- == 0)
                    return i == st->index;
FF_ENABLE_DEPRECATION_WARNINGS
#else
                if ((s->streams[i]->codecpar->codec_type == type) &&
                    !(nopic && (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) &&
                    index-- == 0)
                    return i == st->index;
#endif
            }
            return 0;
        }
        return 1;
    } else if (*spec == 'p' && *(spec + 1) == ':') {
        int prog_id, i, j;
        char *endptr;
        spec += 2;
        prog_id = strtol(spec, &endptr, 0);
        for (i = 0; i < s->nb_programs; i++) {
            if (s->programs[i]->id != prog_id)
                continue;

            if (*endptr++ == ':') {
                int stream_idx = strtol(endptr, NULL, 0);
                return stream_idx >= 0 &&
                    stream_idx < s->programs[i]->nb_stream_indexes &&
                    st->index == s->programs[i]->stream_index[stream_idx];
            }

            for (j = 0; j < s->programs[i]->nb_stream_indexes; j++)
                if (st->index == s->programs[i]->stream_index[j])
                    return 1;
        }
        return 0;
    } else if (*spec == '#' ||
               (*spec == 'i' && *(spec + 1) == ':')) {
        int stream_id;
        char *endptr;
        spec += 1 + (*spec == 'i');
        stream_id = strtol(spec, &endptr, 0);
        if (!*endptr)
            return stream_id == st->id;
    } else if (*spec == 'm' && *(spec + 1) == ':') {
        AVEMDictionaryEntry *tag;
        char *key, *val;
        int ret;

        spec += 2;
        val = strchr(spec, ':');

        key = val ? av_em_strndup(spec, val - spec) : av_em_strdup(spec);
        if (!key)
            return AVERROR(ENOMEM);

        tag = av_em_dict_get(st->metadata, key, NULL, 0);
        if (tag) {
            if (!val || !strcmp(tag->value, val + 1))
                ret = 1;
            else
                ret = 0;
        } else
            ret = 0;

        av_em_freep(&key);
        return ret;
    } else if (*spec == 'u') {
        AVEMCodecParameters *par = st->codecpar;
#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        AVEMCodecContext *codec = st->codec;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        int val;
        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            val = par->sample_rate && par->channels;
#if FF_API_LAVF_AVCTX
            val = val || (codec->sample_rate && codec->channels);
#endif
            if (par->format == AV_SAMPLE_FMT_NONE
#if FF_API_LAVF_AVCTX
                && codec->sample_fmt == AV_SAMPLE_FMT_NONE
#endif
                )
                return 0;
            break;
        case AVMEDIA_TYPE_VIDEO:
            val = par->width && par->height;
#if FF_API_LAVF_AVCTX
            val = val || (codec->width && codec->height);
#endif
            if (par->format == AV_PIX_FMT_NONE
#if FF_API_LAVF_AVCTX
                && codec->pix_fmt == AV_PIX_FMT_NONE
#endif
                )
                return 0;
            break;
        case AVMEDIA_TYPE_UNKNOWN:
            val = 0;
            break;
        default:
            val = 1;
            break;
        }
#if FF_API_LAVF_AVCTX
        return (par->codec_id != AV_CODEC_ID_NONE || codec->codec_id != AV_CODEC_ID_NONE) && val != 0;
#else
        return par->codec_id != AV_CODEC_ID_NONE && val != 0;
#endif
    } else if (!*spec) /* empty specifier, matches everything */
        return 1;

    av_em_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return AVERROR(EINVAL);
}

int em_generate_avci_extradata(AVEMStream *st)
{
    static const uint8_t avci100_1080p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x02, 0x56, 0xb9, 0x3d, 0x7d, 0x7e,
        0x4f, 0xe3, 0x3f, 0x11, 0xf1, 0x9e, 0x08, 0xb8,
        0x8c, 0x54, 0x43, 0xc0, 0x78, 0x02, 0x27, 0xe2,
        0x70, 0x1e, 0x30, 0x10, 0x10, 0x14, 0x00, 0x00,
        0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xca,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci100_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x03, 0x3a, 0x46, 0x65, 0x6a, 0x65,
        0x24, 0xad, 0xe9, 0x12, 0x32, 0x14, 0x1a, 0x26,
        0x34, 0xad, 0xa4, 0x41, 0x82, 0x23, 0x01, 0x50,
        0x2b, 0x1a, 0x24, 0x69, 0x48, 0x30, 0x40, 0x2e,
        0x11, 0x12, 0x08, 0xc6, 0x8c, 0x04, 0x41, 0x28,
        0x4c, 0x34, 0xf0, 0x1e, 0x01, 0x13, 0xf2, 0xe0,
        0x3c, 0x60, 0x20, 0x20, 0x28, 0x00, 0x00, 0x03,
        0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0x94, 0x20,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci50_1080p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x28,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6f, 0x37,
        0xcd, 0xf9, 0xbf, 0x81, 0x6b, 0xf3, 0x7c, 0xde,
        0x6e, 0x6c, 0xd3, 0x3c, 0x05, 0xa0, 0x22, 0x7e,
        0x5f, 0xfc, 0x00, 0x0c, 0x00, 0x13, 0x8c, 0x04,
        0x04, 0x05, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00,
        0x00, 0x03, 0x00, 0x32, 0x84, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci50_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x28,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6e, 0x61,
        0x87, 0x3e, 0x73, 0x4d, 0x98, 0x0c, 0x03, 0x06,
        0x9c, 0x0b, 0x73, 0xe6, 0xc0, 0xb5, 0x18, 0x63,
        0x0d, 0x39, 0xe0, 0x5b, 0x02, 0xd4, 0xc6, 0x19,
        0x1a, 0x79, 0x8c, 0x32, 0x34, 0x24, 0xf0, 0x16,
        0x81, 0x13, 0xf7, 0xff, 0x80, 0x02, 0x00, 0x01,
        0xf1, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0x03,
        0x00, 0x20, 0x00, 0x00, 0x06, 0x50, 0x80, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci100_720p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x2a, 0x33, 0x1d, 0xc7, 0x62,
        0xa1, 0x08, 0x40, 0x54, 0x66, 0x3b, 0x8e, 0xc5,
        0x42, 0x02, 0x10, 0x25, 0x64, 0x2c, 0x89, 0xe8,
        0x85, 0xe4, 0x21, 0x4b, 0x90, 0x83, 0x06, 0x95,
        0xd1, 0x06, 0x46, 0x97, 0x20, 0xc8, 0xd7, 0x43,
        0x08, 0x11, 0xc2, 0x1e, 0x4c, 0x91, 0x0f, 0x01,
        0x40, 0x16, 0xec, 0x07, 0x8c, 0x04, 0x04, 0x05,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x64, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci50_720p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x20,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6f, 0x37,
        0xcd, 0xf9, 0xbf, 0x81, 0x6b, 0xf3, 0x7c, 0xde,
        0x6e, 0x6c, 0xd3, 0x3c, 0x0f, 0x01, 0x6e, 0xff,
        0xc0, 0x00, 0xc0, 0x01, 0x38, 0xc0, 0x40, 0x40,
        0x50, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00,
        0x06, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };

    const uint8_t *data = NULL;
    int size            = 0;

    if (st->codecpar->width == 1920) {
        if (st->codecpar->field_order == AV_FIELD_PROGRESSIVE) {
            data = avci100_1080p_extradata;
            size = sizeof(avci100_1080p_extradata);
        } else {
            data = avci100_1080i_extradata;
            size = sizeof(avci100_1080i_extradata);
        }
    } else if (st->codecpar->width == 1440) {
        if (st->codecpar->field_order == AV_FIELD_PROGRESSIVE) {
            data = avci50_1080p_extradata;
            size = sizeof(avci50_1080p_extradata);
        } else {
            data = avci50_1080i_extradata;
            size = sizeof(avci50_1080i_extradata);
        }
    } else if (st->codecpar->width == 1280) {
        data = avci100_720p_extradata;
        size = sizeof(avci100_720p_extradata);
    } else if (st->codecpar->width == 960) {
        data = avci50_720p_extradata;
        size = sizeof(avci50_720p_extradata);
    }

    if (!size)
        return 0;

    av_em_freep(&st->codecpar->extradata);
    if (em_alloc_extradata(st->codecpar, size))
        return AVERROR(ENOMEM);
    memcpy(st->codecpar->extradata, data, size);

    return 0;
}

uint8_t *av_em_stream_get_side_data(AVEMStream *st, enum AVEMPacketSideDataType type,
                                 int *size)
{
    int i;

    for (i = 0; i < st->nb_side_data; i++) {
        if (st->side_data[i].type == type) {
            if (size)
                *size = st->side_data[i].size;
            return st->side_data[i].data;
        }
    }
    return NULL;
}

uint8_t *av_em_stream_new_side_data(AVEMStream *st, enum AVEMPacketSideDataType type,
                                 int size)
{
    AVEMPacketSideData *sd, *tmp;
    int i;
    uint8_t *data = av_em_alloc(size);

    if (!data)
        return NULL;

    for (i = 0; i < st->nb_side_data; i++) {
        sd = &st->side_data[i];

        if (sd->type == type) {
            av_em_freep(&sd->data);
            sd->data = data;
            sd->size = size;
            return sd->data;
        }
    }

    tmp = av_em_realloc_array(st->side_data, st->nb_side_data + 1, sizeof(*tmp));
    if (!tmp) {
        av_em_freep(&data);
        return NULL;
    }

    st->side_data = tmp;
    st->nb_side_data++;

    sd = &st->side_data[st->nb_side_data - 1];
    sd->type = type;
    sd->data = data;
    sd->size = size;
    return data;
}

int em_stream_add_bitstream_filter(AVEMStream *st, const char *name, const char *args)
{
    int ret;
    const AVBitStreamFilter *bsf;
    AVBSFContext *bsfc;
    AVEMCodecParameters *in_par;

    if (!(bsf = av_em_bsf_get_by_name(name))) {
        av_em_log(NULL, AV_LOG_ERROR, "Unknown bitstream filter '%s'\n", name);
        return AVERROR_BSF_NOT_FOUND;
    }

    if ((ret = av_em_bsf_alloc(bsf, &bsfc)) < 0)
        return ret;

    if (st->internal->nb_bsfcs) {
        in_par = st->internal->bsfcs[st->internal->nb_bsfcs - 1]->par_out;
        bsfc->time_base_in = st->internal->bsfcs[st->internal->nb_bsfcs - 1]->time_base_out;
    } else {
        in_par = st->codecpar;
        bsfc->time_base_in = st->time_base;
    }

    if ((ret = avcodec_em_parameters_copy(bsfc->par_in, in_par)) < 0) {
        av_em_bsf_free(&bsfc);
        return ret;
    }

    if (args && bsfc->filter->priv_class) {
        const AVOption *opt = av_em_opt_next(bsfc->priv_data, NULL);
        const char * shorthand[2] = {NULL};

        if (opt)
            shorthand[0] = opt->name;

        if ((ret = av_em_opt_set_from_string(bsfc->priv_data, args, shorthand, "=", ":")) < 0) {
            av_em_bsf_free(&bsfc);
            return ret;
        }
    }

    if ((ret = av_em_bsf_init(bsfc)) < 0) {
        av_em_bsf_free(&bsfc);
        return ret;
    }

    if ((ret = av_em_dynarray_add_nofree(&st->internal->bsfcs, &st->internal->nb_bsfcs, bsfc))) {
        av_em_bsf_free(&bsfc);
        return ret;
    }

    av_em_log(NULL, AV_LOG_VERBOSE,
           "Automatically inserted bitstream filter '%s'; args='%s'\n",
           name, args ? args : "");
    return 1;
}

#if FF_API_OLD_BSF
FF_DISABLE_DEPRECATION_WARNINGS
int av_em_apply_bitstream_filters(AVEMCodecContext *codec, AVEMPacket *pkt,
                               AVBitStreamFilterContext *bsfc)
{
    int ret = 0;
    while (bsfc) {
        AVEMPacket new_pkt = *pkt;
        int a = av_em_bitstream_filter_filter(bsfc, codec, NULL,
                                           &new_pkt.data, &new_pkt.size,
                                           pkt->data, pkt->size,
                                           pkt->flags & AV_PKT_FLAG_KEY);
        if (a == 0 && new_pkt.size == 0 && new_pkt.side_data_elems == 0) {
            av_em_packet_unref(pkt);
            memset(pkt, 0, sizeof(*pkt));
            return 0;
        }
        if(a == 0 && new_pkt.data != pkt->data) {
            uint8_t *t = av_em_alloc(new_pkt.size + AV_INPUT_BUFFER_PADDING_SIZE); //the new should be a subset of the old so cannot overflow
            if (t) {
                memcpy(t, new_pkt.data, new_pkt.size);
                memset(t + new_pkt.size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
                new_pkt.data = t;
                new_pkt.buf = NULL;
                a = 1;
            } else {
                a = AVERROR(ENOMEM);
            }
        }
        if (a > 0) {
            new_pkt.buf = av_em_buffer_create(new_pkt.data, new_pkt.size,
                                           av_em_buffer_default_free, NULL, 0);
            if (new_pkt.buf) {
                pkt->side_data = NULL;
                pkt->side_data_elems = 0;
                av_em_packet_unref(pkt);
            } else {
                av_em_freep(&new_pkt.data);
                a = AVERROR(ENOMEM);
            }
        }
        if (a < 0) {
            av_em_log(codec, AV_LOG_ERROR,
                   "Failed to open bitstream filter %s for stream %d with codec %s",
                   bsfc->filter->name, pkt->stream_index,
                   codec->codec ? codec->codec->name : "copy");
            ret = a;
            break;
        }
        *pkt = new_pkt;

        bsfc = bsfc->next;
    }
    return ret;
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

void em_format_io_close(AVEMFormatContext *s, AVEMIOContext **pb)
{
    if (*pb)
        s->io_close(s, *pb);
    *pb = NULL;
}

int em_parse_creation_time_metadata(AVEMFormatContext *s, int64_t *timestamp, int return_seconds)
{
    AVEMDictionaryEntry *entry;
    int64_t parsed_timestamp;
    int ret;
    if ((entry = av_em_dict_get(s->metadata, "creation_time", NULL, 0))) {
        if ((ret = av_em_parse_time(&parsed_timestamp, entry->value, 0)) >= 0) {
            *timestamp = return_seconds ? parsed_timestamp / 1000000 : parsed_timestamp;
            return 1;
        } else {
            av_em_log(s, AV_LOG_WARNING, "Failed to parse creation_time %s\n", entry->value);
            return ret;
        }
    }
    return 0;
}

int em_standardize_creation_time(AVEMFormatContext *s)
{
    int64_t timestamp;
    int ret = em_parse_creation_time_metadata(s, &timestamp, 0);
    if (ret == 1) {
        time_t seconds = timestamp / 1000000;
        struct tm *ptm, tmbuf;
        ptm = gmtime_r(&seconds, &tmbuf);
        if (ptm) {
            char buf[32];
            if (!strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", ptm))
                return AVERROR_EXTERNAL;
            av_em_strlcatf(buf, sizeof(buf), ".%06dZ", (int)(timestamp % 1000000));
            av_em_dict_set(&s->metadata, "creation_time", buf, 0);
        } else {
            return AVERROR_EXTERNAL;
        }
    }
    return ret;
}

int em_get_packet_palette(AVEMFormatContext *s, AVEMPacket *pkt, int ret, uint32_t *palette)
{
    uint8_t *side_data;
    int size;

    side_data = av_em_packet_get_side_data(pkt, AV_PKT_DATA_PALETTE, &size);
    if (side_data) {
        if (size != AVPALETTE_SIZE) {
            av_em_log(s, AV_LOG_ERROR, "Invalid palette side data\n");
            return AVERROR_INVALIDDATA;
        }
        memcpy(palette, side_data, AVPALETTE_SIZE);
        return 1;
    }

    if (ret == CONTAINS_PAL) {
        int i;
        for (i = 0; i < AVPALETTE_COUNT; i++)
            palette[i] = AV_RL32(pkt->data + pkt->size - AVPALETTE_SIZE + i*4);
        return 1;
    }

    return 0;
}

int em_bprint_to_codecpar_extradata(AVEMCodecParameters *par, struct AVEMBPrint *buf)
{
    int ret;
    char *str;

    ret = av_em_bprint_finalize(buf, &str);
    if (ret < 0)
        return ret;
    if (!av_em_bprint_is_complete(buf)) {
        av_em_free(str);
        return AVERROR(ENOMEM);
    }

    par->extradata = str;
    /* Note: the string is NUL terminated (so extradata can be read as a
     * string), but the ending character is not accounted in the size (in
     * binary formats you are likely not supposed to mux that character). When
     * extradata is copied, it is also padded with AV_INPUT_BUFFER_PADDING_SIZE
     * zeros. */
    par->extradata_size = buf->len;
    return 0;
}
