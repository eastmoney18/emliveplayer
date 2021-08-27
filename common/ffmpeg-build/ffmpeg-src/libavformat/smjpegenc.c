/*
 * SMJPEG muxer
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

/**
 * @file
 * This is a muxer for Loki SDL Motion JPEG files
 */

#include "avformat.h"
#include "internal.h"
#include "smjpeg.h"

typedef struct SMJPEGMuxContext {
    uint32_t duration;
} SMJPEGMuxContext;

static int smjpeg_write_header(AVEMFormatContext *s)
{
    AVEMDictionaryEntry *t = NULL;
    AVEMIOContext *pb = s->pb;
    int n, tag;

    if (s->nb_streams > 2) {
        av_em_log(s, AV_LOG_ERROR, "more than >2 streams are not supported\n");
        return AVERROR(EINVAL);
    }
    avio_em_write(pb, SMJPEG_MAGIC, 8);
    avio_em_wb32(pb, 0);
    avio_em_wb32(pb, 0);

    em_standardize_creation_time(s);
    while ((t = av_em_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        avio_em_wl32(pb, SMJPEG_TXT);
        avio_em_wb32(pb, strlen(t->key) + strlen(t->value) + 3);
        avio_em_write(pb, t->key, strlen(t->key));
        avio_em_write(pb, " = ", 3);
        avio_em_write(pb, t->value, strlen(t->value));
    }

    for (n = 0; n < s->nb_streams; n++) {
        AVEMStream *st = s->streams[n];
        AVEMCodecParameters *par = st->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            tag = em_codec_get_tag(em_codec_smjpeg_audio_tags, par->codec_id);
            if (!tag) {
                av_em_log(s, AV_LOG_ERROR, "unsupported audio codec\n");
                return AVERROR(EINVAL);
            }
            avio_em_wl32(pb, SMJPEG_SND);
            avio_em_wb32(pb, 8);
            avio_em_wb16(pb, par->sample_rate);
            avio_em_w8(pb, par->bits_per_coded_sample);
            avio_em_w8(pb, par->channels);
            avio_em_wl32(pb, tag);
            avpriv_em_set_pts_info(st, 32, 1, 1000);
        } else if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            tag = em_codec_get_tag(em_codec_smjpeg_video_tags, par->codec_id);
            if (!tag) {
                av_em_log(s, AV_LOG_ERROR, "unsupported video codec\n");
                return AVERROR(EINVAL);
            }
            avio_em_wl32(pb, SMJPEG_VID);
            avio_em_wb32(pb, 12);
            avio_em_wb32(pb, 0);
            avio_em_wb16(pb, par->width);
            avio_em_wb16(pb, par->height);
            avio_em_wl32(pb, tag);
            avpriv_em_set_pts_info(st, 32, 1, 1000);
        }
    }

    avio_em_wl32(pb, SMJPEG_HEND);
    avio_em_flush(pb);

    return 0;
}

static int smjpeg_write_packet(AVEMFormatContext *s, AVEMPacket *pkt)
{
    SMJPEGMuxContext *smc = s->priv_data;
    AVEMIOContext *pb = s->pb;
    AVEMStream *st = s->streams[pkt->stream_index];
    AVEMCodecParameters *par = st->codecpar;

    if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        avio_em_wl32(pb, SMJPEG_SNDD);
    else if (par->codec_type == AVMEDIA_TYPE_VIDEO)
        avio_em_wl32(pb, SMJPEG_VIDD);
    else
        return 0;

    avio_em_wb32(pb, pkt->pts);
    avio_em_wb32(pb, pkt->size);
    avio_em_write(pb, pkt->data, pkt->size);

    smc->duration = FFMAX(smc->duration, pkt->pts + pkt->duration);
    return 0;
}

static int smjpeg_write_trailer(AVEMFormatContext *s)
{
    SMJPEGMuxContext *smc = s->priv_data;
    AVEMIOContext *pb = s->pb;
    int64_t currentpos;

    if (pb->seekable) {
        currentpos = avio_em_tell(pb);
        avio_em_seek(pb, 12, SEEK_SET);
        avio_em_wb32(pb, smc->duration);
        avio_em_seek(pb, currentpos, SEEK_SET);
    }

    avio_em_wl32(pb, SMJPEG_DONE);

    return 0;
}

AVEMOutputFormat ff_smjpeg_muxer = {
    .name           = "smjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("Loki SDL MJPEG"),
    .priv_data_size = sizeof(SMJPEGMuxContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_MJPEG,
    .write_header   = smjpeg_write_header,
    .write_packet   = smjpeg_write_packet,
    .write_trailer  = smjpeg_write_trailer,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_TS_NONSTRICT,
    .codec_tag      = (const AVEMCodecTag *const []){ em_codec_smjpeg_video_tags, em_codec_smjpeg_audio_tags, 0 },
};
