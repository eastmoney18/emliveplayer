/*
 * Android MediaCodec decoder
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

#include <string.h>
#include <sys/types.h>

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "avcodec.h"
#include "internal.h"

#include "mediacodec_sw_buffer.h"
#include "mediacodec_wrapper.h"
#include "mediacodecdec.h"

/**
 * OMX.k3.video.decoder.avc, OMX.NVIDIA.* OMX.SEC.avc.dec and OMX.google
 * codec workarounds used in various place are taken from the Gstreamer
 * project.
 *
 * Gstreamer references:
 * https://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/sys/androidmedia/
 *
 * Gstreamer copyright notice:
 *
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Copyright (C) 2012, Rafaël Carré <funman@videolanorg>
 *
 * Copyright (C) 2015, Sebastian Dröge <sebastian@centricular.com>
 *
 * Copyright (C) 2014-2015, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@gcollabora.com>
 *
 * Copyright (C) 2015, Edward Hervey
 *   Author: Edward Hervey <bilboed@gmail.com>
 *
 * Copyright (C) 2015, Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#define INPUT_DEQUEUE_TIMEOUT_US 8000
#define OUTPUT_DEQUEUE_TIMEOUT_US 8000
#define OUTPUT_DEQUEUE_BLOCK_TIMEOUT_US 1000000

enum {
    COLOR_FormatYUV420Planar                              = 0x13,
    COLOR_FormatYUV420SemiPlanar                          = 0x15,
    COLOR_FormatYCbYCr                                    = 0x19,
    COLOR_FormatAndroidOpaque                             = 0x7F000789,
    COLOR_QCOM_FormatYUV420SemiPlanar                     = 0x7fa30c00,
    COLOR_QCOM_FormatYUV420SemiPlanar32m                  = 0x7fa30c04,
    COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7fa30c03,
    COLOR_TI_FormatYUV420PackedSemiPlanar                 = 0x7f000100,
    COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced       = 0x7f000001,
};

static const struct {

    int color_format;
    enum AVPixelFormat pix_fmt;

} color_formats[] = {

    { COLOR_FormatYUV420Planar,                              AV_PIX_FMT_YUV420P },
    { COLOR_FormatYUV420SemiPlanar,                          AV_PIX_FMT_NV12    },
    { COLOR_QCOM_FormatYUV420SemiPlanar,                     AV_PIX_FMT_NV12    },
    { COLOR_QCOM_FormatYUV420SemiPlanar32m,                  AV_PIX_FMT_NV12    },
    { COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka, AV_PIX_FMT_NV12    },
    { COLOR_TI_FormatYUV420PackedSemiPlanar,                 AV_PIX_FMT_NV12    },
    { COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced,       AV_PIX_FMT_NV12    },
    { 0 }
};

static enum AVPixelFormat mcdec_map_color_format(AVEMCodecContext *avctx,
                                                 MediaCodecDecContext *s,
                                                 int color_format)
{
    int i;
    enum AVPixelFormat ret = AV_PIX_FMT_NONE;

    if (!strcmp(s->codec_name, "OMX.k3.video.decoder.avc") && color_format == COLOR_FormatYCbYCr) {
        s->color_format = color_format = COLOR_TI_FormatYUV420PackedSemiPlanar;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(color_formats); i++) {
        if (color_formats[i].color_format == color_format) {
            return color_formats[i].pix_fmt;
        }
    }

    av_em_log(avctx, AV_LOG_ERROR, "Output color format 0x%x (value=%d) is not supported\n",
        color_format, color_format);

    return ret;
}

static int mediacodec_wrap_buffer(AVEMCodecContext *avctx,
                                  MediaCodecDecContext *s,
                                  uint8_t *data,
                                  size_t size,
                                  ssize_t index,
                                  FFAMediaCodecBufferInfo *info,
                                  AVFrame *frame)
{
    int ret = 0;
    int status = 0;

    frame->width = avctx->width;
    frame->height = avctx->height;
    frame->format = avctx->pix_fmt;

    /* MediaCodec buffers needs to be copied to our own refcounted buffers
     * because the flush command invalidates all input and output buffers.
     */
    if ((ret = em_get_buffer(avctx, frame, 0)) < 0) {
        av_em_log(avctx, AV_LOG_ERROR, "Could not allocate buffer\n");
        goto done;
    }

    /* Override frame->pkt_pts as em_get_buffer will override its value based
     * on the last avpacket received which is not in sync with the frame:
     *   * N avpackets can be pushed before 1 frame is actually returned
     *   * 0-sized avpackets are pushed to flush remaining frames at EOS */
    frame->pkt_pts = info->presentationTimeUs;
    frame->pkt_dts = AV_NOPTS_VALUE;

    av_em_log(avctx, AV_LOG_DEBUG,
            "Frame: width=%d stride=%d height=%d slice-height=%d "
            "crop-top=%d crop-bottom=%d crop-left=%d crop-right=%d encoder=%s\n"
            "destination linesizes=%d,%d,%d\n" ,
            avctx->width, s->stride, avctx->height, s->slice_height,
            s->crop_top, s->crop_bottom, s->crop_left, s->crop_right, s->codec_name,
            frame->linesize[0], frame->linesize[1], frame->linesize[2]);

    switch (s->color_format) {
    case COLOR_FormatYUV420Planar:
        ff_mediacodec_sw_buffer_copy_yuv420_planar(avctx, s, data, size, info, frame);
        break;
    case COLOR_FormatYUV420SemiPlanar:
    case COLOR_QCOM_FormatYUV420SemiPlanar:
    case COLOR_QCOM_FormatYUV420SemiPlanar32m:
        ff_mediacodec_sw_buffer_copy_yuv420_semi_planar(avctx, s, data, size, info, frame);
        break;
    case COLOR_TI_FormatYUV420PackedSemiPlanar:
    case COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced:
        ff_mediacodec_sw_buffer_copy_yuv420_packed_semi_planar(avctx, s, data, size, info, frame);
        break;
    case COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka:
        ff_mediacodec_sw_buffer_copy_yuv420_packed_semi_planar_64x32Tile2m8ka(avctx, s, data, size, info, frame);
        break;
    default:
        av_em_log(avctx, AV_LOG_ERROR, "Unsupported color format 0x%x (value=%d)\n",
            s->color_format, s->color_format);
        ret = AVERROR(EINVAL);
        goto done;
    }

    ret = 0;
done:
    status = ff_AMediaCodec_releaseOutputBuffer(s->codec, index, 0);
    if (status < 0) {
        av_em_log(avctx, AV_LOG_ERROR, "Failed to release output buffer\n");
        ret = AVERROR_EXTERNAL;
    }

    return ret;
}

static int mediacodec_dec_parse_format(AVEMCodecContext *avctx, MediaCodecDecContext *s)
{
    int width = 0;
    int height = 0;
    int32_t value = 0;
    char *format = NULL;

    if (!s->format) {
        av_em_log(avctx, AV_LOG_ERROR, "Output MediaFormat is not set\n");
        return AVERROR(EINVAL);
    }

    format = ff_AMediaFormat_toString(s->format);
    if (!format) {
        return AVERROR_EXTERNAL;
    }
    av_em_log(avctx, AV_LOG_DEBUG, "Parsing MediaFormat %s\n", format);
    av_em_freep(&format);

    /* Mandatory fields */
    if (!ff_AMediaFormat_getInt32(s->format, "width", &value)) {
        format = ff_AMediaFormat_toString(s->format);
        av_em_log(avctx, AV_LOG_ERROR, "Could not get %s from format %s\n", "width", format);
        av_em_freep(&format);
        return AVERROR_EXTERNAL;
    }
    s->width = value;

    if (!ff_AMediaFormat_getInt32(s->format, "height", &value)) {
        format = ff_AMediaFormat_toString(s->format);
        av_em_log(avctx, AV_LOG_ERROR, "Could not get %s from format %s\n", "height", format);
        av_em_freep(&format);
        return AVERROR_EXTERNAL;
    }
    s->height = value;

    if (!ff_AMediaFormat_getInt32(s->format, "stride", &value)) {
        format = ff_AMediaFormat_toString(s->format);
        av_em_log(avctx, AV_LOG_ERROR, "Could not get %s from format %s\n", "stride", format);
        av_em_freep(&format);
        return AVERROR_EXTERNAL;
    }
    s->stride = value > 0 ? value : s->width;

    if (!ff_AMediaFormat_getInt32(s->format, "slice-height", &value)) {
        format = ff_AMediaFormat_toString(s->format);
        av_em_log(avctx, AV_LOG_ERROR, "Could not get %s from format %s\n", "slice-height", format);
        av_em_freep(&format);
        return AVERROR_EXTERNAL;
    }
    s->slice_height = value > 0 ? value : s->height;

    if (strstr(s->codec_name, "OMX.Nvidia.")) {
        s->slice_height = FFALIGN(s->height, 16);
    } else if (strstr(s->codec_name, "OMX.SEC.avc.dec")) {
        s->slice_height = avctx->height;
        s->stride = avctx->width;
    }

    if (!ff_AMediaFormat_getInt32(s->format, "color-format", &value)) {
        format = ff_AMediaFormat_toString(s->format);
        av_em_log(avctx, AV_LOG_ERROR, "Could not get %s from format %s\n", "color-format", format);
        av_em_freep(&format);
        return AVERROR_EXTERNAL;
    }
    s->color_format = value;

    s->pix_fmt = avctx->pix_fmt = mcdec_map_color_format(avctx, s, value);
    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_em_log(avctx, AV_LOG_ERROR, "Output color format is not supported\n");
        return AVERROR(EINVAL);
    }

    /* Optional fields */
    if (ff_AMediaFormat_getInt32(s->format, "crop-top", &value))
        s->crop_top = value;

    if (ff_AMediaFormat_getInt32(s->format, "crop-bottom", &value))
        s->crop_bottom = value;

    if (ff_AMediaFormat_getInt32(s->format, "crop-left", &value))
        s->crop_left = value;

    if (ff_AMediaFormat_getInt32(s->format, "crop-right", &value))
        s->crop_right = value;

    width = s->crop_right + 1 - s->crop_left;
    height = s->crop_bottom + 1 - s->crop_top;

    av_em_log(avctx, AV_LOG_INFO,
        "Output crop parameters top=%d bottom=%d left=%d right=%d, "
        "resulting dimensions width=%d height=%d\n",
        s->crop_top, s->crop_bottom, s->crop_left, s->crop_right,
        width, height);

    return em_set_dimensions(avctx, width, height);
}

int ff_mediacodec_dec_init(AVEMCodecContext *avctx, MediaCodecDecContext *s,
                           const char *mime, FFAMediaFormat *format)
{
    int ret = 0;
    int status;
    int profile;

    s->first_buffer_at = av_em_gettime();

    profile = ff_AMediaCodecProfile_getProfileFromAVEMCodecContext(avctx);
    if (profile < 0) {
        av_em_log(avctx, AV_LOG_WARNING, "Unsupported or unknown profile");
    }

    s->codec_name = ff_AMediaCodecList_getCodecNameByType(mime, profile, 0, avctx);
    if (!s->codec_name) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    av_em_log(avctx, AV_LOG_DEBUG, "Found decoder %s\n", s->codec_name);
    s->codec = ff_AMediaCodec_createCodecByName(s->codec_name);
    if (!s->codec) {
        av_em_log(avctx, AV_LOG_ERROR, "Failed to create media decoder for type %s and name %s\n", mime, s->codec_name);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    status = ff_AMediaCodec_configure(s->codec, format, NULL, NULL, 0);
    if (status < 0) {
        char *desc = ff_AMediaFormat_toString(format);
        av_em_log(avctx, AV_LOG_ERROR,
            "Failed to configure codec (status = %d) with format %s\n",
            status, desc);
        av_em_freep(&desc);

        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    status = ff_AMediaCodec_start(s->codec);
    if (status < 0) {
        char *desc = ff_AMediaFormat_toString(format);
        av_em_log(avctx, AV_LOG_ERROR,
            "Failed to start codec (status = %d) with format %s\n",
            status, desc);
        av_em_freep(&desc);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    s->format = ff_AMediaCodec_getOutputFormat(s->codec);
    if (s->format) {
        if ((ret = mediacodec_dec_parse_format(avctx, s)) < 0) {
            av_em_log(avctx, AV_LOG_ERROR,
                "Failed to configure context\n");
            goto fail;
        }
    }

    av_em_log(avctx, AV_LOG_DEBUG, "MediaCodec %p started successfully\n", s->codec);

    return 0;

fail:
    av_em_log(avctx, AV_LOG_ERROR, "MediaCodec %p failed to start\n", s->codec);
    ff_mediacodec_dec_close(avctx, s);
    return ret;
}

int ff_mediacodec_dec_decode(AVEMCodecContext *avctx, MediaCodecDecContext *s,
                             AVFrame *frame, int *got_frame,
                             AVEMPacket *pkt)
{
    int ret;
    int offset = 0;
    int need_flushing = 0;
    uint8_t *data;
    ssize_t index;
    size_t size;
    FFAMediaCodec *codec = s->codec;
    FFAMediaCodecBufferInfo info = { 0 };

    int status;

    int64_t input_dequeue_timeout_us = INPUT_DEQUEUE_TIMEOUT_US;
    int64_t output_dequeue_timeout_us = OUTPUT_DEQUEUE_TIMEOUT_US;

    if (pkt->size == 0) {
        need_flushing = 1;
    }

    if (s->flushing && s->eos) {
        return 0;
    }

    while (offset < pkt->size || (need_flushing && !s->flushing)) {
        int size;

        index = ff_AMediaCodec_dequeueInputBuffer(codec, input_dequeue_timeout_us);
        if (ff_AMediaCodec_infoTryAgainLater(codec, index)) {
            break;
        }

        if (index < 0) {
            av_em_log(avctx, AV_LOG_ERROR, "Failed to dequeue input buffer (status=%zd)\n", index);
            return AVERROR_EXTERNAL;
        }

        data = ff_AMediaCodec_getInputBuffer(codec, index, &size);
        if (!data) {
            av_em_log(avctx, AV_LOG_ERROR, "Failed to get input buffer\n");
            return AVERROR_EXTERNAL;
        }

        if (need_flushing) {
            uint32_t flags = ff_AMediaCodec_getBufferFlagEndOfStream(codec);

            av_em_log(avctx, AV_LOG_DEBUG, "Sending End Of Stream signal\n");

            status = ff_AMediaCodec_queueInputBuffer(codec, index, 0, 0, pkt->pts, flags);
            if (status < 0) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to queue input empty buffer (status = %d)\n", status);
                return AVERROR_EXTERNAL;
            }

            s->flushing = 1;
            break;
        } else {
            size = FFMIN(pkt->size - offset, size);

            memcpy(data, pkt->data + offset, size);
            offset += size;

            status = ff_AMediaCodec_queueInputBuffer(codec, index, 0, size, pkt->pts, 0);
            if (status < 0) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to queue input buffer (status = %d)\n", status);
                return AVERROR_EXTERNAL;
            }
        }
    }

    if (need_flushing || s->flushing) {
        /* If the codec is flushing or need to be flushed, block for a fair
         * amount of time to ensure we got a frame */
        output_dequeue_timeout_us = OUTPUT_DEQUEUE_BLOCK_TIMEOUT_US;
    } else if (s->dequeued_buffer_nb == 0) {
        /* If the codec hasn't produced any frames, do not block so we
         * can push data to it as fast as possible, and get the first
         * frame */
        output_dequeue_timeout_us = 0;
    }

    index = ff_AMediaCodec_dequeueOutputBuffer(codec, &info, output_dequeue_timeout_us);
    if (index >= 0) {
        int ret;

        if (!s->first_buffer++) {
            av_em_log(avctx, AV_LOG_DEBUG, "Got first buffer after %fms\n", (av_em_gettime() - s->first_buffer_at) / 1000);
        }

        av_em_log(avctx, AV_LOG_DEBUG, "Got output buffer %zd"
                " offset=%" PRIi32 " size=%" PRIi32 " ts=%" PRIi64
                " flags=%" PRIu32 "\n", index, info.offset, info.size,
                info.presentationTimeUs, info.flags);

        if (info.flags & ff_AMediaCodec_getBufferFlagEndOfStream(codec)) {
            s->eos = 1;
        }

        if (info.size) {
            data = ff_AMediaCodec_getOutputBuffer(codec, index, &size);
            if (!data) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to get output buffer\n");
                return AVERROR_EXTERNAL;
            }

            if ((ret = mediacodec_wrap_buffer(avctx, s, data, size, index, &info, frame)) < 0) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to wrap MediaCodec buffer\n");
                return ret;
            }

            *got_frame = 1;
            s->dequeued_buffer_nb++;
        } else {
            status = ff_AMediaCodec_releaseOutputBuffer(codec, index, 0);
            if (status < 0) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to release output buffer\n");
            }
        }

    } else if (ff_AMediaCodec_infoOutputFormatChanged(codec, index)) {
        char *format = NULL;

        if (s->format) {
            status = ff_AMediaFormat_delete(s->format);
            if (status < 0) {
                av_em_log(avctx, AV_LOG_ERROR, "Failed to delete MediaFormat %p\n", s->format);
            }
        }

        s->format = ff_AMediaCodec_getOutputFormat(codec);
        if (!s->format) {
            av_em_log(avctx, AV_LOG_ERROR, "Failed to get output format\n");
            return AVERROR_EXTERNAL;
        }

        format = ff_AMediaFormat_toString(s->format);
        if (!format) {
            return AVERROR_EXTERNAL;
        }
        av_em_log(avctx, AV_LOG_INFO, "Output MediaFormat changed to %s\n", format);
        av_em_freep(&format);

        if ((ret = mediacodec_dec_parse_format(avctx, s)) < 0) {
            return ret;
        }

    } else if (ff_AMediaCodec_infoOutputBuffersChanged(codec, index)) {
        ff_AMediaCodec_cleanOutputBuffers(codec);
    } else if (ff_AMediaCodec_infoTryAgainLater(codec, index)) {
        if (s->flushing) {
            av_em_log(avctx, AV_LOG_ERROR, "Failed to dequeue output buffer within %" PRIi64 "ms "
                                        "while flushing remaining frames, output will probably lack frames\n",
                                        output_dequeue_timeout_us / 1000);
        } else {
            av_em_log(avctx, AV_LOG_DEBUG, "No output buffer available, try again later\n");
        }
    } else {
        av_em_log(avctx, AV_LOG_ERROR, "Failed to dequeue output buffer (status=%zd)\n", index);
        return AVERROR_EXTERNAL;
    }

    return offset;
}

int ff_mediacodec_dec_flush(AVEMCodecContext *avctx, MediaCodecDecContext *s)
{
    FFAMediaCodec *codec = s->codec;
    int status;

    s->dequeued_buffer_nb = 0;

    s->flushing = 0;
    s->eos = 0;

    status = ff_AMediaCodec_flush(codec);
    if (status < 0) {
        av_em_log(avctx, AV_LOG_ERROR, "Failed to flush codec\n");
        return AVERROR_EXTERNAL;
    }

    s->first_buffer = 0;
    s->first_buffer_at = av_em_gettime();

    return 0;
}

int ff_mediacodec_dec_close(AVEMCodecContext *avctx, MediaCodecDecContext *s)
{
    if (s->codec) {
        ff_AMediaCodec_delete(s->codec);
        s->codec = NULL;
    }

    if (s->format) {
        ff_AMediaFormat_delete(s->format);
        s->format = NULL;
    }

    av_em_freep(&s->codec_name);

    return 0;
}