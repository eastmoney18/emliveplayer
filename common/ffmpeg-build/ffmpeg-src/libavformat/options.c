/*
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
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

#include "libavutil/internal.h"
#include "libavutil/opt.h"

/**
 * @file
 * Options definition for AVEMFormatContext.
 */

FF_DISABLE_DEPRECATION_WARNINGS
#include "options_table.h"
FF_ENABLE_DEPRECATION_WARNINGS

static const char* format_to_name(void* ptr)
{
    AVEMFormatContext* fc = (AVEMFormatContext*) ptr;
    if(fc->iformat) return fc->iformat->name;
    else if(fc->oformat) return fc->oformat->name;
    else return "NULL";
}

static void *format_child_next(void *obj, void *prev)
{
    AVEMFormatContext *s = obj;
    if (!prev && s->priv_data &&
        ((s->iformat && s->iformat->priv_class) ||
          s->oformat && s->oformat->priv_class))
        return s->priv_data;
    if (s->pb && s->pb->av_class && prev != s->pb)
        return s->pb;
    return NULL;
}

static const AVEMClass *format_child_class_next(const AVEMClass *prev)
{
    AVEMInputFormat  *ifmt = NULL;
    AVEMOutputFormat *ofmt = NULL;

    if (!prev)
        return &em_avio_class;

    while ((ifmt = av_em_iformat_next(ifmt)))
        if (ifmt->priv_class == prev)
            break;

    if (!ifmt)
        while ((ofmt = av_em_oformat_next(ofmt)))
            if (ofmt->priv_class == prev)
                break;
    if (!ofmt)
        while (ifmt = av_em_iformat_next(ifmt))
            if (ifmt->priv_class)
                return ifmt->priv_class;

    while (ofmt = av_em_oformat_next(ofmt))
        if (ofmt->priv_class)
            return ofmt->priv_class;

    return NULL;
}

static AVEMClassCategory get_category(void *ptr)
{
    AVEMFormatContext* s = ptr;
    if(s->iformat) return AV_CLASS_CATEGORY_DEMUXER;
    else           return AV_CLASS_CATEGORY_MUXER;
}

static const AVEMClass av_format_context_class = {
    .class_name     = "AVEMFormatContext",
    .item_name      = format_to_name,
    .option         = avformat_options,
    .version        = LIBAVUTIL_VERSION_INT,
    .child_next     = format_child_next,
    .child_class_next = format_child_class_next,
    .category       = AV_CLASS_CATEGORY_MUXER,
    .get_category   = get_category,
};

static int io_open_default(AVEMFormatContext *s, AVEMIOContext **pb,
                           const char *url, int flags, AVEMDictionary **options)
{
#if FF_API_OLD_OPEN_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
    if (s->open_cb)
        return s->open_cb(s, pb, url, flags, &s->interrupt_callback, options);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return emio_open_whitelist(pb, url, flags, &s->interrupt_callback, options, s->protocol_whitelist, s->protocol_blacklist);
}

static void io_close_default(AVEMFormatContext *s, AVEMIOContext *pb)
{
    avio_em_close(pb);
}

static void avformat_get_context_defaults(AVEMFormatContext *s)
{
    memset(s, 0, sizeof(AVEMFormatContext));

    s->av_class = &av_format_context_class;

    s->io_open  = io_open_default;
    s->io_close = io_close_default;

    av_em_opt_set_defaults(s);
}

AVEMFormatContext *avformat_em_alloc_context(void)
{
    AVEMFormatContext *ic;
    ic = av_em_alloc(sizeof(AVEMFormatContext));
    if (!ic) return ic;
    avformat_get_context_defaults(ic);

    ic->internal = av_em_mallocz(sizeof(*ic->internal));
    if (!ic->internal) {
        avformat_em_free_context(ic);
        return NULL;
    }
    ic->internal->offset = AV_NOPTS_VALUE;
    ic->internal->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;

    return ic;
}

enum AVEMDurationEstimationMethod av_em_fmt_ctx_get_duration_estimation_method(const AVEMFormatContext* ctx)
{
    return ctx->duration_estimation_method;
}

const AVEMClass *avformat_em_get_class(void)
{
    return &av_format_context_class;
}
