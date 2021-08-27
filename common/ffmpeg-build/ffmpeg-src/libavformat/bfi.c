/*
 * Brute Force & Ignorance (BFI) demuxer
 * Copyright (c) 2008 Sisir Koppaka
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
 * @brief Brute Force & Ignorance (.bfi) file demuxer
 * @author Sisir Koppaka ( sisir.koppaka at gmail dot com )
 * @see http://wiki.multimedia.cx/index.php?title=BFI
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

typedef struct BFIContext {
    int nframes;
    int audio_frame;
    int video_frame;
    int video_size;
    int avflag;
} BFIContext;

static int bfi_probe(AVEMProbeData * p)
{
    /* Check file header */
    if (AV_RL32(p->buf) == MKTAG('B', 'F', '&', 'I'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int bfi_read_header(AVEMFormatContext * s)
{
    BFIContext *bfi = s->priv_data;
    AVEMIOContext *pb = s->pb;
    AVEMStream *vstream;
    AVEMStream *astream;
    int fps, chunk_header;

    /* Initialize the video codec... */
    vstream = avformat_em_new_stream(s, NULL);
    if (!vstream)
        return AVERROR(ENOMEM);

    /* Initialize the audio codec... */
    astream = avformat_em_new_stream(s, NULL);
    if (!astream)
        return AVERROR(ENOMEM);

    /* Set the total number of frames. */
    avio_em_skip(pb, 8);
    chunk_header           = avio_em_rl32(pb);
    bfi->nframes           = avio_em_rl32(pb);
    avio_em_rl32(pb);
    avio_em_rl32(pb);
    avio_em_rl32(pb);
    fps                    = avio_em_rl32(pb);
    avio_em_skip(pb, 12);
    vstream->codecpar->width  = avio_em_rl32(pb);
    vstream->codecpar->height = avio_em_rl32(pb);

    /*Load the palette to extradata */
    avio_em_skip(pb, 8);
    vstream->codecpar->extradata      = av_em_alloc(768);
    if (!vstream->codecpar->extradata)
        return AVERROR(ENOMEM);
    vstream->codecpar->extradata_size = 768;
    avio_em_read(pb, vstream->codecpar->extradata,
               vstream->codecpar->extradata_size);

    astream->codecpar->sample_rate = avio_em_rl32(pb);

    /* Set up the video codec... */
    avpriv_em_set_pts_info(vstream, 32, 1, fps);
    vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vstream->codecpar->codec_id   = AV_CODEC_ID_BFI;
    vstream->codecpar->format     = AV_PIX_FMT_PAL8;
    vstream->nb_frames            =
    vstream->duration             = bfi->nframes;

    /* Set up the audio codec now... */
    astream->codecpar->codec_type      = AVMEDIA_TYPE_AUDIO;
    astream->codecpar->codec_id        = AV_CODEC_ID_PCM_U8;
    astream->codecpar->channels        = 1;
    astream->codecpar->channel_layout  = AV_CH_LAYOUT_MONO;
    astream->codecpar->bits_per_coded_sample = 8;
    astream->codecpar->bit_rate        =
        astream->codecpar->sample_rate * astream->codecpar->bits_per_coded_sample;
    avio_em_seek(pb, chunk_header - 3, SEEK_SET);
    avpriv_em_set_pts_info(astream, 64, 1, astream->codecpar->sample_rate);
    return 0;
}


static int bfi_read_packet(AVEMFormatContext * s, AVEMPacket * pkt)
{
    BFIContext *bfi = s->priv_data;
    AVEMIOContext *pb = s->pb;
    int ret, audio_offset, video_offset, chunk_size, audio_size = 0;
    if (bfi->nframes == 0 || avio_em_feof(pb)) {
        return AVERROR_EOF;
    }

    /* If all previous chunks were completely read, then find a new one... */
    if (!bfi->avflag) {
        uint32_t state = 0;
        while(state != MKTAG('S','A','V','I')){
            if (avio_em_feof(pb))
                return AVERROR(EIO);
            state = 256*state + avio_em_r8(pb);
        }
        /* Now that the chunk's location is confirmed, we proceed... */
        chunk_size      = avio_em_rl32(pb);
        avio_em_rl32(pb);
        audio_offset    = avio_em_rl32(pb);
        avio_em_rl32(pb);
        video_offset    = avio_em_rl32(pb);
        audio_size      = video_offset - audio_offset;
        bfi->video_size = chunk_size - video_offset;
        if (audio_size < 0 || bfi->video_size < 0) {
            av_em_log(s, AV_LOG_ERROR, "Invalid audio/video offsets or chunk size\n");
            return AVERROR_INVALIDDATA;
        }

        //Tossing an audio packet at the audio decoder.
        ret = av_em_get_packet(pb, pkt, audio_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->audio_frame;
        bfi->audio_frame += ret;
    } else if (bfi->video_size > 0) {

        //Tossing a video packet at the video decoder.
        ret = av_em_get_packet(pb, pkt, bfi->video_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->video_frame;
        bfi->video_frame += ret / bfi->video_size;

        /* One less frame to read. A cursory decrement. */
        bfi->nframes--;
    } else {
        /* Empty video packet */
        ret = AVERROR(EAGAIN);
    }

    bfi->avflag       = !bfi->avflag;
    pkt->stream_index = bfi->avflag;
    return ret;
}

AVEMInputFormat ff_bfi_demuxer = {
    .name           = "bfi",
    .long_name      = NULL_IF_CONFIG_SMALL("Brute Force & Ignorance"),
    .priv_data_size = sizeof(BFIContext),
    .read_probe     = bfi_probe,
    .read_header    = bfi_read_header,
    .read_packet    = bfi_read_packet,
};
