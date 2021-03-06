/*
 * AVEMPacket functions for libavcodec
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

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

void av_em_init_packet(AVEMPacket *pkt)
{
    pkt->pts                  = AV_NOPTS_VALUE;
    pkt->dts                  = AV_NOPTS_VALUE;
    pkt->pos                  = -1;
    pkt->duration             = 0;
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->convergence_duration = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    pkt->flags                = 0;
    pkt->stream_index         = 0;
    pkt->buf                  = NULL;
    pkt->side_data            = NULL;
    pkt->side_data_elems      = 0;
}

AVEMPacket *av_em_packet_alloc(void)
{
    AVEMPacket *pkt = av_em_mallocz(sizeof(AVEMPacket));
    if (!pkt)
        return pkt;

    av_em_packet_unref(pkt);

    return pkt;
}

void av_em_packet_free(AVEMPacket **pkt)
{
    if (!pkt || !*pkt)
        return;

    av_em_packet_unref(*pkt);
    av_em_freep(pkt);
}

static int packet_alloc(AVEMBufferRef **buf, int size)
{
    int ret;
    if (size < 0 || size >= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    ret = av_em_buffer_realloc(buf, size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (ret < 0)
        return ret;

    memset((*buf)->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int av_em_new_packet(AVEMPacket *pkt, int size)
{
    AVEMBufferRef *buf = NULL;
    int ret = packet_alloc(&buf, size);
    if (ret < 0)
        return ret;

    av_em_init_packet(pkt);
    pkt->buf      = buf;
    pkt->data     = buf->data;
    pkt->size     = size;

    return 0;
}

void av_em_shrink_packet(AVEMPacket *pkt, int size)
{
    if (pkt->size <= size)
        return;
    pkt->size = size;
    memset(pkt->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
}

int av_em_grow_packet(AVEMPacket *pkt, int grow_by)
{
    int new_size;
    av_assert0((unsigned)pkt->size <= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE);
    if ((unsigned)grow_by >
        INT_MAX - (pkt->size + AV_INPUT_BUFFER_PADDING_SIZE))
        return -1;

    new_size = pkt->size + grow_by + AV_INPUT_BUFFER_PADDING_SIZE;
    if (pkt->buf) {
        size_t data_offset;
        uint8_t *old_data = pkt->data;
        if (pkt->data == NULL) {
            data_offset = 0;
            pkt->data = pkt->buf->data;
        } else {
            data_offset = pkt->data - pkt->buf->data;
            if (data_offset > INT_MAX - new_size)
                return -1;
        }

        if (new_size + data_offset > pkt->buf->size) {
            int ret = av_em_buffer_realloc(&pkt->buf, new_size + data_offset);
            if (ret < 0) {
                pkt->data = old_data;
                return ret;
            }
            pkt->data = pkt->buf->data + data_offset;
        }
    } else {
        pkt->buf = av_em_buffer_alloc(new_size);
        if (!pkt->buf)
            return AVERROR(ENOMEM);
        memcpy(pkt->buf->data, pkt->data, pkt->size);
        pkt->data = pkt->buf->data;
    }
    pkt->size += grow_by;
    memset(pkt->data + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int av_em_packet_from_data(AVEMPacket *pkt, uint8_t *data, int size)
{
    if (size >= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    pkt->buf = av_em_buffer_create(data, size + AV_INPUT_BUFFER_PADDING_SIZE,
                                av_em_buffer_default_free, NULL, 0);
    if (!pkt->buf)
        return AVERROR(ENOMEM);

    pkt->data = data;
    pkt->size = size;

    return 0;
}

#if FF_API_AVPACKET_OLD_API
FF_DISABLE_DEPRECATION_WARNINGS
#define ALLOC_MALLOC(data, size) data = av_em_alloc(size)
#define ALLOC_BUF(data, size)                \
do {                                         \
    av_em_buffer_realloc(&pkt->buf, size);      \
    data = pkt->buf ? pkt->buf->data : NULL; \
} while (0)

#define DUP_DATA(dst, src, size, padding, ALLOC)                        \
    do {                                                                \
        void *data;                                                     \
        if (padding) {                                                  \
            if ((unsigned)(size) >                                      \
                (unsigned)(size) + AV_INPUT_BUFFER_PADDING_SIZE)        \
                goto failed_alloc;                                      \
            ALLOC(data, size + AV_INPUT_BUFFER_PADDING_SIZE);           \
        } else {                                                        \
            ALLOC(data, size);                                          \
        }                                                               \
        if (!data)                                                      \
            goto failed_alloc;                                          \
        memcpy(data, src, size);                                        \
        if (padding)                                                    \
            memset((uint8_t *)data + size, 0,                           \
                   AV_INPUT_BUFFER_PADDING_SIZE);                       \
        dst = data;                                                     \
    } while (0)

/* Makes duplicates of data, side_data, but does not copy any other fields */
static int copy_packet_data(AVEMPacket *pkt, const AVEMPacket *src, int dup)
{
    pkt->data      = NULL;
    pkt->side_data = NULL;
    if (pkt->buf) {
        AVEMBufferRef *ref = av_em_buffer_ref(src->buf);
        if (!ref)
            return AVERROR(ENOMEM);
        pkt->buf  = ref;
        pkt->data = ref->data;
    } else {
        DUP_DATA(pkt->data, src->data, pkt->size, 1, ALLOC_BUF);
    }
    if (pkt->side_data_elems && dup)
        pkt->side_data = src->side_data;
    if (pkt->side_data_elems && !dup) {
        return av_em_copy_packet_side_data(pkt, src);
    }
    return 0;

failed_alloc:
    av_em_packet_unref(pkt);
    return AVERROR(ENOMEM);
}

int av_em_copy_packet_side_data(AVEMPacket *pkt, const AVEMPacket *src)
{
    if (src->side_data_elems) {
        int i;
        DUP_DATA(pkt->side_data, src->side_data,
                src->side_data_elems * sizeof(*src->side_data), 0, ALLOC_MALLOC);
        if (src != pkt) {
            memset(pkt->side_data, 0,
                   src->side_data_elems * sizeof(*src->side_data));
        }
        for (i = 0; i < src->side_data_elems; i++) {
            DUP_DATA(pkt->side_data[i].data, src->side_data[i].data,
                    src->side_data[i].size, 1, ALLOC_MALLOC);
            pkt->side_data[i].size = src->side_data[i].size;
            pkt->side_data[i].type = src->side_data[i].type;
        }
    }
    pkt->side_data_elems = src->side_data_elems;
    return 0;

failed_alloc:
    av_em_packet_unref(pkt);
    return AVERROR(ENOMEM);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int av_em_dup_packet(AVEMPacket *pkt)
{
    AVEMPacket tmp_pkt;

    if (!pkt->buf && pkt->data) {
        tmp_pkt = *pkt;
        return copy_packet_data(pkt, &tmp_pkt, 1);
    }
    return 0;
}

int av_em_copy_packet(AVEMPacket *dst, const AVEMPacket *src)
{
    *dst = *src;
    return copy_packet_data(dst, src, 0);
}

void av_em_packet_free_side_data(AVEMPacket *pkt)
{
    int i;
    for (i = 0; i < pkt->side_data_elems; i++)
        av_em_freep(&pkt->side_data[i].data);
    av_em_freep(&pkt->side_data);
    pkt->side_data_elems = 0;
}

#if FF_API_AVPACKET_OLD_API
FF_DISABLE_DEPRECATION_WARNINGS
void av_em_free_packet(AVEMPacket *pkt)
{
    if (pkt) {
        if (pkt->buf)
            av_em_buffer_unref(&pkt->buf);
        pkt->data            = NULL;
        pkt->size            = 0;

        av_em_packet_free_side_data(pkt);
    }
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int av_em_packet_add_side_data(AVEMPacket *pkt, enum AVEMPacketSideDataType type,
                            uint8_t *data, size_t size)
{
    int elems = pkt->side_data_elems;

    if ((unsigned)elems + 1 > INT_MAX / sizeof(*pkt->side_data))
        return AVERROR(ERANGE);

    pkt->side_data = av_em_realloc(pkt->side_data,
                                (elems + 1) * sizeof(*pkt->side_data));
    if (!pkt->side_data)
        return AVERROR(ENOMEM);

    pkt->side_data[elems].data = data;
    pkt->side_data[elems].size = size;
    pkt->side_data[elems].type = type;
    pkt->side_data_elems++;

    return 0;
}


uint8_t *av_em_packet_new_side_data(AVEMPacket *pkt, enum AVEMPacketSideDataType type,
                                 int size)
{
    int ret;
    uint8_t *data;

    if ((unsigned)size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return NULL;
    data = av_em_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return NULL;

    ret = av_em_packet_add_side_data(pkt, type, data, size);
    if (ret < 0) {
        av_em_freep(&data);
        return NULL;
    }

    return data;
}

uint8_t *av_em_packet_get_side_data(AVEMPacket *pkt, enum AVEMPacketSideDataType type,
                                 int *size)
{
    int i;

    for (i = 0; i < pkt->side_data_elems; i++) {
        if (pkt->side_data[i].type == type) {
            if (size)
                *size = pkt->side_data[i].size;
            return pkt->side_data[i].data;
        }
    }
    return NULL;
}

const char *av_em_packet_side_data_name(enum AVEMPacketSideDataType type)
{
    switch(type) {
    case AV_PKT_DATA_PALETTE:                    return "Palette";
    case AV_PKT_DATA_NEW_EXTRADATA:              return "New Extradata";
    case AV_PKT_DATA_PARAM_CHANGE:               return "Param Change";
    case AV_PKT_DATA_H263_MB_INFO:               return "H263 MB Info";
    case AV_PKT_DATA_REPLAYGAIN:                 return "Replay Gain";
    case AV_PKT_DATA_DISPLAYMATRIX:              return "Display Matrix";
    case AV_PKT_DATA_STEREO3D:                   return "Stereo 3D";
    case AV_PKT_DATA_AUDIO_SERVICE_TYPE:         return "Audio Service Type";
    case AV_PKT_DATA_SKIP_SAMPLES:               return "Skip Samples";
    case AV_PKT_DATA_JP_DUALMONO:                return "JP Dual Mono";
    case AV_PKT_DATA_STRINGS_METADATA:           return "Strings Metadata";
    case AV_PKT_DATA_SUBTITLE_POSITION:          return "Subtitle Position";
    case AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL:   return "Matroska BlockAdditional";
    case AV_PKT_DATA_WEBVTT_IDENTIFIER:          return "WebVTT ID";
    case AV_PKT_DATA_WEBVTT_SETTINGS:            return "WebVTT Settings";
    case AV_PKT_DATA_METADATA_UPDATE:            return "Metadata Update";
    case AV_PKT_DATA_MPEGTS_STREAM_ID:           return "MPEGTS Stream ID";
    case AV_PKT_DATA_MASTERING_DISPLAY_METADATA: return "Mastering display metadata";
    }
    return NULL;
}

#define FF_MERGE_MARKER 0x8c4d9d108e25e9feULL

int av_em_packet_merge_side_data(AVEMPacket *pkt){
    if(pkt->side_data_elems){
        AVEMBufferRef *buf;
        int i;
        uint8_t *p;
        uint64_t size= pkt->size + 8LL + AV_INPUT_BUFFER_PADDING_SIZE;
        AVEMPacket old= *pkt;
        for (i=0; i<old.side_data_elems; i++) {
            size += old.side_data[i].size + 5LL;
        }
        if (size > INT_MAX)
            return AVERROR(EINVAL);
        buf = av_em_buffer_alloc(size);
        if (!buf)
            return AVERROR(ENOMEM);
        pkt->buf = buf;
        pkt->data = p = buf->data;
        pkt->size = size - AV_INPUT_BUFFER_PADDING_SIZE;
        bytestream_put_buffer(&p, old.data, old.size);
        for (i=old.side_data_elems-1; i>=0; i--) {
            bytestream_put_buffer(&p, old.side_data[i].data, old.side_data[i].size);
            bytestream_put_be32(&p, old.side_data[i].size);
            *p++ = old.side_data[i].type | ((i==old.side_data_elems-1)*128);
        }
        bytestream_put_be64(&p, FF_MERGE_MARKER);
        av_assert0(p-pkt->data == pkt->size);
        memset(p, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        av_em_packet_unref(&old);
        pkt->side_data_elems = 0;
        pkt->side_data = NULL;
        return 1;
    }
    return 0;
}

int av_em_packet_split_side_data(AVEMPacket *pkt){
    if (!pkt->side_data_elems && pkt->size >12 && AV_RB64(pkt->data + pkt->size - 8) == FF_MERGE_MARKER){
        int i;
        unsigned int size;
        uint8_t *p;

        p = pkt->data + pkt->size - 8 - 5;
        for (i=1; ; i++){
            size = AV_RB32(p);
            if (size>INT_MAX - 5 || p - pkt->data < size)
                return 0;
            if (p[4]&128)
                break;
            if (p - pkt->data < size + 5)
                return 0;
            p-= size+5;
        }

        pkt->side_data = av_em_malloc_array(i, sizeof(*pkt->side_data));
        if (!pkt->side_data)
            return AVERROR(ENOMEM);

        p= pkt->data + pkt->size - 8 - 5;
        for (i=0; ; i++){
            size= AV_RB32(p);
            av_assert0(size<=INT_MAX - 5 && p - pkt->data >= size);
            pkt->side_data[i].data = av_em_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
            pkt->side_data[i].size = size;
            pkt->side_data[i].type = p[4]&127;
            if (!pkt->side_data[i].data)
                return AVERROR(ENOMEM);
            memcpy(pkt->side_data[i].data, p-size, size);
            pkt->size -= size + 5;
            if(p[4]&128)
                break;
            p-= size+5;
        }
        pkt->size -= 8;
        pkt->side_data_elems = i+1;
        return 1;
    }
    return 0;
}

uint8_t *av_em_packet_pack_dictionary(AVEMDictionary *dict, int *size)
{
    AVEMDictionaryEntry *t = NULL;
    uint8_t *data = NULL;
    *size = 0;

    if (!dict)
        return NULL;

    while ((t = av_em_dict_get(dict, "", t, AV_DICT_IGNORE_SUFFIX))) {
        const size_t keylen   = strlen(t->key);
        const size_t valuelen = strlen(t->value);
        const size_t new_size = *size + keylen + 1 + valuelen + 1;
        uint8_t *const new_data = av_em_realloc(data, new_size);

        if (!new_data)
            goto fail;
        data = new_data;
        if (new_size > INT_MAX)
            goto fail;

        memcpy(data + *size, t->key, keylen + 1);
        memcpy(data + *size + keylen + 1, t->value, valuelen + 1);

        *size = new_size;
    }

    return data;

fail:
    av_em_freep(&data);
    *size = 0;
    return NULL;
}

int av_em_packet_unpack_dictionary(const uint8_t *data, int size, AVEMDictionary **dict)
{
    const uint8_t *end = data + size;
    int ret = 0;

    if (!dict || !data || !size)
        return ret;
    if (size && end[-1])
        return AVERROR_INVALIDDATA;
    while (data < end) {
        const uint8_t *key = data;
        const uint8_t *val = data + strlen(key) + 1;

        if (val >= end)
            return AVERROR_INVALIDDATA;

        ret = av_em_dict_set(dict, key, val, 0);
        if (ret < 0)
            break;
        data = val + strlen(val) + 1;
    }

    return ret;
}

int av_em_packet_shrink_side_data(AVEMPacket *pkt, enum AVEMPacketSideDataType type,
                               int size)
{
    int i;

    for (i = 0; i < pkt->side_data_elems; i++) {
        if (pkt->side_data[i].type == type) {
            if (size > pkt->side_data[i].size)
                return AVERROR(ENOMEM);
            pkt->side_data[i].size = size;
            return 0;
        }
    }
    return AVERROR(ENOENT);
}

int av_em_packet_copy_props(AVEMPacket *dst, const AVEMPacket *src)
{
    int i;

    dst->pts                  = src->pts;
    dst->dts                  = src->dts;
    dst->pos                  = src->pos;
    dst->duration             = src->duration;
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    dst->convergence_duration = src->convergence_duration;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    dst->flags                = src->flags;
    dst->stream_index         = src->stream_index;

    for (i = 0; i < src->side_data_elems; i++) {
         enum AVEMPacketSideDataType type = src->side_data[i].type;
         int size          = src->side_data[i].size;
         uint8_t *src_data = src->side_data[i].data;
         uint8_t *dst_data = av_em_packet_new_side_data(dst, type, size);

        if (!dst_data) {
            av_em_packet_free_side_data(dst);
            return AVERROR(ENOMEM);
        }
        memcpy(dst_data, src_data, size);
    }

    return 0;
}

void av_em_packet_unref(AVEMPacket *pkt)
{
    av_em_packet_free_side_data(pkt);
    av_em_buffer_unref(&pkt->buf);
    av_em_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
}

int av_em_packet_ref(AVEMPacket *dst, const AVEMPacket *src)
{
    int ret;

    ret = av_em_packet_copy_props(dst, src);
    if (ret < 0)
        return ret;

    if (!src->buf) {
        ret = packet_alloc(&dst->buf, src->size);
        if (ret < 0)
            goto fail;
        memcpy(dst->buf->data, src->data, src->size);

        dst->data = dst->buf->data;
    } else {
        dst->buf = av_em_buffer_ref(src->buf);
        if (!dst->buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst->data = src->data;
    }

    dst->size = src->size;

    return 0;
fail:
    av_em_packet_free_side_data(dst);
    return ret;
}

AVEMPacket *av_em_packet_clone(AVEMPacket *src)
{
    AVEMPacket *ret = av_em_packet_alloc();

    if (!ret)
        return ret;

    if (av_em_packet_ref(ret, src))
        av_em_packet_free(&ret);

    return ret;
}

void av_em_packet_move_ref(AVEMPacket *dst, AVEMPacket *src)
{
    *dst = *src;
    av_em_init_packet(src);
    src->data = NULL;
    src->size = 0;
}

void av_em_packet_rescale_ts(AVEMPacket *pkt, AVEMRational src_tb, AVEMRational dst_tb)
{
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_em_rescale_q(pkt->pts, src_tb, dst_tb);
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_em_rescale_q(pkt->dts, src_tb, dst_tb);
    if (pkt->duration > 0)
        pkt->duration = av_em_rescale_q(pkt->duration, src_tb, dst_tb);
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    if (pkt->convergence_duration > 0)
        pkt->convergence_duration = av_em_rescale_q(pkt->convergence_duration, src_tb, dst_tb);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
}

int em_side_data_set_encoder_stats(AVEMPacket *pkt, int quality, int64_t *error, int error_count, int pict_type)
{
    uint8_t *side_data;
    int side_data_size;
    int i;

    side_data = av_em_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS, &side_data_size);
    if (!side_data) {
        side_data_size = 4+4+8*error_count;
        side_data = av_em_packet_new_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                            side_data_size);
    }

    if (!side_data || side_data_size < 4+4+8*error_count)
        return AVERROR(ENOMEM);

    AV_WL32(side_data   , quality  );
    side_data[4] = pict_type;
    side_data[5] = error_count;
    for (i = 0; i<error_count; i++)
        AV_WL64(side_data+8 + 8*i , error[i]);

    return 0;
}
