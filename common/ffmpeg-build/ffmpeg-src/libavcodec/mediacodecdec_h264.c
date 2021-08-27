/*
 * Android MediaCodec H.264 decoder
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixfmt.h"
#include "libavutil/atomic.h"

#include "avcodec.h"
#include "h264.h"
#include "internal.h"
#include "mediacodecdec.h"
#include "mediacodec_wrapper.h"

#define CODEC_MIME "video/avc"

typedef struct MediaCodecH264DecContext {

    MediaCodecDecContext ctx;

    AVBSFContext *bsf;

    AVFifoBuffer *fifo;

    AVEMPacket filtered_pkt;

} MediaCodecH264DecContext;

static av_cold int mediacodec_decode_close(AVEMCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    ff_mediacodec_dec_close(avctx, &s->ctx);

    av_em_fifo_free(s->fifo);

    av_em_bsf_free(&s->bsf);
    av_em_packet_unref(&s->filtered_pkt);

    return 0;
}

static av_cold int mediacodec_decode_init(AVEMCodecContext *avctx)
{
    int i;
    int ret;

    H264ParamSets ps;
    const PPS *pps = NULL;
    const SPS *sps = NULL;
    int is_avc = 0;
    int nal_length_size = 0;

    FFAMediaFormat *format = NULL;
    MediaCodecH264DecContext *s = avctx->priv_data;

    memset(&ps, 0, sizeof(ps));

    format = ff_AMediaFormat_new();
    if (!format) {
        av_em_log(avctx, AV_LOG_ERROR, "Failed to create media format\n");
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    ff_AMediaFormat_setString(format, "mime", CODEC_MIME);
    ff_AMediaFormat_setInt32(format, "width", avctx->width);
    ff_AMediaFormat_setInt32(format, "height", avctx->height);

    ret = em_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &is_avc, &nal_length_size, 0, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const PPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const SPS*)ps.sps_list[pps->sps_id]->data;
        }
    }

    if (pps && sps) {
        ff_AMediaFormat_setBuffer(format, "csd-0", (void*)sps->data, sps->data_size);
        ff_AMediaFormat_setBuffer(format, "csd-1", (void*)pps->data, pps->data_size);
    } else {
        av_em_log(avctx, AV_LOG_ERROR, "Could not extract PPS/SPS from extradata");
        ret = AVERROR_INVALIDDATA;
        goto done;
    }

    if ((ret = ff_mediacodec_dec_init(avctx, &s->ctx, CODEC_MIME, format)) < 0) {
        goto done;
    }

    av_em_log(avctx, AV_LOG_INFO, "MediaCodec started successfully, ret = %d\n", ret);

    s->fifo = av_em_fifo_alloc(sizeof(AVEMPacket));
    if (!s->fifo) {
        ret = AVERROR(ENOMEM);
        goto done;
    }

    const AVBitStreamFilter *bsf = av_em_bsf_get_by_name("h264_mp4toannexb");
    if(!bsf) {
        ret = AVERROR_BSF_NOT_FOUND;
        goto done;
    }

    if ((ret = av_em_bsf_alloc(bsf, &s->bsf))) {
        goto done;
    }

    if (((ret = avcodec_em_parameters_from_context(s->bsf->par_in, avctx)) < 0) ||
        ((ret = av_em_bsf_init(s->bsf)) < 0)) {
          goto done;
    }

    av_em_init_packet(&s->filtered_pkt);

done:
    if (format) {
        ff_AMediaFormat_delete(format);
    }

    if (ret < 0) {
        mediacodec_decode_close(avctx);
    }

    em_h264_ps_uninit(&ps);

    return ret;
}


static int mediacodec_process_data(AVEMCodecContext *avctx, AVFrame *frame,
                                   int *got_frame, AVEMPacket *pkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    return ff_mediacodec_dec_decode(avctx, &s->ctx, frame, got_frame, pkt);
}

static int mediacodec_decode_frame(AVEMCodecContext *avctx, void *data,
                                   int *got_frame, AVEMPacket *avpkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVEMPacket input_pkt = { 0 };

        if (av_em_fifo_space(s->fifo) < sizeof(input_pkt)) {
            ret = av_em_fifo_realloc2(s->fifo,
                                   av_em_fifo_size(s->fifo) + sizeof(input_pkt));
            if (ret < 0)
                return ret;
        }

        ret = av_em_packet_ref(&input_pkt, avpkt);
        if (ret < 0)
            return ret;
        av_em_fifo_generic_write(s->fifo, &input_pkt, sizeof(input_pkt), NULL);
    }

    /* process buffered data */
    while (!*got_frame) {
        /* prepare the input data -- convert to Annex B if needed */
        if (s->filtered_pkt.size <= 0) {
            AVEMPacket input_pkt = { 0 };

            av_em_packet_unref(&s->filtered_pkt);

            /* no more data */
            if (av_em_fifo_size(s->fifo) < sizeof(AVEMPacket)) {
                return avpkt->size ? avpkt->size :
                    ff_mediacodec_dec_decode(avctx, &s->ctx, frame, got_frame, avpkt);
            }

            av_em_fifo_generic_read(s->fifo, &input_pkt, sizeof(input_pkt), NULL);

            ret = av_em_bsf_send_packet(s->bsf, &input_pkt);
            if (ret < 0) {
                return ret;
            }

            ret = av_em_bsf_receive_packet(s->bsf, &s->filtered_pkt);
            if (ret == AVERROR(EAGAIN)) {
                goto done;
            }

            /* h264_mp4toannexb is used here and does not requires flushing */
            av_assert0(ret != AVERROR_EOF);

            if (ret < 0) {
                return ret;
            }
        }

        ret = mediacodec_process_data(avctx, frame, got_frame, &s->filtered_pkt);
        if (ret < 0)
            return ret;

        s->filtered_pkt.size -= ret;
        s->filtered_pkt.data += ret;
    }
done:
    return avpkt->size;
}

static void mediacodec_decode_flush(AVEMCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    while (av_em_fifo_size(s->fifo)) {
        AVEMPacket pkt;
        av_em_fifo_generic_read(s->fifo, &pkt, sizeof(pkt), NULL);
        av_em_packet_unref(&pkt);
    }
    av_em_fifo_reset(s->fifo);

    av_em_packet_unref(&s->filtered_pkt);

    ff_mediacodec_dec_flush(avctx, &s->ctx);
}

AVEMCodec em_h264_mediacodec_decoder = {
    .name           = "h264_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
