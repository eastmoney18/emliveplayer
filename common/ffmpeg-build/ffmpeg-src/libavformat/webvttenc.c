/*
 * Copyright (c) 2013 Matthew Heaney
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
 * WebVTT subtitle muxer
 * @see http://dev.w3.org/html5/webvtt/
 */

#include "avformat.h"
#include "internal.h"

static void webvtt_write_time(AVEMIOContext *pb, int64_t millisec)
{
    int64_t sec, min, hour;
    sec = millisec / 1000;
    millisec -= 1000 * sec;
    min = sec / 60;
    sec -= 60 * min;
    hour = min / 60;
    min -= 60 * hour;

    if (hour > 0)
        avio_em_printf(pb, "%"PRId64":", hour);

    avio_em_printf(pb, "%02"PRId64":%02"PRId64".%03"PRId64"", min, sec, millisec);
}

static int webvtt_write_header(AVEMFormatContext *ctx)
{
    AVEMStream     *s = ctx->streams[0];
    AVEMCodecParameters *par = ctx->streams[0]->codecpar;
    AVEMIOContext *pb = ctx->pb;

    if (ctx->nb_streams != 1 || par->codec_id != AV_CODEC_ID_WEBVTT) {
        av_em_log(ctx, AV_LOG_ERROR, "Exactly one WebVTT stream is needed.\n");
        return AVERROR(EINVAL);
    }

    avpriv_em_set_pts_info(s, 64, 1, 1000);

    avio_em_printf(pb, "WEBVTT\n");
    avio_em_flush(pb);

    return 0;
}

static int webvtt_write_packet(AVEMFormatContext *ctx, AVEMPacket *pkt)
{
    AVEMIOContext  *pb = ctx->pb;
    int id_size, settings_size;
    uint8_t *id, *settings;

    avio_em_printf(pb, "\n");

    id = av_em_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_IDENTIFIER,
                                 &id_size);

    if (id && id_size > 0)
        avio_em_printf(pb, "%.*s\n", id_size, id);

    webvtt_write_time(pb, pkt->pts);
    avio_em_printf(pb, " --> ");
    webvtt_write_time(pb, pkt->pts + pkt->duration);

    settings = av_em_packet_get_side_data(pkt, AV_PKT_DATA_WEBVTT_SETTINGS,
                                       &settings_size);

    if (settings && settings_size > 0)
        avio_em_printf(pb, " %.*s", settings_size, settings);

    avio_em_printf(pb, "\n");

    avio_em_write(pb, pkt->data, pkt->size);
    avio_em_printf(pb, "\n");

    return 0;
}

AVEMOutputFormat ff_webvtt_muxer = {
    .name              = "webvtt",
    .long_name         = NULL_IF_CONFIG_SMALL("WebVTT subtitle"),
    .extensions        = "vtt",
    .mime_type         = "text/vtt",
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
    .subtitle_codec    = AV_CODEC_ID_WEBVTT,
    .write_header      = webvtt_write_header,
    .write_packet      = webvtt_write_packet,
};
