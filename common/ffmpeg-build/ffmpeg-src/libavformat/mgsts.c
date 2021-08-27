/*
 * Metar Gear Solid: The Twin Snakes demuxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "avformat.h"
#include "riff.h"

static int read_probe(AVEMProbeData *p)
{
    if (AV_RB32(p->buf     ) != 0x000E ||
        AV_RB32(p->buf +  4) != 0x0050 ||
        AV_RB32(p->buf + 12) != 0x0034)
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int read_header(AVEMFormatContext *s)
{
    AVEMIOContext *pb = s->pb;
    AVEMStream    *st;
    AVEMRational  fps;
    uint32_t chunk_size;

    avio_em_skip(pb, 4);
    chunk_size = avio_em_rb32(pb);
    if (chunk_size != 80)
        return AVERROR(EIO);
    avio_em_skip(pb, 20);

    st = avformat_em_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->need_parsing = AVSTREAM_PARSE_HEADERS;
    st->start_time = 0;
    st->nb_frames  =
    st->duration   = avio_em_rb32(pb);
    fps = av_em_d2q(av_int2float(avio_em_rb32(pb)), INT_MAX);
    st->codecpar->width  = avio_em_rb32(pb);
    st->codecpar->height = avio_em_rb32(pb);
    avio_em_skip(pb, 12);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_tag  = avio_em_rb32(pb);
    st->codecpar->codec_id   = em_codec_get_id(em_codec_bmp_tags,
                                               st->codecpar->codec_tag);
    avpriv_em_set_pts_info(st, 64, fps.den, fps.num);
    avio_em_skip(pb, 20);

    return 0;
}

static int read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    AVEMIOContext *pb = s->pb;
    uint32_t chunk_size, payload_size;
    int ret;

    if (avio_em_feof(pb))
        return AVERROR_EOF;

    avio_em_skip(pb, 4);
    chunk_size = avio_em_rb32(pb);
    avio_em_skip(pb, 4);
    payload_size = avio_em_rb32(pb);

    if (chunk_size < payload_size + 16)
        return AVERROR(EIO);

    ret = av_em_get_packet(pb, pkt, payload_size);
    if (ret < 0)
        return ret;

    pkt->pos -= 16;
    pkt->duration = 1;
    avio_em_skip(pb, chunk_size - (ret + 16));

    return ret;
}

AVEMInputFormat ff_mgsts_demuxer = {
    .name        = "mgsts",
    .long_name   = NULL_IF_CONFIG_SMALL("Metal Gear Solid: The Twin Snakes"),
    .read_probe  = read_probe,
    .read_header = read_header,
    .read_packet = read_packet,
    .flags       = AVFMT_GENERIC_INDEX,
};
