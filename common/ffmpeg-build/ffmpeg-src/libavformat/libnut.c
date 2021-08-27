/*
 * NUT (de)muxing via libnut
 * copyright (c) 2006 Oded Shimon <ods15@ods15.dyndns.org>
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
 * NUT demuxing and muxing via libnut.
 * @author Oded Shimon <ods15@ods15.dyndns.org>
 */

#include "avformat.h"
#include "internal.h"
#include "riff.h"
#include <libnut.h>

#define ID_STRING "nut/multimedia container"
#define ID_LENGTH (strlen(ID_STRING) + 1)

typedef struct {
    nut_context_tt * nut;
    nut_stream_header_tt * s;
} NUTContext;

static const AVEMCodecTag nut_tags[] = {
    { AV_CODEC_ID_MPEG4,  MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_MP3,    MKTAG('m', 'p', '3', ' ') },
    { AV_CODEC_ID_VORBIS, MKTAG('v', 'r', 'b', 's') },
    { 0, 0 },
};

#if CONFIG_LIBNUT_MUXER
static int av_write(void * h, size_t len, const uint8_t * buf) {
    AVEMIOContext * bc = h;
    avio_em_write(bc, buf, len);
    //avio_em_flush(bc);
    return len;
}

static int nut_write_header(AVEMFormatContext * avf) {
    NUTContext * priv = avf->priv_data;
    AVEMIOContext * bc = avf->pb;
    nut_muxer_opts_tt mopts = {
        .output = {
            .priv = bc,
            .write = av_write,
        },
        .alloc = { av_malloc, av_em_realloc, av_em_free },
        .write_index = 1,
        .realtime_stream = 0,
        .max_distance = 32768,
        .fti = NULL,
    };
    nut_stream_header_tt * s;
    int i;

    priv->s = s = av_em_mallocz_array(avf->nb_streams + 1, sizeof*s);
    if(!s)
        return AVERROR(ENOMEM);

    for (i = 0; i < avf->nb_streams; i++) {
        AVEMCodecParameters *par = avf->streams[i]->codecpar;
        int j;
        int fourcc = 0;
        int num, denom, ssize;

        s[i].type = par->codec_type == AVMEDIA_TYPE_VIDEO ? NUT_VIDEO_CLASS : NUT_AUDIO_CLASS;

        if (par->codec_tag) fourcc = par->codec_tag;
        else fourcc = em_codec_get_tag(nut_tags, par->codec_id);

        if (!fourcc) {
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) fourcc = em_codec_get_tag(em_codec_bmp_tags, par->codec_id);
            if (par->codec_type == AVMEDIA_TYPE_AUDIO) fourcc = em_codec_get_tag(em_codec_wav_tags, par->codec_id);
        }

        s[i].fourcc_len = 4;
        s[i].fourcc = av_em_alloc(s[i].fourcc_len);
        for (j = 0; j < s[i].fourcc_len; j++) s[i].fourcc[j] = (fourcc >> (j*8)) & 0xFF;

        ff_parse_specific_params(avf->streams[i], &num, &ssize, &denom);
        avpriv_em_set_pts_info(avf->streams[i], 60, denom, num);

        s[i].time_base.num = denom;
        s[i].time_base.den = num;

        s[i].fixed_fps = 0;
        s[i].decode_delay = par->video_delay;
        s[i].codec_specific_len = par->extradata_size;
        s[i].codec_specific = par->extradata;

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            s[i].width = par->width;
            s[i].height = par->height;
            s[i].sample_width = 0;
            s[i].sample_height = 0;
            s[i].colorspace_type = 0;
        } else {
            s[i].samplerate_num = par->sample_rate;
            s[i].samplerate_denom = 1;
            s[i].channel_count = par->channels;
        }
    }

    s[avf->nb_streams].type = -1;
    priv->nut = nut_muxer_init(&mopts, s, NULL);

    return 0;
}

static int nut_write_packet(AVEMFormatContext * avf, AVEMPacket * pkt) {
    NUTContext * priv = avf->priv_data;
    nut_packet_tt p;

    p.len = pkt->size;
    p.stream = pkt->stream_index;
    p.pts = pkt->pts;
    p.flags = pkt->flags & AV_PKT_FLAG_KEY ? NUT_FLAG_KEY : 0;
    p.next_pts = 0;

    nut_write_frame_reorder(priv->nut, &p, pkt->data);

    return 0;
}

static int nut_write_trailer(AVEMFormatContext * avf) {
    AVEMIOContext * bc = avf->pb;
    NUTContext * priv = avf->priv_data;
    int i;

    nut_muxer_uninit_reorder(priv->nut);
    avio_em_flush(bc);

    for(i = 0; priv->s[i].type != -1; i++ ) av_em_freep(&priv->s[i].fourcc);
    av_em_freep(&priv->s);

    return 0;
}

AVEMOutputFormat ff_libnut_muxer = {
    .name              = "libnut",
    .long_name         = "nut format",
    .mime_type         = "video/x-nut",
    .extensions        = "nut",
    .priv_data_size    = sizeof(NUTContext),
    .audio_codec       = AV_CODEC_ID_VORBIS,
    .video_codec       = AV_CODEC_ID_MPEG4,
    .write_header      = nut_write_header,
    .write_packet      = nut_write_packet,
    .write_trailer     = nut_write_trailer,
    .flags             = AVFMT_GLOBALHEADER,
};
#endif /* CONFIG_LIBNUT_MUXER */

static int nut_probe(AVEMProbeData *p) {
    if (!memcmp(p->buf, ID_STRING, ID_LENGTH)) return AVPROBE_SCORE_MAX;

    return 0;
}

static size_t av_read(void * h, size_t len, uint8_t * buf) {
    AVEMIOContext * bc = h;
    return avio_em_read(bc, buf, len);
}

static off_t av_seek(void * h, int64_t pos, int whence) {
    AVEMIOContext * bc = h;
    if (whence == SEEK_END) {
        pos = avio_em_size(bc) + pos;
        whence = SEEK_SET;
    }
    return avio_em_seek(bc, pos, whence);
}

static int nut_read_header(AVEMFormatContext * avf) {
    NUTContext * priv = avf->priv_data;
    AVEMIOContext * bc = avf->pb;
    nut_demuxer_opts_tt dopts = {
        .input = {
            .priv = bc,
            .seek = av_seek,
            .read = av_read,
            .eof = NULL,
            .file_pos = 0,
        },
        .alloc = { av_malloc, av_em_realloc, av_em_free },
        .read_index = 1,
        .cache_syncpoints = 1,
    };
    nut_context_tt * nut = priv->nut = nut_demuxer_init(&dopts);
    nut_stream_header_tt * s;
    int ret, i;

    if(!nut)
        return -1;

    if ((ret = nut_read_headers(nut, &s, NULL))) {
        av_em_log(avf, AV_LOG_ERROR, " NUT error: %s\n", nut_error(ret));
        nut_demuxer_uninit(nut);
        priv->nut = NULL;
        return -1;
    }

    priv->s = s;

    for (i = 0; s[i].type != -1 && i < 2; i++) {
        AVEMStream * st = avformat_em_new_stream(avf, NULL);
        int j;

        if (!st)
            return AVERROR(ENOMEM);

        for (j = 0; j < s[i].fourcc_len && j < 8; j++) st->codecpar->codec_tag |= s[i].fourcc[j]<<(j*8);

        st->codecpar->video_delay = s[i].decode_delay;

        st->codecpar->extradata_size = s[i].codec_specific_len;
        if (st->codecpar->extradata_size) {
            if(em_alloc_extradata(st->codecpar, st->codecpar->extradata_size)){
                nut_demuxer_uninit(nut);
                priv->nut = NULL;
                return AVERROR(ENOMEM);
            }
            memcpy(st->codecpar->extradata, s[i].codec_specific, st->codecpar->extradata_size);
        }

        avpriv_em_set_pts_info(avf->streams[i], 60, s[i].time_base.num, s[i].time_base.den);
        st->start_time = 0;
        st->duration = s[i].max_pts;

        st->codecpar->codec_id = em_codec_get_id(nut_tags, st->codecpar->codec_tag);

        switch(s[i].type) {
        case NUT_AUDIO_CLASS:
            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            if (st->codecpar->codec_id == AV_CODEC_ID_NONE) st->codecpar->codec_id = em_codec_get_id(em_codec_wav_tags, st->codecpar->codec_tag);

            st->codecpar->channels = s[i].channel_count;
            st->codecpar->sample_rate = s[i].samplerate_num / s[i].samplerate_denom;
            break;
        case NUT_VIDEO_CLASS:
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            if (st->codecpar->codec_id == AV_CODEC_ID_NONE) st->codecpar->codec_id = em_codec_get_id(em_codec_bmp_tags, st->codecpar->codec_tag);

            st->codecpar->width = s[i].width;
            st->codecpar->height = s[i].height;
            st->sample_aspect_ratio.num = s[i].sample_width;
            st->sample_aspect_ratio.den = s[i].sample_height;
            break;
        }
        if (st->codecpar->codec_id == AV_CODEC_ID_NONE) av_em_log(avf, AV_LOG_ERROR, "Unknown codec?!\n");
    }

    return 0;
}

static int nut_read_packet(AVEMFormatContext * avf, AVEMPacket * pkt) {
    NUTContext * priv = avf->priv_data;
    nut_packet_tt pd;
    int ret;

    ret = nut_read_next_packet(priv->nut, &pd);

    if (ret || av_em_new_packet(pkt, pd.len) < 0) {
        if (ret != NUT_ERR_EOF)
            av_em_log(avf, AV_LOG_ERROR, " NUT error: %s\n", nut_error(ret));
        return -1;
    }

    if (pd.flags & NUT_FLAG_KEY) pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->pts = pd.pts;
    pkt->stream_index = pd.stream;
    pkt->pos = avio_em_tell(avf->pb);

    ret = nut_read_frame(priv->nut, &pd.len, pkt->data);

    return ret;
}

static int nut_read_seek(AVEMFormatContext * avf, int stream_index, int64_t target_ts, int flags) {
    NUTContext * priv = avf->priv_data;
    int active_streams[] = { stream_index, -1 };
    double time_pos = target_ts * priv->s[stream_index].time_base.num / (double)priv->s[stream_index].time_base.den;

    if (nut_seek(priv->nut, time_pos, 2*!(flags & AVSEEK_FLAG_BACKWARD), active_streams)) return -1;

    return 0;
}

static int nut_read_close(AVEMFormatContext *s) {
    NUTContext * priv = s->priv_data;

    nut_demuxer_uninit(priv->nut);

    return 0;
}

AVEMInputFormat ff_libnut_demuxer = {
    .name           = "libnut",
    .long_name      = NULL_IF_CONFIG_SMALL("NUT format"),
    .priv_data_size = sizeof(NUTContext),
    .read_probe     = nut_probe,
    .read_header    = nut_read_header,
    .read_packet    = nut_read_packet,
    .read_close     = nut_read_close,
    .read_seek      = nut_read_seek,
    .extensions     = "nut",
};
