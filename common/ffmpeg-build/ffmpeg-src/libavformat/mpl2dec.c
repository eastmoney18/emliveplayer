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
 * MPL2 subtitles format demuxer
 */

#include "avformat.h"
#include "internal.h"
#include "subtitles.h"

typedef struct {
    FFDemuxSubtitlesQueue q;
} MPL2Context;

static int mpl2_probe(AVEMProbeData *p)
{
    int i;
    char c;
    int64_t start, end;
    const unsigned char *ptr = p->buf;
    const unsigned char *ptr_end = ptr + p->buf_size;

    for (i = 0; i < 2; i++) {
        if (sscanf(ptr, "[%"SCNd64"][%"SCNd64"]%c", &start, &end, &c) != 3 &&
            sscanf(ptr, "[%"SCNd64"][]%c",          &start,       &c) != 2)
            return 0;
        ptr += ff_subtitles_next_line(ptr);
        if (ptr >= ptr_end)
            return 0;
    }
    return AVPROBE_SCORE_MAX;
}

static int read_ts(char **line, int64_t *pts_start, int *duration)
{
    char c;
    int len;
    int64_t end;

    if (sscanf(*line, "[%"SCNd64"][]%c%n",
               pts_start, &c, &len) >= 2) {
        *duration = -1;
        *line += len - 1;
        return 0;
    }
    if (sscanf(*line, "[%"SCNd64"][%"SCNd64"]%c%n",
               pts_start, &end, &c, &len) >= 3) {
        *duration = end - *pts_start;
        *line += len - 1;
        return 0;
    }
    return -1;
}

static int mpl2_read_header(AVEMFormatContext *s)
{
    MPL2Context *mpl2 = s->priv_data;
    AVEMStream *st = avformat_em_new_stream(s, NULL);
    int res = 0;

    if (!st)
        return AVERROR(ENOMEM);
    avpriv_em_set_pts_info(st, 64, 1, 10);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_MPL2;

    while (!avio_em_feof(s->pb)) {
        char line[4096];
        char *p = line;
        const int64_t pos = avio_em_tell(s->pb);
        int len = em_get_line(s->pb, line, sizeof(line));
        int64_t pts_start;
        int duration;

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (!read_ts(&p, &pts_start, &duration)) {
            AVEMPacket *sub;

            sub = ff_subtitles_queue_insert(&mpl2->q, p, strlen(p), 0);
            if (!sub)
                return AVERROR(ENOMEM);
            sub->pos = pos;
            sub->pts = pts_start;
            sub->duration = duration;
        }
    }

    ff_subtitles_queue_finalize(s, &mpl2->q);
    return res;
}

static int mpl2_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    MPL2Context *mpl2 = s->priv_data;
    return ff_subtitles_queue_read_packet(&mpl2->q, pkt);
}

static int mpl2_read_seek(AVEMFormatContext *s, int stream_index,
                          int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    MPL2Context *mpl2 = s->priv_data;
    return ff_subtitles_queue_seek(&mpl2->q, s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int mpl2_read_close(AVEMFormatContext *s)
{
    MPL2Context *mpl2 = s->priv_data;
    ff_subtitles_queue_clean(&mpl2->q);
    return 0;
}

AVEMInputFormat ff_mpl2_demuxer = {
    .name           = "mpl2",
    .long_name      = NULL_IF_CONFIG_SMALL("MPL2 subtitles"),
    .priv_data_size = sizeof(MPL2Context),
    .read_probe     = mpl2_probe,
    .read_header    = mpl2_read_header,
    .read_packet    = mpl2_read_packet,
    .read_seek2     = mpl2_read_seek,
    .read_close     = mpl2_read_close,
    .extensions     = "txt,mpl2",
};
