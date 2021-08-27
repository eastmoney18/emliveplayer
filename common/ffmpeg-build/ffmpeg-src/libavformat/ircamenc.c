/*
 * IRCAM muxer
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
#include "avio_internal.h"
#include "internal.h"
#include "rawenc.h"
#include "ircam.h"

static int ircam_write_header(AVEMFormatContext *s)
{
    AVEMCodecParameters *par = s->streams[0]->codecpar;
    uint32_t tag;

    if (s->nb_streams != 1) {
        av_em_log(s, AV_LOG_ERROR, "only one stream is supported\n");
        return AVERROR(EINVAL);
    }

    tag = em_codec_get_tag(em_codec_ircam_le_tags, par->codec_id);
    if (!tag) {
        av_em_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(EINVAL);
    }

    avio_em_wl32(s->pb, 0x0001A364);
    avio_em_wl32(s->pb, av_em_q2intfloat((AVEMRational){par->sample_rate, 1}));
    avio_em_wl32(s->pb, par->channels);
    avio_em_wl32(s->pb, tag);
    emio_fill(s->pb, 0, 1008);
    return 0;
}

AVEMOutputFormat ff_ircam_muxer = {
    .name           = "ircam",
    .extensions     = "sf,ircam",
    .long_name      = NULL_IF_CONFIG_SMALL("Berkeley/IRCAM/CARL Sound Format"),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = ircam_write_header,
    .write_packet   = ff_raw_write_packet,
    .codec_tag      = (const AVEMCodecTag *const []){ em_codec_ircam_le_tags, 0 },
};
