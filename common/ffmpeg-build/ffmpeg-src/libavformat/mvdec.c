/*
 * Silicon Graphics Movie demuxer
 * Copyright (c) 2012 Peter Ross
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
 * Silicon Graphics Movie demuxer
 */

#include "libavutil/channel_layout.h"
#include "libavutil/eval.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/rational.h"

#include "avformat.h"
#include "internal.h"

typedef struct MvContext {
    int nb_video_tracks;
    int nb_audio_tracks;

    int eof_count;        ///< number of streams that have finished
    int stream_index;     ///< current stream index
    int frame[2];         ///< frame nb for current stream

    int acompression;     ///< compression level for audio stream
    int aformat;          ///< audio format
} MvContext;

#define AUDIO_FORMAT_SIGNED 401

static int mv_probe(AVEMProbeData *p)
{
    if (AV_RB32(p->buf) == MKBETAG('M', 'O', 'V', 'I') &&
        AV_RB16(p->buf + 4) < 3)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static char *var_read_string(AVEMIOContext *pb, int size)
{
    int n;
    char *str;

    if (size < 0 || size == INT_MAX)
        return NULL;

    str = av_em_alloc(size + 1);
    if (!str)
        return NULL;
    n = avio_em_get_str(pb, size, str, size + 1);
    if (n < size)
        avio_em_skip(pb, size - n);
    return str;
}

static int var_read_int(AVEMIOContext *pb, int size)
{
    int v;
    char *s = var_read_string(pb, size);
    if (!s)
        return 0;
    v = strtol(s, NULL, 10);
    av_em_free(s);
    return v;
}

static AVEMRational var_read_float(AVEMIOContext *pb, int size)
{
    AVEMRational v;
    char *s = var_read_string(pb, size);
    if (!s)
        return (AVEMRational) { 0, 0 };
    v = av_em_d2q(av_em_strtod(s, NULL), INT_MAX);
    av_em_free(s);
    return v;
}

static void var_read_metadata(AVEMFormatContext *avctx, const char *tag, int size)
{
    char *value = var_read_string(avctx->pb, size);
    if (value)
        av_em_dict_set(&avctx->metadata, tag, value, AV_DICT_DONT_STRDUP_VAL);
}

static int set_channels(AVEMFormatContext *avctx, AVEMStream *st, int channels)
{
    if (channels <= 0) {
        av_em_log(avctx, AV_LOG_ERROR, "Channel count %d invalid.\n", channels);
        return AVERROR_INVALIDDATA;
    }
    st->codecpar->channels       = channels;
    st->codecpar->channel_layout = (st->codecpar->channels == 1) ? AV_CH_LAYOUT_MONO
                                                                 : AV_CH_LAYOUT_STEREO;
    return 0;
}

/**
 * Parse global variable
 * @return < 0 if unknown
 */
static int parse_global_var(AVEMFormatContext *avctx, AVEMStream *st,
                            const char *name, int size)
{
    MvContext *mv = avctx->priv_data;
    AVEMIOContext *pb = avctx->pb;
    if (!strcmp(name, "__NUM_I_TRACKS")) {
        mv->nb_video_tracks = var_read_int(pb, size);
    } else if (!strcmp(name, "__NUM_A_TRACKS")) {
        mv->nb_audio_tracks = var_read_int(pb, size);
    } else if (!strcmp(name, "COMMENT") || !strcmp(name, "TITLE")) {
        var_read_metadata(avctx, name, size);
    } else if (!strcmp(name, "LOOP_MODE") || !strcmp(name, "NUM_LOOPS") ||
               !strcmp(name, "OPTIMIZED")) {
        avio_em_skip(pb, size); // ignore
    } else
        return AVERROR_INVALIDDATA;

    return 0;
}

/**
 * Parse audio variable
 * @return < 0 if unknown
 */
static int parse_audio_var(AVEMFormatContext *avctx, AVEMStream *st,
                           const char *name, int size)
{
    MvContext *mv = avctx->priv_data;
    AVEMIOContext *pb = avctx->pb;
    if (!strcmp(name, "__DIR_COUNT")) {
        st->nb_frames = var_read_int(pb, size);
    } else if (!strcmp(name, "AUDIO_FORMAT")) {
        mv->aformat = var_read_int(pb, size);
    } else if (!strcmp(name, "COMPRESSION")) {
        mv->acompression = var_read_int(pb, size);
    } else if (!strcmp(name, "DEFAULT_VOL")) {
        var_read_metadata(avctx, name, size);
    } else if (!strcmp(name, "NUM_CHANNELS")) {
        return set_channels(avctx, st, var_read_int(pb, size));
    } else if (!strcmp(name, "SAMPLE_RATE")) {
        st->codecpar->sample_rate = var_read_int(pb, size);
        avpriv_em_set_pts_info(st, 33, 1, st->codecpar->sample_rate);
    } else if (!strcmp(name, "SAMPLE_WIDTH")) {
        st->codecpar->bits_per_coded_sample = var_read_int(pb, size) * 8;
    } else
        return AVERROR_INVALIDDATA;

    return 0;
}

/**
 * Parse video variable
 * @return < 0 if unknown
 */
static int parse_video_var(AVEMFormatContext *avctx, AVEMStream *st,
                           const char *name, int size)
{
    AVEMIOContext *pb = avctx->pb;
    if (!strcmp(name, "__DIR_COUNT")) {
        st->nb_frames = st->duration = var_read_int(pb, size);
    } else if (!strcmp(name, "COMPRESSION")) {
        char *str = var_read_string(pb, size);
        if (!str)
            return AVERROR_INVALIDDATA;
        if (!strcmp(str, "1")) {
            st->codecpar->codec_id = AV_CODEC_ID_MVC1;
        } else if (!strcmp(str, "2")) {
            st->codecpar->format = AV_PIX_FMT_ABGR;
            st->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else if (!strcmp(str, "3")) {
            st->codecpar->codec_id = AV_CODEC_ID_SGIRLE;
        } else if (!strcmp(str, "10")) {
            st->codecpar->codec_id = AV_CODEC_ID_MJPEG;
        } else if (!strcmp(str, "MVC2")) {
            st->codecpar->codec_id = AV_CODEC_ID_MVC2;
        } else {
            avpriv_em_request_sample(avctx, "Video compression %s", str);
        }
        av_em_free(str);
    } else if (!strcmp(name, "FPS")) {
        AVEMRational fps = var_read_float(pb, size);
        avpriv_em_set_pts_info(st, 64, fps.den, fps.num);
        st->avg_frame_rate = fps;
    } else if (!strcmp(name, "HEIGHT")) {
        st->codecpar->height = var_read_int(pb, size);
    } else if (!strcmp(name, "PIXEL_ASPECT")) {
        st->sample_aspect_ratio = var_read_float(pb, size);
        av_em_reduce(&st->sample_aspect_ratio.num, &st->sample_aspect_ratio.den,
                  st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                  INT_MAX);
    } else if (!strcmp(name, "WIDTH")) {
        st->codecpar->width = var_read_int(pb, size);
    } else if (!strcmp(name, "ORIENTATION")) {
        if (var_read_int(pb, size) == 1101) {
            st->codecpar->extradata      = av_em_strdup("BottomUp");
            st->codecpar->extradata_size = 9;
        }
    } else if (!strcmp(name, "Q_SPATIAL") || !strcmp(name, "Q_TEMPORAL")) {
        var_read_metadata(avctx, name, size);
    } else if (!strcmp(name, "INTERLACING") || !strcmp(name, "PACKING")) {
        avio_em_skip(pb, size); // ignore
    } else
        return AVERROR_INVALIDDATA;

    return 0;
}

static int read_table(AVEMFormatContext *avctx, AVEMStream *st,
                       int (*parse)(AVEMFormatContext *avctx, AVEMStream *st,
                                    const char *name, int size))
{
    int count, i;
    AVEMIOContext *pb = avctx->pb;
    avio_em_skip(pb, 4);
    count = avio_em_rb32(pb);
    avio_em_skip(pb, 4);
    for (i = 0; i < count; i++) {
        char name[17];
        int size;
        avio_em_read(pb, name, 16);
        name[sizeof(name) - 1] = 0;
        size = avio_em_rb32(pb);
        if (size < 0) {
            av_em_log(avctx, AV_LOG_ERROR, "entry size %d is invalid\n", size);
            return AVERROR_INVALIDDATA;
        }
        if (parse(avctx, st, name, size) < 0) {
            avpriv_em_request_sample(avctx, "Variable %s", name);
            avio_em_skip(pb, size);
        }
    }
    return 0;
}

static void read_index(AVEMIOContext *pb, AVEMStream *st)
{
    uint64_t timestamp = 0;
    int i;
    for (i = 0; i < st->nb_frames; i++) {
        uint32_t pos  = avio_em_rb32(pb);
        uint32_t size = avio_em_rb32(pb);
        avio_em_skip(pb, 8);
        av_em_add_index_entry(st, pos, timestamp, size, 0, AVINDEX_KEYFRAME);
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            timestamp += size / (st->codecpar->channels * 2);
        } else {
            timestamp++;
        }
    }
}

static int mv_read_header(AVEMFormatContext *avctx)
{
    MvContext *mv = avctx->priv_data;
    AVEMIOContext *pb = avctx->pb;
    AVEMStream *ast = NULL, *vst = NULL; //initialization to suppress warning
    int version, i;
    int ret;

    avio_em_skip(pb, 4);

    version = avio_em_rb16(pb);
    if (version == 2) {
        uint64_t timestamp;
        int v;
        avio_em_skip(pb, 22);

        /* allocate audio track first to prevent unnecessary seeking
         * (audio packet always precede video packet for a given frame) */
        ast = avformat_em_new_stream(avctx, NULL);
        if (!ast)
            return AVERROR(ENOMEM);

        vst = avformat_em_new_stream(avctx, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        avpriv_em_set_pts_info(vst, 64, 1, 15);
        vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        vst->avg_frame_rate    = av_em_inv_q(vst->time_base);
        vst->nb_frames         = avio_em_rb32(pb);
        v = avio_em_rb32(pb);
        switch (v) {
        case 1:
            vst->codecpar->codec_id = AV_CODEC_ID_MVC1;
            break;
        case 2:
            vst->codecpar->format = AV_PIX_FMT_ARGB;
            vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
            break;
        default:
            avpriv_em_request_sample(avctx, "Video compression %i", v);
            break;
        }
        vst->codecpar->codec_tag = 0;
        vst->codecpar->width     = avio_em_rb32(pb);
        vst->codecpar->height    = avio_em_rb32(pb);
        avio_em_skip(pb, 12);

        ast->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        ast->nb_frames          = vst->nb_frames;
        ast->codecpar->sample_rate = avio_em_rb32(pb);
        avpriv_em_set_pts_info(ast, 33, 1, ast->codecpar->sample_rate);
        if (set_channels(avctx, ast, avio_em_rb32(pb)) < 0)
            return AVERROR_INVALIDDATA;

        v = avio_em_rb32(pb);
        if (v == AUDIO_FORMAT_SIGNED) {
            ast->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
        } else {
            avpriv_em_request_sample(avctx, "Audio compression (format %i)", v);
        }

        avio_em_skip(pb, 12);
        var_read_metadata(avctx, "title", 0x80);
        var_read_metadata(avctx, "comment", 0x100);
        avio_em_skip(pb, 0x80);

        timestamp = 0;
        for (i = 0; i < vst->nb_frames; i++) {
            uint32_t pos   = avio_em_rb32(pb);
            uint32_t asize = avio_em_rb32(pb);
            uint32_t vsize = avio_em_rb32(pb);
            avio_em_skip(pb, 8);
            av_em_add_index_entry(ast, pos, timestamp, asize, 0, AVINDEX_KEYFRAME);
            av_em_add_index_entry(vst, pos + asize, i, vsize, 0, AVINDEX_KEYFRAME);
            timestamp += asize / (ast->codecpar->channels * 2);
        }
    } else if (!version && avio_em_rb16(pb) == 3) {
        avio_em_skip(pb, 4);

        if ((ret = read_table(avctx, NULL, parse_global_var)) < 0)
            return ret;

        if (mv->nb_audio_tracks > 1) {
            avpriv_em_request_sample(avctx, "Multiple audio streams support");
            return AVERROR_PATCHWELCOME;
        } else if (mv->nb_audio_tracks) {
            ast = avformat_em_new_stream(avctx, NULL);
            if (!ast)
                return AVERROR(ENOMEM);
            ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            if ((read_table(avctx, ast, parse_audio_var)) < 0)
                return ret;
            if (mv->acompression == 100 &&
                mv->aformat == AUDIO_FORMAT_SIGNED &&
                ast->codecpar->bits_per_coded_sample == 16) {
                ast->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
            } else {
                avpriv_em_request_sample(avctx,
                                      "Audio compression %i (format %i, sr %i)",
                                      mv->acompression, mv->aformat,
                                      ast->codecpar->bits_per_coded_sample);
                ast->codecpar->codec_id = AV_CODEC_ID_NONE;
            }
            if (ast->codecpar->channels <= 0) {
                av_em_log(avctx, AV_LOG_ERROR, "No valid channel count found.\n");
                return AVERROR_INVALIDDATA;
            }
        }

        if (mv->nb_video_tracks > 1) {
            avpriv_em_request_sample(avctx, "Multiple video streams support");
            return AVERROR_PATCHWELCOME;
        } else if (mv->nb_video_tracks) {
            vst = avformat_em_new_stream(avctx, NULL);
            if (!vst)
                return AVERROR(ENOMEM);
            vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            if ((ret = read_table(avctx, vst, parse_video_var))<0)
                return ret;
        }

        if (mv->nb_audio_tracks)
            read_index(pb, ast);

        if (mv->nb_video_tracks)
            read_index(pb, vst);
    } else {
        avpriv_em_request_sample(avctx, "Version %i", version);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int mv_read_packet(AVEMFormatContext *avctx, AVEMPacket *pkt)
{
    MvContext *mv = avctx->priv_data;
    AVEMIOContext *pb = avctx->pb;
    AVEMStream *st = avctx->streams[mv->stream_index];
    const AVEMIndexEntry *index;
    int frame = mv->frame[mv->stream_index];
    int64_t ret;
    uint64_t pos;

    if (frame < st->nb_index_entries) {
        index = &st->index_entries[frame];
        pos   = avio_em_tell(pb);
        if (index->pos > pos)
            avio_em_skip(pb, index->pos - pos);
        else if (index->pos < pos) {
            if (!pb->seekable)
                return AVERROR(EIO);
            ret = avio_em_seek(pb, index->pos, SEEK_SET);
            if (ret < 0)
                return ret;
        }
        ret = av_em_get_packet(pb, pkt, index->size);
        if (ret < 0)
            return ret;

        pkt->stream_index = mv->stream_index;
        pkt->pts          = index->timestamp;
        pkt->flags       |= AV_PKT_FLAG_KEY;

        mv->frame[mv->stream_index]++;
        mv->eof_count = 0;
    } else {
        mv->eof_count++;
        if (mv->eof_count >= avctx->nb_streams)
            return AVERROR_EOF;

        // avoid returning 0 without a packet
        return AVERROR(EAGAIN);
    }

    mv->stream_index++;
    if (mv->stream_index >= avctx->nb_streams)
        mv->stream_index = 0;

    return 0;
}

static int mv_read_seek(AVEMFormatContext *avctx, int stream_index,
                        int64_t timestamp, int flags)
{
    MvContext *mv = avctx->priv_data;
    AVEMStream *st = avctx->streams[stream_index];
    int frame, i;

    if ((flags & AVSEEK_FLAG_FRAME) || (flags & AVSEEK_FLAG_BYTE))
        return AVERROR(ENOSYS);

    if (!avctx->pb->seekable)
        return AVERROR(EIO);

    frame = av_em_index_search_timestamp(st, timestamp, flags);
    if (frame < 0)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < avctx->nb_streams; i++)
        mv->frame[i] = frame;
    return 0;
}

AVEMInputFormat ff_mv_demuxer = {
    .name           = "mv",
    .long_name      = NULL_IF_CONFIG_SMALL("Silicon Graphics Movie"),
    .priv_data_size = sizeof(MvContext),
    .read_probe     = mv_probe,
    .read_header    = mv_read_header,
    .read_packet    = mv_read_packet,
    .read_seek      = mv_read_seek,
};
