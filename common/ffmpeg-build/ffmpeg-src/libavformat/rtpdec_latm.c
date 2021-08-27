/*
 * RTP Depacketization of MP4A-LATM, RFC 3016
 * Copyright (c) 2010 Martin Storsjo
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

#include "avio_internal.h"
#include "rtpdec_formats.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"

struct PayloadContext {
    AVEMIOContext *dyn_buf;
    uint8_t *buf;
    int pos, len;
    uint32_t timestamp;
};

static void latm_close_context(PayloadContext *data)
{
    emio_free_dyn_buf(&data->dyn_buf);
    av_em_freep(&data->buf);
}

static int latm_parse_packet(AVEMFormatContext *ctx, PayloadContext *data,
                             AVEMStream *st, AVEMPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags)
{
    int ret, cur_len;

    if (buf) {
        if (!data->dyn_buf || data->timestamp != *timestamp) {
            av_em_freep(&data->buf);
            emio_free_dyn_buf(&data->dyn_buf);

            data->timestamp = *timestamp;
            if ((ret = avio_em_open_dyn_buf(&data->dyn_buf)) < 0)
                return ret;
        }
        avio_em_write(data->dyn_buf, buf, len);

        if (!(flags & RTP_FLAG_MARKER))
            return AVERROR(EAGAIN);
        av_em_freep(&data->buf);
        data->len = avio_em_close_dyn_buf(data->dyn_buf, &data->buf);
        data->dyn_buf = NULL;
        data->pos = 0;
    }

    if (!data->buf) {
        av_em_log(ctx, AV_LOG_ERROR, "No data available yet\n");
        return AVERROR(EIO);
    }

    cur_len = 0;
    while (data->pos < data->len) {
        uint8_t val = data->buf[data->pos++];
        cur_len += val;
        if (val != 0xff)
            break;
    }
    if (data->pos + cur_len > data->len) {
        av_em_log(ctx, AV_LOG_ERROR, "Malformed LATM packet\n");
        return AVERROR(EIO);
    }

    if ((ret = av_em_new_packet(pkt, cur_len)) < 0)
        return ret;
    memcpy(pkt->data, data->buf + data->pos, cur_len);
    data->pos += cur_len;
    pkt->stream_index = st->index;
    return data->pos < data->len;
}

static int parse_fmtp_config(AVEMStream *st, const char *value)
{
    int len = em_hex_to_data(NULL, value), i, ret = 0;
    GetBitContext gb;
    uint8_t *config;
    int audio_mux_version, same_time_framing, num_programs, num_layers;

    /* Pad this buffer, too, to avoid out of bounds reads with get_bits below */
    config = av_em_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!config)
        return AVERROR(ENOMEM);
    em_hex_to_data(config, value);
    init_get_bits(&gb, config, len*8);
    audio_mux_version = get_bits(&gb, 1);
    same_time_framing = get_bits(&gb, 1);
    skip_bits(&gb, 6); /* num_sub_frames */
    num_programs      = get_bits(&gb, 4);
    num_layers        = get_bits(&gb, 3);
    if (audio_mux_version != 0 || same_time_framing != 1 || num_programs != 0 ||
        num_layers != 0) {
        av_em_log(NULL, AV_LOG_WARNING, "Unsupported LATM config (%d,%d,%d,%d)\n",
                                     audio_mux_version, same_time_framing,
                                     num_programs, num_layers);
        ret = AVERROR_PATCHWELCOME;
        goto end;
    }
    av_em_freep(&st->codecpar->extradata);
    if (em_alloc_extradata(st->codecpar, (get_bits_left(&gb) + 7)/8)) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    for (i = 0; i < st->codecpar->extradata_size; i++)
        st->codecpar->extradata[i] = get_bits(&gb, 8);

end:
    av_em_free(config);
    return ret;
}

static int parse_fmtp(AVEMFormatContext *s,
                      AVEMStream *stream, PayloadContext *data,
                      const char *attr, const char *value)
{
    int res;

    if (!strcmp(attr, "config")) {
        res = parse_fmtp_config(stream, value);
        if (res < 0)
            return res;
    } else if (!strcmp(attr, "cpresent")) {
        int cpresent = atoi(value);
        if (cpresent != 0)
            avpriv_em_request_sample(s,
                                  "RTP MP4A-LATM with in-band configuration");
    }

    return 0;
}

static int latm_parse_sdp_line(AVEMFormatContext *s, int st_index,
                               PayloadContext *data, const char *line)
{
    const char *p;

    if (st_index < 0)
        return 0;

    if (av_em_strstart(line, "fmtp:", &p))
        return ff_parse_fmtp(s, s->streams[st_index], data, p, parse_fmtp);

    return 0;
}

RTPDynamicProtocolHandler ff_mp4a_latm_dynamic_handler = {
    .enc_name           = "MP4A-LATM",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = AV_CODEC_ID_AAC,
    .priv_data_size     = sizeof(PayloadContext),
    .parse_sdp_a_line   = latm_parse_sdp_line,
    .close              = latm_close_context,
    .parse_packet       = latm_parse_packet,
};
