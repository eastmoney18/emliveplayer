/*
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * Options definition for AVEMCodecContext.
 */

#include "avcodec.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include <float.h>              /* FLT_MIN, FLT_MAX */
#include <string.h>

FF_DISABLE_DEPRECATION_WARNINGS
#include "options_table.h"
FF_ENABLE_DEPRECATION_WARNINGS

static const char* context_to_name(void* ptr) {
    AVEMCodecContext *avc= ptr;

    if(avc && avc->codec && avc->codec->name)
        return avc->codec->name;
    else
        return "NULL";
}

static void *codec_child_next(void *obj, void *prev)
{
    AVEMCodecContext *s = obj;
    if (!prev && s->codec && s->codec->priv_class && s->priv_data)
        return s->priv_data;
    return NULL;
}

static const AVEMClass *codec_child_class_next(const AVEMClass *prev)
{
    AVEMCodec *c = NULL;

    /* find the codec that corresponds to prev */
    while (prev && (c = av_em_codec_next(c)))
        if (c->priv_class == prev)
            break;

    /* find next codec with priv options */
    while (c = av_em_codec_next(c))
        if (c->priv_class)
            return c->priv_class;
    return NULL;
}

static AVEMClassCategory get_category(void *ptr)
{
    AVEMCodecContext* avctx = ptr;
    if(avctx->codec && avctx->codec->decode) return AV_CLASS_CATEGORY_DECODER;
    else                                     return AV_CLASS_CATEGORY_ENCODER;
}

static const AVEMClass av_em_codec_context_class = {
    .class_name              = "AVEMCodecContext",
    .item_name               = context_to_name,
    .option                  = avcodec_options,
    .version                 = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset = offsetof(AVEMCodecContext, log_level_offset),
    .child_next              = codec_child_next,
    .child_class_next        = codec_child_class_next,
    .category                = AV_CLASS_CATEGORY_ENCODER,
    .get_category            = get_category,
};

static int init_context_defaults(AVEMCodecContext *s, const AVEMCodec *codec)
{
    int flags=0;
    memset(s, 0, sizeof(AVEMCodecContext));

    s->av_class = &av_em_codec_context_class;

    s->codec_type = codec ? codec->type : AVMEDIA_TYPE_UNKNOWN;
    if (codec) {
        s->codec = codec;
        s->codec_id = codec->id;
    }

    if(s->codec_type == AVMEDIA_TYPE_AUDIO)
        flags= AV_OPT_FLAG_AUDIO_PARAM;
    else if(s->codec_type == AVMEDIA_TYPE_VIDEO)
        flags= AV_OPT_FLAG_VIDEO_PARAM;
    else if(s->codec_type == AVMEDIA_TYPE_SUBTITLE)
        flags= AV_OPT_FLAG_SUBTITLE_PARAM;
    av_em_opt_set_defaults2(s, flags, flags);

    s->time_base           = (AVEMRational){0,1};
    s->framerate           = (AVEMRational){ 0, 1 };
    s->pkt_timebase        = (AVEMRational){ 0, 1 };
    s->get_buffer2         = avcodec_em_default_get_buffer2;
    s->get_format          = avcodec_em_default_get_format;
    s->execute             = avcodec_em_default_execute;
    s->execute2            = avcodec_em_default_execute2;
    s->sample_aspect_ratio = (AVEMRational){0,1};
    s->pix_fmt             = AV_PIX_FMT_NONE;
    s->sample_fmt          = AV_SAMPLE_FMT_NONE;

    s->reordered_opaque    = AV_NOPTS_VALUE;
    if(codec && codec->priv_data_size){
        if(!s->priv_data){
            s->priv_data= av_em_mallocz(codec->priv_data_size);
            if (!s->priv_data) {
                return AVERROR(ENOMEM);
            }
        }
        if(codec->priv_class){
            *(const AVEMClass**)s->priv_data = codec->priv_class;
            av_em_opt_set_defaults(s->priv_data);
        }
    }
    if (codec && codec->defaults) {
        int ret;
        const AVEMCodecDefault *d = codec->defaults;
        while (d->key) {
            ret = av_em_opt_set(s, d->key, d->value, 0);
            av_assert0(ret >= 0);
            d++;
        }
    }
    return 0;
}

#if FF_API_GET_CONTEXT_DEFAULTS
int avcodec_em_get_context_defaults3(AVEMCodecContext *s, const AVEMCodec *codec)
{
    return init_context_defaults(s, codec);
}
#endif

AVEMCodecContext *avcodec_em_alloc_context3(const AVEMCodec *codec)
{
    AVEMCodecContext *avctx= av_em_alloc(sizeof(AVEMCodecContext));

    if (!avctx)
        return NULL;

    if (init_context_defaults(avctx, codec) < 0) {
        av_em_free(avctx);
        return NULL;
    }

    return avctx;
}

void avcodec_em_free_context(AVEMCodecContext **pavctx)
{
    AVEMCodecContext *avctx = *pavctx;

    if (!avctx)
        return;

    avcodec_em_close(avctx);

    av_em_freep(&avctx->extradata);
    av_em_freep(&avctx->subtitle_header);
    av_em_freep(&avctx->intra_matrix);
    av_em_freep(&avctx->inter_matrix);
    av_em_freep(&avctx->rc_override);

    av_em_freep(pavctx);
}

#if FF_API_COPY_CONTEXT
int avcodec_em_copy_context(AVEMCodecContext *dest, const AVEMCodecContext *src)
{
    const AVEMCodec *orig_codec = dest->codec;
    uint8_t *orig_priv_data = dest->priv_data;

    if (avcodec_em_is_open(dest)) { // check that the dest context is uninitialized
        av_em_log(dest, AV_LOG_ERROR,
               "Tried to copy AVEMCodecContext %p into already-initialized %p\n",
               src, dest);
        return AVERROR(EINVAL);
    }

    av_em_opt_free(dest);
    av_em_freep(&dest->rc_override);
    av_em_freep(&dest->intra_matrix);
    av_em_freep(&dest->inter_matrix);
    av_em_freep(&dest->extradata);
    av_em_freep(&dest->subtitle_header);

    memcpy(dest, src, sizeof(*dest));
    av_em_opt_copy(dest, src);

    dest->priv_data       = orig_priv_data;
    dest->codec           = orig_codec;

    if (orig_priv_data && src->codec && src->codec->priv_class &&
        dest->codec && dest->codec->priv_class)
        av_em_opt_copy(orig_priv_data, src->priv_data);


    /* set values specific to opened codecs back to their default state */
    dest->slice_offset    = NULL;
    dest->hwaccel         = NULL;
    dest->internal        = NULL;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    dest->coded_frame     = NULL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    /* reallocate values that should be allocated separately */
    dest->extradata       = NULL;
    dest->intra_matrix    = NULL;
    dest->inter_matrix    = NULL;
    dest->rc_override     = NULL;
    dest->subtitle_header = NULL;
    dest->hw_frames_ctx   = NULL;

#define alloc_and_copy_or_fail(obj, size, pad) \
    if (src->obj && size > 0) { \
        dest->obj = av_em_alloc(size + pad); \
        if (!dest->obj) \
            goto fail; \
        memcpy(dest->obj, src->obj, size); \
        if (pad) \
            memset(((uint8_t *) dest->obj) + size, 0, pad); \
    }
    alloc_and_copy_or_fail(extradata,    src->extradata_size,
                           AV_INPUT_BUFFER_PADDING_SIZE);
    dest->extradata_size  = src->extradata_size;
    alloc_and_copy_or_fail(intra_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(inter_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(rc_override,  src->rc_override_count * sizeof(*src->rc_override), 0);
    alloc_and_copy_or_fail(subtitle_header, src->subtitle_header_size, 1);
    av_assert0(dest->subtitle_header_size == src->subtitle_header_size);
#undef alloc_and_copy_or_fail

    if (src->hw_frames_ctx) {
        dest->hw_frames_ctx = av_em_buffer_ref(src->hw_frames_ctx);
        if (!dest->hw_frames_ctx)
            goto fail;
    }

    return 0;

fail:
    av_em_freep(&dest->subtitle_header);
    av_em_freep(&dest->rc_override);
    av_em_freep(&dest->intra_matrix);
    av_em_freep(&dest->inter_matrix);
    av_em_freep(&dest->extradata);
    av_em_buffer_unref(&dest->hw_frames_ctx);
    dest->subtitle_header_size = 0;
    dest->extradata_size = 0;
    av_em_opt_free(dest);
    return AVERROR(ENOMEM);
}
#endif

const AVEMClass *avcodec_em_get_class(void)
{
    return &av_em_codec_context_class;
}

#define FOFFSET(x) offsetof(AVFrame,x)

static const AVOption frame_options[]={
{"best_effort_timestamp", "", FOFFSET(best_effort_timestamp), AV_OPT_TYPE_INT64, {.i64 = AV_NOPTS_VALUE }, INT64_MIN, INT64_MAX, 0},
{"pkt_pos", "", FOFFSET(pkt_pos), AV_OPT_TYPE_INT64, {.i64 = -1 }, INT64_MIN, INT64_MAX, 0},
{"pkt_size", "", FOFFSET(pkt_size), AV_OPT_TYPE_INT64, {.i64 = -1 }, INT64_MIN, INT64_MAX, 0},
{"sample_aspect_ratio", "", FOFFSET(sample_aspect_ratio), AV_OPT_TYPE_RATIONAL, {.dbl = 0 }, 0, INT_MAX, 0},
{"width", "", FOFFSET(width), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"height", "", FOFFSET(height), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"format", "", FOFFSET(format), AV_OPT_TYPE_INT, {.i64 = -1 }, 0, INT_MAX, 0},
{"channel_layout", "", FOFFSET(channel_layout), AV_OPT_TYPE_INT64, {.i64 = 0 }, 0, INT64_MAX, 0},
{"sample_rate", "", FOFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{NULL},
};

static const AVEMClass av_frame_class = {
    .class_name              = "AVFrame",
    .item_name               = NULL,
    .option                  = frame_options,
    .version                 = LIBAVUTIL_VERSION_INT,
};

const AVEMClass *avcodec_em_get_frame_class(void)
{
    return &av_frame_class;
}

#define SROFFSET(x) offsetof(AVSubtitleRect,x)

static const AVOption subtitle_rect_options[]={
{"x", "", SROFFSET(x), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"y", "", SROFFSET(y), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"w", "", SROFFSET(w), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"h", "", SROFFSET(h), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"type", "", SROFFSET(type), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"flags", "", SROFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, 1, 0, "flags"},
{"forced", "", SROFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, 1, 0},
{NULL},
};

static const AVEMClass av_subtitle_rect_class = {
    .class_name             = "AVSubtitleRect",
    .item_name              = NULL,
    .option                 = subtitle_rect_options,
    .version                = LIBAVUTIL_VERSION_INT,
};

const AVEMClass *avcodec_em_get_subtitle_rect_class(void)
{
    return &av_subtitle_rect_class;
}

#ifdef TEST
static int dummy_init(AVEMCodecContext *ctx)
{
    //TODO: this code should set every possible pointer that could be set by codec and is not an option;
    ctx->extradata_size = 8;
    ctx->extradata = av_em_alloc(ctx->extradata_size);
    return 0;
}

static int dummy_close(AVEMCodecContext *ctx)
{
    av_em_freep(&ctx->extradata);
    ctx->extradata_size = 0;
    return 0;
}

static int dummy_encode(AVEMCodecContext *ctx, AVEMPacket *pkt, const AVFrame *frame, int *got_packet)
{
    return AVERROR(ENOSYS);
}

typedef struct Dummy12Context {
    AVEMClass  *av_class;
    int      num;
    char*    str;
} Dummy12Context;

typedef struct Dummy3Context {
    void     *fake_av_class;
    int      num;
    char*    str;
} Dummy3Context;

#define OFFSET(x) offsetof(Dummy12Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption dummy_options[] = {
    { "str", "set str", OFFSET(str), AV_OPT_TYPE_STRING, { .str = "i'm src default value" }, 0, 0, VE},
    { "num", "set num", OFFSET(num), AV_OPT_TYPE_INT,    { .i64 = 1500100900 },    0, INT_MAX, VE},
    { NULL },
};

static const AVEMClass dummy_v1_class = {
    .class_name = "dummy_v1_class",
    .item_name  = av_em_default_item_name,
    .option     = dummy_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVEMClass dummy_v2_class = {
    .class_name = "dummy_v2_class",
    .item_name  = av_em_default_item_name,
    .option     = dummy_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* codec with options */
static AVEMCodec dummy_v1_encoder = {
    .name             = "dummy_v1_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 1,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_class       = &dummy_v1_class,
    .priv_data_size   = sizeof(Dummy12Context),
};

/* codec with options, different class */
static AVEMCodec dummy_v2_encoder = {
    .name             = "dummy_v2_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 2,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_class       = &dummy_v2_class,
    .priv_data_size   = sizeof(Dummy12Context),
};

/* codec with priv data, but no class */
static AVEMCodec dummy_v3_encoder = {
    .name             = "dummy_v3_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 3,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
    .priv_data_size   = sizeof(Dummy3Context),
};

/* codec without priv data */
static AVEMCodec dummy_v4_encoder = {
    .name             = "dummy_v4_codec",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_NONE - 4,
    .encode2          = dummy_encode,
    .init             = dummy_init,
    .close            = dummy_close,
};

static void test_copy_print_codec(const AVEMCodecContext *ctx)
{
    printf("%-14s: %dx%d prv: %s",
           ctx->codec ? ctx->codec->name : "NULL",
           ctx->width, ctx->height,
           ctx->priv_data ? "set" : "null");
    if (ctx->codec && ctx->codec->priv_class && ctx->codec->priv_data_size) {
        int64_t i64;
        char *str = NULL;
        av_em_opt_get_int(ctx->priv_data, "num", 0, &i64);
        av_em_opt_get(ctx->priv_data, "str", 0, (uint8_t**)&str);
        printf(" opts: %"PRId64" %s", i64, str);
        av_em_free(str);
    }
    printf("\n");
}

static void test_copy(const AVEMCodec *c1, const AVEMCodec *c2)
{
    AVEMCodecContext *ctx1, *ctx2;
    printf("%s -> %s\nclosed:\n", c1 ? c1->name : "NULL", c2 ? c2->name : "NULL");
    ctx1 = avcodec_em_alloc_context3(c1);
    ctx2 = avcodec_em_alloc_context3(c2);
    ctx1->width = ctx1->height = 128;
    if (ctx2->codec && ctx2->codec->priv_class && ctx2->codec->priv_data_size) {
        av_em_opt_set(ctx2->priv_data, "num", "667", 0);
        av_em_opt_set(ctx2->priv_data, "str", "i'm dest value before copy", 0);
    }
    avcodec_em_copy_context(ctx2, ctx1);
    test_copy_print_codec(ctx1);
    test_copy_print_codec(ctx2);
    if (ctx1->codec) {
        printf("opened:\n");
        avcodec_em_open2(ctx1, ctx1->codec, NULL);
        if (ctx2->codec && ctx2->codec->priv_class && ctx2->codec->priv_data_size) {
            av_em_opt_set(ctx2->priv_data, "num", "667", 0);
            av_em_opt_set(ctx2->priv_data, "str", "i'm dest value before copy", 0);
        }
        avcodec_em_copy_context(ctx2, ctx1);
        test_copy_print_codec(ctx1);
        test_copy_print_codec(ctx2);
        avcodec_em_close(ctx1);
    }
    avcodec_em_free_context(&ctx1);
    avcodec_em_free_context(&ctx2);
}

int main(void)
{
    AVEMCodec *dummy_codec[] = {
        &dummy_v1_encoder,
        &dummy_v2_encoder,
        &dummy_v3_encoder,
        &dummy_v4_encoder,
        NULL,
    };
    int i, j;

    for (i = 0; dummy_codec[i]; i++)
        avcodec_em_register(dummy_codec[i]);

    printf("testing avcodec_em_copy_context()\n");
    for (i = 0; i < FF_ARRAY_ELEMS(dummy_codec); i++)
        for (j = 0; j < FF_ARRAY_ELEMS(dummy_codec); j++)
            test_copy(dummy_codec[i], dummy_codec[j]);
    return 0;
}
#endif
