/*
 * Concat URL protocol
 * Copyright (c) 2006 Steve Lhomme
 * Copyright (c) 2007 Wolfram Gloger
 * Copyright (c) 2010 Michele Orrù
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

#include "libavutil/avstring.h"
#include "libavutil/mem.h"

#include "avformat.h"
#include "url.h"

#define AV_CAT_SEPARATOR "|"

struct concat_nodes {
    EMURLContext *uc;                ///< node's EMURLContext
    int64_t     size;              ///< url filesize
};

struct concat_data {
    struct concat_nodes *nodes;    ///< list of nodes to concat
    size_t               length;   ///< number of cat'ed nodes
    size_t               current;  ///< index of currently read node
};

static av_cold int concat_close(EMURLContext *h)
{
    int err = 0;
    size_t i;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;

    for (i = 0; i != data->length; i++)
        err |= ffurl_em_close(nodes[i].uc);

    av_em_freep(&data->nodes);

    return err < 0 ? -1 : 0;
}

static av_cold int concat_open(EMURLContext *h, const char *uri, int flags)
{
    char *node_uri = NULL;
    int err = 0;
    int64_t size;
    size_t len, i;
    EMURLContext *uc;
    struct concat_data  *data = h->priv_data;
    struct concat_nodes *nodes;

    if (!av_em_strstart(uri, "concat:", &uri)) {
        av_em_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
        return AVERROR(EINVAL);
    }

    for (i = 0, len = 1; uri[i]; i++) {
        if (uri[i] == *AV_CAT_SEPARATOR) {
            /* integer overflow */
            if (++len == UINT_MAX / sizeof(*nodes)) {
                av_em_freep(&h->priv_data);
                return AVERROR(ENAMETOOLONG);
            }
        }
    }

    if (!(nodes = av_em_realloc(NULL, sizeof(*nodes) * len)))
        return AVERROR(ENOMEM);
    else
        data->nodes = nodes;

    /* handle input */
    if (!*uri)
        err = AVERROR(ENOENT);
    for (i = 0; *uri; i++) {
        /* parsing uri */
        len = strcspn(uri, AV_CAT_SEPARATOR);
        if ((err = av_em_reallocp(&node_uri, len + 1)) < 0)
            break;
        av_em_strlcpy(node_uri, uri, len + 1);
        uri += len + strspn(uri + len, AV_CAT_SEPARATOR);

        /* creating EMURLContext */
        err = ffurl_em_open_whitelist(&uc, node_uri, flags,
                                   &h->interrupt_callback, NULL, h->protocol_whitelist, h->protocol_blacklist, h);
        if (err < 0)
            break;

        /* creating size */
        if ((size = ffurl_em_size(uc)) < 0) {
            ffurl_em_close(uc);
            err = AVERROR(ENOSYS);
            break;
        }

        /* assembling */
        nodes[i].uc   = uc;
        nodes[i].size = size;
    }
    av_em_free(node_uri);
    data->length = i;

    if (err < 0)
        concat_close(h);
    else if (!(nodes = av_em_realloc(nodes, data->length * sizeof(*nodes)))) {
        concat_close(h);
        err = AVERROR(ENOMEM);
    } else
        data->nodes = nodes;
    return err;
}

static int concat_read(EMURLContext *h, unsigned char *buf, int size)
{
    int result, total = 0;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i                   = data->current;

    while (size > 0) {
        result = ffurl_em_read(nodes[i].uc, buf, size);
        if (result < 0)
            return total ? total : result;
        if (!result) {
            if (i + 1 == data->length ||
                ffurl_em_seek(nodes[++i].uc, 0, SEEK_SET) < 0)
                break;
        }
        total += result;
        buf   += result;
        size  -= result;
    }
    data->current = i;
    return total;
}

static int64_t concat_seek(EMURLContext *h, int64_t pos, int whence)
{
    int64_t result;
    struct concat_data  *data  = h->priv_data;
    struct concat_nodes *nodes = data->nodes;
    size_t i;

    switch (whence) {
    case SEEK_END:
        for (i = data->length - 1; i && pos < -nodes[i].size; i--)
            pos += nodes[i].size;
        break;
    case SEEK_CUR:
        /* get the absolute position */
        for (i = 0; i != data->current; i++)
            pos += nodes[i].size;
        pos += ffurl_em_seek(nodes[i].uc, 0, SEEK_CUR);
        whence = SEEK_SET;
        /* fall through with the absolute position */
    case SEEK_SET:
        for (i = 0; i != data->length - 1 && pos >= nodes[i].size; i++)
            pos -= nodes[i].size;
        break;
    default:
        return AVERROR(EINVAL);
    }

    result = ffurl_em_seek(nodes[i].uc, pos, whence);
    if (result >= 0) {
        data->current = i;
        while (i)
            result += nodes[--i].size;
    }
    return result;
}

const EMURLProtocol em_concat_protocol = {
    .name           = "concat",
    .url_open       = concat_open,
    .url_read       = concat_read,
    .url_seek       = concat_seek,
    .url_close      = concat_close,
    .priv_data_size = sizeof(struct concat_data),
    .default_whitelist = "concat,file,subfile",
};
