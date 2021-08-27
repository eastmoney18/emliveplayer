/*
 * Format register and lookup
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

#include "libavutil/atomic.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "avio_internal.h"
#include "avformat.h"
#include "id3v2.h"
#include "internal.h"


/**
 * @file
 * Format register and lookup
 */
/** head of registered input format linked list */
static AVEMInputFormat *first_iformat = NULL;
/** head of registered output format linked list */
static AVEMOutputFormat *first_oformat = NULL;

static AVEMInputFormat **last_iformat = &first_iformat;
static AVEMOutputFormat **last_oformat = &first_oformat;

AVEMInputFormat *av_em_iformat_next(const AVEMInputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_iformat;
}

AVEMOutputFormat *av_em_oformat_next(const AVEMOutputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_oformat;
}

void av_em_register_input_format(AVEMInputFormat *format)
{
    AVEMInputFormat **p = last_iformat;

    // Note, format could be added after the first 2 checks but that implies that *p is no longer NULL
    while(p != &format->next && !format->next && avpriv_em_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;

    if (!format->next)
        last_iformat = &format->next;
}

void av_em_register_output_format(AVEMOutputFormat *format)
{
    AVEMOutputFormat **p = last_oformat;

    // Note, format could be added after the first 2 checks but that implies that *p is no longer NULL
    while(p != &format->next && !format->next && avpriv_em_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;

    if (!format->next)
        last_oformat = &format->next;
}

int av_em_match_ext(const char *filename, const char *extensions)
{
    const char *ext;

    if (!filename)
        return 0;

    ext = strrchr(filename, '.');
    if (ext)
        return av_em_match_name(ext + 1, extensions);
    return 0;
}

AVEMOutputFormat *av_em_guess_format(const char *short_name, const char *filename,
                                const char *mime_type)
{
    AVEMOutputFormat *fmt = NULL, *fmt_found;
    int score_max, score;

    /* specific test for image sequences */
#if CONFIG_IMAGE2_MUXER
    if (!short_name && filename &&
        av_em_filename_number_test(filename) &&
        em_guess_image2_codec(filename) != AV_CODEC_ID_NONE) {
        return av_em_guess_format("image2", NULL, NULL);
    }
#endif
    /* Find the proper file type. */
    fmt_found = NULL;
    score_max = 0;
    while ((fmt = av_em_oformat_next(fmt))) {
        score = 0;
        if (fmt->name && short_name && av_em_match_name(short_name, fmt->name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            av_em_match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
    }
    return fmt_found;
}

enum AVEMCodecID av_em_guess_codec(AVEMOutputFormat *fmt, const char *short_name,
                              const char *filename, const char *mime_type,
                              enum AVEMMediaType type)
{
    if (av_em_match_name("segment", fmt->name) || av_em_match_name("ssegment", fmt->name)) {
        AVEMOutputFormat *fmt2 = av_em_guess_format(NULL, filename, NULL);
        if (fmt2)
            fmt = fmt2;
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        enum AVEMCodecID codec_id = AV_CODEC_ID_NONE;

#if CONFIG_IMAGE2_MUXER
        if (!strcmp(fmt->name, "image2") || !strcmp(fmt->name, "image2pipe")) {
            codec_id = em_guess_image2_codec(filename);
        }
#endif
        if (codec_id == AV_CODEC_ID_NONE)
            codec_id = fmt->video_codec;
        return codec_id;
    } else if (type == AVMEDIA_TYPE_AUDIO)
        return fmt->audio_codec;
    else if (type == AVMEDIA_TYPE_SUBTITLE)
        return fmt->subtitle_codec;
    else if (type == AVMEDIA_TYPE_DATA)
        return fmt->data_codec;
    else
        return AV_CODEC_ID_NONE;
}

AVEMInputFormat *av_em_find_input_format(const char *short_name)
{
    AVEMInputFormat *fmt = NULL;
    while ((fmt = av_em_iformat_next(fmt)))
        if (av_em_match_name(short_name, fmt->name))
            return fmt;
    return NULL;
}

AVEMInputFormat *av_em_probe_input_format3(AVEMProbeData *pd, int is_opened,
                                      int *score_ret)
{
    AVEMProbeData lpd = *pd;
    AVEMInputFormat *fmt1 = NULL, *fmt;
    int score, score_max = 0;
    const static uint8_t zerobuffer[AVPROBE_PADDING_SIZE];
    enum nodat {
        NO_ID3,
        ID3_ALMOST_GREATER_PROBE,
        ID3_GREATER_PROBE,
        ID3_GREATER_MAX_PROBE,
    } nodat = NO_ID3;

    if (!lpd.buf)
        lpd.buf = (unsigned char *) zerobuffer;

    if (lpd.buf_size > 10 && ff_id3v2_match(lpd.buf, ID3v2_DEFAULT_MAGIC)) {
        int id3len = ff_id3v2_tag_len(lpd.buf);
        if (lpd.buf_size > id3len + 16) {
            if (lpd.buf_size < 2LL*id3len + 16)
                nodat = ID3_ALMOST_GREATER_PROBE;
            lpd.buf      += id3len;
            lpd.buf_size -= id3len;
        } else if (id3len >= PROBE_BUF_MAX) {
            nodat = ID3_GREATER_MAX_PROBE;
        } else
            nodat = ID3_GREATER_PROBE;
    }

    fmt = NULL;
    while ((fmt1 = av_em_iformat_next(fmt1))) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE) && strcmp(fmt1->name, "image2"))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(&lpd);
			av_em_log(NULL, AV_LOG_TRACE, "Probing %s score:%d size:%d\n", fmt1->name, score, lpd.buf_size);
                
            if (fmt1->extensions && av_em_match_ext(lpd.filename, fmt1->extensions)) {
                switch (nodat) {
                case NO_ID3:
                    score = FFMAX(score, 1);
                    break;
                case ID3_GREATER_PROBE:
                case ID3_ALMOST_GREATER_PROBE:
                    score = FFMAX(score, AVPROBE_SCORE_EXTENSION / 2 - 1);
                    break;
                case ID3_GREATER_MAX_PROBE:
                    score = FFMAX(score, AVPROBE_SCORE_EXTENSION);
                    break;
                }
            }
        } else if (fmt1->extensions) {
            if (av_em_match_ext(lpd.filename, fmt1->extensions))
                score = AVPROBE_SCORE_EXTENSION;
        }
        if (av_em_match_name(lpd.mime_type, fmt1->mime_type)) {
            if (AVPROBE_SCORE_MIME > score) {
                av_em_log(NULL, AV_LOG_DEBUG, "Probing %s score:%d increased to %d due to MIME type\n", fmt1->name, score, AVPROBE_SCORE_MIME);
                score = AVPROBE_SCORE_MIME;
            }
        }
        if (score > score_max) {
            score_max = score;
            fmt       = fmt1;
			if (score_max == AVPROBE_SCORE_MAX) {        
				av_em_log(NULL, AV_LOG_DEBUG, "probing score is already exceed max score.\n");
				break;
			}
        } else if (score == score_max)
            fmt = NULL;
    }
    if (nodat == ID3_GREATER_PROBE)
        score_max = FFMIN(AVPROBE_SCORE_EXTENSION / 2 - 1, score_max);
    *score_ret = score_max;

    return fmt;
}

AVEMInputFormat *av_em_probe_input_format2(AVEMProbeData *pd, int is_opened, int *score_max)
{
    int score_ret;
    AVEMInputFormat *fmt = av_em_probe_input_format3(pd, is_opened, &score_ret);
    if (score_ret > *score_max) {
        *score_max = score_ret;
        return fmt;
    } else
        return NULL;
}

AVEMInputFormat *av_em_probe_input_format(AVEMProbeData *pd, int is_opened)
{
    int score = 0;
    return av_em_probe_input_format2(pd, is_opened, &score);
}

int av_em_probe_input_buffer2(AVEMIOContext *pb, AVEMInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    AVEMProbeData pd = { filename ? filename : "" };
    uint8_t *buf = NULL;
    int ret = 0, probe_size, buf_offset = 0;
    int score = 0;
    int ret2;

    if (!max_probe_size)
        max_probe_size = PROBE_BUF_MAX;
    else if (max_probe_size < PROBE_BUF_MIN) {
        av_em_log(logctx, AV_LOG_ERROR,
               "Specified probe size value %u cannot be < %u\n", max_probe_size, PROBE_BUF_MIN);
        return AVERROR(EINVAL);
    }

    if (offset >= max_probe_size)
        return AVERROR(EINVAL);

    if (pb->av_class) {
        uint8_t *mime_type_opt = NULL;
        char *semi;
        av_em_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type_opt);
        pd.mime_type = (const char *)mime_type_opt;
        semi = pd.mime_type ? strchr(pd.mime_type, ';') : NULL;
        if (semi) {
            *semi = '\0';
        }
    }
#if 0
    if (!*fmt && pb->av_class && av_em_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type) >= 0 && mime_type) {
        if (!av_em_strcasecmp(mime_type, "audio/aacp")) {
            *fmt = av_em_find_input_format("aac");
        }
        av_em_freep(&mime_type);
    }
#endif

    for (probe_size = PROBE_BUF_MIN; probe_size <= max_probe_size && !*fmt;
         probe_size = FFMIN(probe_size << 1,
                            FFMAX(max_probe_size, probe_size + 1))) {
        score = probe_size < max_probe_size ? AVPROBE_SCORE_RETRY : 0;

        /* Read probe data. */
        if ((ret = av_em_reallocp(&buf, probe_size + AVPROBE_PADDING_SIZE)) < 0)
            goto fail;
        if ((ret = avio_em_read(pb, buf + buf_offset,
                             probe_size - buf_offset)) < 0) {
            /* Fail if error was not end of file, otherwise, lower score. */
            if (ret != AVERROR_EOF)
                goto fail;

            score = 0;
            ret   = 0;          /* error was end of file, nothing read */
        }
        buf_offset += ret;
        if (buf_offset < offset)
            continue;
        pd.buf_size = buf_offset - offset;
        pd.buf = &buf[offset];

        memset(pd.buf + pd.buf_size, 0, AVPROBE_PADDING_SIZE);

        /* Guess file format. */
        *fmt = av_em_probe_input_format2(&pd, 1, &score);
        if (*fmt) {
            /* This can only be true in the last iteration. */
            if (score <= AVPROBE_SCORE_RETRY) {
                av_em_log(logctx, AV_LOG_WARNING,
                       "Format %s detected only with low score of %d, "
                       "misdetection possible!\n", (*fmt)->name, score);
            } else
                av_em_log(logctx, AV_LOG_DEBUG,
                       "Format %s probed with size=%d and score=%d\n",
                       (*fmt)->name, probe_size, score);
#if 0
            FILE *f = fopen("probestat.tmp", "ab");
            fprintf(f, "probe_size:%d format:%s score:%d filename:%s\n", probe_size, (*fmt)->name, score, filename);
            fclose(f);
#endif
        }
    }

    if (!*fmt)
        ret = AVERROR_INVALIDDATA;

fail:
    /* Rewind. Reuse probe buffer to avoid seeking. */
    ret2 = emio_rewind_with_probe_data(pb, &buf, buf_offset);
    if (ret >= 0)
        ret = ret2;

    av_em_freep(&pd.mime_type);
    return ret < 0 ? ret : score;
}

int av_em_probe_input_buffer(AVEMIOContext *pb, AVEMInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size)
{
    int ret = av_em_probe_input_buffer2(pb, fmt, filename, logctx, offset, max_probe_size);
    return ret < 0 ? ret : 0;
}
