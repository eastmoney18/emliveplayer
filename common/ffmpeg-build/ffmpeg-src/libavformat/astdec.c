/*
 * AST demuxer
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "ast.h"

static int ast_probe(AVEMProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('S','T','R','M'))
        return 0;

    if (!AV_RB16(p->buf + 10) ||
        !AV_RB16(p->buf + 12) || AV_RB16(p->buf + 12) > 256 ||
        !AV_RB32(p->buf + 16) || AV_RB32(p->buf + 16) > 8*48000)
        return AVPROBE_SCORE_MAX / 8;

    return AVPROBE_SCORE_MAX / 3 * 2;
}

static int ast_read_header(AVEMFormatContext *s)
{
    int depth;
    AVEMStream *st;

    st = avformat_em_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_em_skip(s->pb, 8);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = em_codec_get_id(em_codec_ast_tags, avio_em_rb16(s->pb));

    depth = avio_em_rb16(s->pb);
    if (depth != 16) {
        avpriv_em_request_sample(s, "depth %d", depth);
        return AVERROR_INVALIDDATA;
    }

    st->codecpar->channels = avio_em_rb16(s->pb);
    if (!st->codecpar->channels)
        return AVERROR_INVALIDDATA;

    if (st->codecpar->channels == 2)
        st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
    else if (st->codecpar->channels == 4)
        st->codecpar->channel_layout = AV_CH_LAYOUT_4POINT0;

    avio_em_skip(s->pb, 2);
    st->codecpar->sample_rate = avio_em_rb32(s->pb);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    st->start_time         = 0;
    st->duration           = avio_em_rb32(s->pb);
    avio_em_skip(s->pb, 40);
    avpriv_em_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int ast_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    uint32_t type, size;
    int64_t pos;
    int ret;

    if (avio_em_feof(s->pb))
        return AVERROR_EOF;

    pos  = avio_em_tell(s->pb);
    type = avio_em_rl32(s->pb);
    size = avio_em_rb32(s->pb);
    if (size > INT_MAX / s->streams[0]->codecpar->channels)
        return AVERROR_INVALIDDATA;

    size *= s->streams[0]->codecpar->channels;
    if ((ret = avio_em_skip(s->pb, 24)) < 0) // padding
        return ret;

    if (type == MKTAG('B','L','C','K')) {
        ret = av_em_get_packet(s->pb, pkt, size);
        pkt->stream_index = 0;
        pkt->pos = pos;
    } else {
        av_em_log(s, AV_LOG_ERROR, "unknown chunk %x\n", type);
        avio_em_skip(s->pb, size);
        ret = AVERROR_INVALIDDATA;
    }

    return ret;
}

AVEMInputFormat ff_ast_demuxer = {
    .name           = "ast",
    .long_name      = NULL_IF_CONFIG_SMALL("AST (Audio Stream)"),
    .read_probe     = ast_probe,
    .read_header    = ast_read_header,
    .read_packet    = ast_read_packet,
    .extensions     = "ast",
    .flags          = AVFMT_GENERIC_INDEX,
    .codec_tag      = (const AVEMCodecTag* const []){em_codec_ast_tags, 0},
};
