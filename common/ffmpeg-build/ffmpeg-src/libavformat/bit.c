/*
 * G.729 bit format muxer and demuxer
 * Copyright (c) 2007-2008 Vladimir Voroshilov
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
#include "internal.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"

#define MAX_FRAME_SIZE 10

#define SYNC_WORD  0x6b21
#define BIT_0      0x7f
#define BIT_1      0x81

static int probe(AVEMProbeData *p)
{
    int i, j;

    if(p->buf_size < 0x40)
        return 0;

    for(i=0; i+3<p->buf_size && i< 10*0x50; ){
        if(AV_RL16(&p->buf[0]) != SYNC_WORD)
            return 0;
        j=AV_RL16(&p->buf[2]);
        if(j!=0x40 && j!=0x50)
            return 0;
        i+=j;
    }
    return AVPROBE_SCORE_EXTENSION;
}

static int read_header(AVEMFormatContext *s)
{
    AVEMStream* st;

    st=avformat_em_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id=AV_CODEC_ID_G729;
    st->codecpar->sample_rate=8000;
    st->codecpar->block_align = 16;
    st->codecpar->channels=1;

    avpriv_em_set_pts_info(st, 64, 1, 100);
    return 0;
}

static int read_packet(AVEMFormatContext *s,
                          AVEMPacket *pkt)
{
    AVEMIOContext *pb = s->pb;
    PutBitContext pbo;
    uint16_t buf[8 * MAX_FRAME_SIZE + 2];
    int packet_size;
    uint16_t* src=buf;
    int i, j, ret;
    int64_t pos= avio_em_tell(pb);

    if(avio_em_feof(pb))
        return AVERROR_EOF;

    avio_em_rl16(pb); // sync word
    packet_size = avio_em_rl16(pb) / 8;
    if(packet_size > MAX_FRAME_SIZE)
        return AVERROR_INVALIDDATA;

    ret = avio_em_read(pb, (uint8_t*)buf, (8 * packet_size) * sizeof(uint16_t));
    if(ret<0)
        return ret;
    if(ret != 8 * packet_size * sizeof(uint16_t))
        return AVERROR(EIO);

    if (av_em_new_packet(pkt, packet_size) < 0)
        return AVERROR(ENOMEM);

    init_put_bits(&pbo, pkt->data, packet_size);
    for(j=0; j < packet_size; j++)
        for(i=0; i<8;i++)
            put_bits(&pbo,1, AV_RL16(src++) == BIT_1 ? 1 : 0);

    flush_put_bits(&pbo);

    pkt->duration=1;
    pkt->pos = pos;
    return 0;
}

AVEMInputFormat ff_bit_demuxer = {
    .name        = "bit",
    .long_name   = NULL_IF_CONFIG_SMALL("G.729 BIT file format"),
    .read_probe  = probe,
    .read_header = read_header,
    .read_packet = read_packet,
    .extensions  = "bit",
};

#if CONFIG_MUXERS
static int write_header(AVEMFormatContext *s)
{
    AVEMCodecParameters *par = s->streams[0]->codecpar;

    if ((par->codec_id != AV_CODEC_ID_G729) || par->channels != 1) {
        av_em_log(s, AV_LOG_ERROR,
               "only codec g729 with 1 channel is supported by this format\n");
        return AVERROR(EINVAL);
    }

    par->bits_per_coded_sample = 16;
    par->block_align = (par->bits_per_coded_sample * par->channels) >> 3;

    return 0;
}

static int write_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    AVEMIOContext *pb = s->pb;
    GetBitContext gb;
    int i;

    if (pkt->size != 10)
        return AVERROR(EINVAL);

    avio_em_wl16(pb, SYNC_WORD);
    avio_em_wl16(pb, 8 * 10);

    init_get_bits(&gb, pkt->data, 8*10);
    for(i=0; i< 8 * 10; i++)
        avio_em_wl16(pb, get_bits1(&gb) ? BIT_1 : BIT_0);

    return 0;
}

AVEMOutputFormat ff_bit_muxer = {
    .name         = "bit",
    .long_name    = NULL_IF_CONFIG_SMALL("G.729 BIT file format"),
    .mime_type    = "audio/bit",
    .extensions   = "bit",
    .audio_codec  = AV_CODEC_ID_G729,
    .video_codec  = AV_CODEC_ID_NONE,
    .write_header = write_header,
    .write_packet = write_packet,
};
#endif