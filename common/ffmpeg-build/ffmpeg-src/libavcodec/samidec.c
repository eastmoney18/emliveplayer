/*
 * Copyright (c) 2012 Clément Bœsch
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
 * SAMI subtitle decoder
 * @see http://msdn.microsoft.com/en-us/library/ms971327.aspx
 */

#include "ass.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "htmlsubtitles.h"

typedef struct {
    AVEMBPrint source;
    AVEMBPrint content;
    AVEMBPrint encoded_source;
    AVEMBPrint encoded_content;
    AVEMBPrint full;
    int readorder;
} SAMIContext;

static int sami_paragraph_to_ass(AVEMCodecContext *avctx, const char *src)
{
    SAMIContext *sami = avctx->priv_data;
    int ret = 0;
    char *tag = NULL;
    char *dupsrc = av_em_strdup(src);
    char *p = dupsrc;
    AVEMBPrint *dst_content = &sami->encoded_content;
    AVEMBPrint *dst_source = &sami->encoded_source;

    av_em_bprint_clear(&sami->encoded_content);
    av_em_bprint_clear(&sami->content);
    av_em_bprint_clear(&sami->encoded_source);
    for (;;) {
        char *saveptr = NULL;
        int prev_chr_is_space = 0;
        AVEMBPrint *dst = &sami->content;

        /* parse & extract paragraph tag */
        p = av_em_stristr(p, "<P");
        if (!p)
            break;
        if (p[2] != '>' && !av_isspace(p[2])) { // avoid confusion with tags such as <PRE>
            p++;
            continue;
        }
        if (dst->len) // add a separator with the previous paragraph if there was one
            av_em_bprintf(dst, "\\N");
        tag = av_em_strtok(p, ">", &saveptr);
        if (!tag || !saveptr)
            break;
        p = saveptr;

        /* check if the current paragraph is the "source" (speaker name) */
        if (av_em_stristr(tag, "ID=Source") || av_em_stristr(tag, "ID=\"Source\"")) {
            dst = &sami->source;
            av_em_bprint_clear(dst);
        }

        /* if empty event -> skip subtitle */
        while (av_isspace(*p))
            p++;
        if (!strncmp(p, "&nbsp;", 6)) {
            ret = -1;
            goto end;
        }

        /* extract the text, stripping most of the tags */
        while (*p) {
            if (*p == '<') {
                if (!av_em_strncasecmp(p, "<P", 2) && (p[2] == '>' || av_isspace(p[2])))
                    break;
            }
            if (!av_em_strncasecmp(p, "<BR", 3)) {
                av_em_bprintf(dst, "\\N");
                p++;
                while (*p && *p != '>')
                    p++;
                if (!*p)
                    break;
                if (*p == '>')
                    p++;
                continue;
            }
            if (!av_isspace(*p))
                av_em_bprint_chars(dst, *p, 1);
            else if (!prev_chr_is_space)
                av_em_bprint_chars(dst, ' ', 1);
            prev_chr_is_space = av_isspace(*p);
            p++;
        }
    }

    av_em_bprint_clear(&sami->full);
    if (sami->source.len) {
        em_htmlmarkup_to_ass(avctx, dst_source, sami->source.str);
        av_em_bprintf(&sami->full, "{\\i1}%s{\\i0}\\N", sami->encoded_source.str);
    }
    em_htmlmarkup_to_ass(avctx, dst_content, sami->content.str);
    av_em_bprintf(&sami->full, "%s", sami->encoded_content.str);

end:
    av_em_free(dupsrc);
    return ret;
}

static int sami_decode_frame(AVEMCodecContext *avctx,
                             void *data, int *got_sub_ptr, AVEMPacket *avpkt)
{
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    SAMIContext *sami = avctx->priv_data;

    if (ptr && avpkt->size > 0 && !sami_paragraph_to_ass(avctx, ptr)) {
        // TODO: pass escaped sami->encoded_source.str as source
        int ret = ff_ass_add_rect(sub, sami->full.str, sami->readorder++, 0, NULL, NULL);
        if (ret < 0)
            return ret;
    }
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

static av_cold int sami_init(AVEMCodecContext *avctx)
{
    SAMIContext *sami = avctx->priv_data;
    av_em_bprint_init(&sami->source,  0, 2048);
    av_em_bprint_init(&sami->content, 0, 2048);
    av_em_bprint_init(&sami->encoded_source,  0, 2048);
    av_em_bprint_init(&sami->encoded_content, 0, 2048);
    av_em_bprint_init(&sami->full,    0, 2048);
    return ff_ass_subtitle_header_default(avctx);
}

static av_cold int sami_close(AVEMCodecContext *avctx)
{
    SAMIContext *sami = avctx->priv_data;
    av_em_bprint_finalize(&sami->source,  NULL);
    av_em_bprint_finalize(&sami->content, NULL);
    av_em_bprint_finalize(&sami->encoded_source,  NULL);
    av_em_bprint_finalize(&sami->encoded_content, NULL);
    av_em_bprint_finalize(&sami->full,    NULL);
    return 0;
}

static void sami_flush(AVEMCodecContext *avctx)
{
    SAMIContext *sami = avctx->priv_data;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        sami->readorder = 0;
}

AVEMCodec ff_sami_decoder = {
    .name           = "sami",
    .long_name      = NULL_IF_CONFIG_SMALL("SAMI subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_SAMI,
    .priv_data_size = sizeof(SAMIContext),
    .init           = sami_init,
    .close          = sami_close,
    .decode         = sami_decode_frame,
    .flush          = sami_flush,
};
