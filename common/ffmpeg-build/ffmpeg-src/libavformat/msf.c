/*
 * MSF demuxer
 * Copyright (c) 2015 Paul B Mahol
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
#include "internal.h"

static int msf_probe(AVEMProbeData *p)
{
    if (memcmp(p->buf, "MSF", 3))
        return 0;

    if (AV_RB32(p->buf+8) <= 0)
        return 0;

    if (AV_RB32(p->buf+16) <= 0)
        return 0;

    if (AV_RB32(p->buf+4) > 16)
        return AVPROBE_SCORE_MAX / 5; //unsupported / unknown codec

    return AVPROBE_SCORE_MAX / 3 * 2;
}

static int msf_read_header(AVEMFormatContext *s)
{
    unsigned codec, align, size;
    AVEMStream *st;

    avio_em_skip(s->pb, 4);

    st = avformat_em_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    codec                  = avio_em_rb32(s->pb);
    st->codecpar->channels    = avio_em_rb32(s->pb);
    if (st->codecpar->channels <= 0 || st->codecpar->channels >= INT_MAX / 1024)
        return AVERROR_INVALIDDATA;
    size = avio_em_rb32(s->pb);
    st->codecpar->sample_rate = avio_em_rb32(s->pb);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    align = avio_em_rb32(s->pb) ;
    if (align > INT_MAX / st->codecpar->channels)
        return AVERROR_INVALIDDATA;
    st->codecpar->block_align = align;
    switch (codec) {
    case 0: st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE; break;
    case 3: st->codecpar->block_align = 16 * st->codecpar->channels;
            st->codecpar->codec_id = AV_CODEC_ID_ADPCM_PSX; break;
    case 7: st->need_parsing = AVSTREAM_PARSE_FULL_RAW;
            st->codecpar->codec_id = AV_CODEC_ID_MP3;       break;
    default:
            avpriv_em_request_sample(s, "Codec %d", codec);
            return AVERROR_PATCHWELCOME;
    }
    st->duration = av_em_get_audio_frame_duration2(st->codecpar, size);
    avio_em_skip(s->pb, 0x40 - avio_em_tell(s->pb));
    avpriv_em_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int msf_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    AVEMCodecParameters *par = s->streams[0]->codecpar;

    return av_em_get_packet(s->pb, pkt, par->block_align ? par->block_align : 1024 * par->channels);
}

AVEMInputFormat ff_msf_demuxer = {
    .name           = "msf",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony PS3 MSF"),
    .read_probe     = msf_probe,
    .read_header    = msf_read_header,
    .read_packet    = msf_read_packet,
    .extensions     = "msf",
};
