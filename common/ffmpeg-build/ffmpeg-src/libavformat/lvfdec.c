/*
 * LVF demuxer
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
#include "avformat.h"
#include "riff.h"

static int lvf_probe(AVEMProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('L', 'V', 'F', 'F'))
        return 0;

    if (!AV_RL32(p->buf + 16) || AV_RL32(p->buf + 16) > 256)
        return AVPROBE_SCORE_MAX / 8;

    return AVPROBE_SCORE_EXTENSION;
}

static int lvf_read_header(AVEMFormatContext *s)
{
    AVEMStream *st;
    int64_t next_offset;
    unsigned size, nb_streams, id;

    avio_em_skip(s->pb, 16);
    nb_streams = avio_em_rl32(s->pb);
    if (!nb_streams)
        return AVERROR_INVALIDDATA;
    if (nb_streams > 2) {
        avpriv_em_request_sample(s, "%d streams", nb_streams);
        return AVERROR_PATCHWELCOME;
    }

    avio_em_skip(s->pb, 1012);

    while (!avio_em_feof(s->pb)) {
        id          = avio_em_rl32(s->pb);
        size        = avio_em_rl32(s->pb);
        next_offset = avio_em_tell(s->pb) + size;

        switch (id) {
        case MKTAG('0', '0', 'f', 'm'):
            st = avformat_em_new_stream(s, 0);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            avio_em_skip(s->pb, 4);
            st->codecpar->width      = avio_em_rl32(s->pb);
            st->codecpar->height     = avio_em_rl32(s->pb);
            avio_em_skip(s->pb, 4);
            st->codecpar->codec_tag  = avio_em_rl32(s->pb);
            st->codecpar->codec_id   = em_codec_get_id(em_codec_bmp_tags,
                                                       st->codecpar->codec_tag);
            avpriv_em_set_pts_info(st, 32, 1, 1000);
            break;
        case MKTAG('0', '1', 'f', 'm'):
            st = avformat_em_new_stream(s, 0);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
            st->codecpar->codec_tag   = avio_em_rl16(s->pb);
            st->codecpar->channels    = avio_em_rl16(s->pb);
            st->codecpar->sample_rate = avio_em_rl16(s->pb);
            avio_em_skip(s->pb, 8);
            st->codecpar->bits_per_coded_sample = avio_em_r8(s->pb);
            st->codecpar->codec_id    = em_codec_get_id(em_codec_wav_tags,
                                                        st->codecpar->codec_tag);
            avpriv_em_set_pts_info(st, 32, 1, 1000);
            break;
        case 0:
            avio_em_seek(s->pb, 2048 + 8, SEEK_SET);
            return 0;
        default:
            avpriv_em_request_sample(s, "id %d", id);
            return AVERROR_PATCHWELCOME;
        }

        avio_em_seek(s->pb, next_offset, SEEK_SET);
    }

    return AVERROR_EOF;
}

static int lvf_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    unsigned size, flags, timestamp, id;
    int64_t pos;
    int ret, is_video = 0;

    pos = avio_em_tell(s->pb);
    while (!avio_em_feof(s->pb)) {
        id    = avio_em_rl32(s->pb);
        size  = avio_em_rl32(s->pb);

        if (size == 0xFFFFFFFFu)
            return AVERROR_EOF;

        switch (id) {
        case MKTAG('0', '0', 'd', 'c'):
            is_video = 1;
        case MKTAG('0', '1', 'w', 'b'):
            if (size < 8)
                return AVERROR_INVALIDDATA;
            timestamp = avio_em_rl32(s->pb);
            flags = avio_em_rl32(s->pb);
            ret = av_em_get_packet(s->pb, pkt, size - 8);
            if (flags & (1 << 12))
                pkt->flags |= AV_PKT_FLAG_KEY;
            pkt->stream_index = is_video ? 0 : 1;
            pkt->pts          = timestamp;
            pkt->pos          = pos;
            return ret;
        default:
            ret = avio_em_skip(s->pb, size);
        }

        if (ret < 0)
            return ret;
    }

    return AVERROR_EOF;
}

AVEMInputFormat ff_lvf_demuxer = {
    .name        = "lvf",
    .long_name   = NULL_IF_CONFIG_SMALL("LVF"),
    .read_probe  = lvf_probe,
    .read_header = lvf_read_header,
    .read_packet = lvf_read_packet,
    .extensions  = "lvf",
    .flags       = AVFMT_GENERIC_INDEX,
};
