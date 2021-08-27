/*
 * copyright (c) 2009 Michael Niedermayer
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
#include "metadata.h"
#include "libavutil/dict.h"
#include "libavutil/avstring.h"

void ff_em_metadata_conv(AVEMDictionary **pm, const AVMetadataConv *d_conv,
                                       const AVMetadataConv *s_conv)
{
    /* TODO: use binary search to look up the two conversion tables
       if the tables are getting big enough that it would matter speed wise */
    const AVMetadataConv *sc, *dc;
    AVEMDictionaryEntry *mtag = NULL;
    AVEMDictionary *dst = NULL;
    const char *key;

    if (d_conv == s_conv || !pm)
        return;

    while ((mtag = av_em_dict_get(*pm, "", mtag, AV_DICT_IGNORE_SUFFIX))) {
        key = mtag->key;
        if (s_conv)
            for (sc=s_conv; sc->native; sc++)
                if (!av_em_strcasecmp(key, sc->native)) {
                    key = sc->generic;
                    break;
                }
        if (d_conv)
            for (dc=d_conv; dc->native; dc++)
                if (!av_em_strcasecmp(key, dc->generic)) {
                    key = dc->native;
                    break;
                }
        av_em_dict_set(&dst, key, mtag->value, 0);
    }
    av_em_dict_free(pm);
    *pm = dst;
}

void ff_metadata_conv_ctx(AVEMFormatContext *ctx, const AVMetadataConv *d_conv,
                                                const AVMetadataConv *s_conv)
{
    int i;
    ff_em_metadata_conv(&ctx->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_streams ; i++)
        ff_em_metadata_conv(&ctx->streams [i]->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_chapters; i++)
        ff_em_metadata_conv(&ctx->chapters[i]->metadata, d_conv, s_conv);
    for (i=0; i<ctx->nb_programs; i++)
        ff_em_metadata_conv(&ctx->programs[i]->metadata, d_conv, s_conv);
}
