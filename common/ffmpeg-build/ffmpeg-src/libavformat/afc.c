/*
 * AFC demuxer
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
#include "avformat.h"
#include "internal.h"

typedef struct AFCDemuxContext {
    int64_t    data_end;
} AFCDemuxContext;

static int afc_read_header(AVEMFormatContext *s)
{
    AFCDemuxContext *c = s->priv_data;
    AVEMStream *st;

    st = avformat_em_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_AFC;
    st->codecpar->channels   = 2;
    st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;

    if (em_alloc_extradata(st->codecpar, 1))
        return AVERROR(ENOMEM);
    st->codecpar->extradata[0] = 8 * st->codecpar->channels;

    c->data_end = avio_em_rb32(s->pb) + 32LL;
    st->duration = avio_em_rb32(s->pb);
    st->codecpar->sample_rate = avio_em_rb16(s->pb);
    avio_em_skip(s->pb, 22);
    avpriv_em_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int afc_read_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    AFCDemuxContext *c = s->priv_data;
    int64_t size;
    int ret;

    size = FFMIN(c->data_end - avio_em_tell(s->pb), 18 * 128);
    if (size <= 0)
        return AVERROR_EOF;

    ret = av_em_get_packet(s->pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

AVEMInputFormat ff_afc_demuxer = {
    .name           = "afc",
    .long_name      = NULL_IF_CONFIG_SMALL("AFC"),
    .priv_data_size = sizeof(AFCDemuxContext),
    .read_header    = afc_read_header,
    .read_packet    = afc_read_packet,
    .extensions     = "afc",
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK,
};
