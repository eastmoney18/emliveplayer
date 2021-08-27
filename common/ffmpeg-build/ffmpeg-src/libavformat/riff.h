/*
 * RIFF common functions and data
 * copyright (c) 2000 Fabrice Bellard
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
 * internal header for RIFF based (de)muxers
 * do NOT include this in end user applications
 */

#ifndef AVFORMAT_RIFF_H
#define AVFORMAT_RIFF_H

#include "libavcodec/avcodec.h"
#include "avio.h"
#include "internal.h"
#include "metadata.h"

extern const AVMetadataConv em_riff_info_conv[];

int64_t ff_start_tag(AVEMIOContext *pb, const char *tag);
void ff_end_tag(AVEMIOContext *pb, int64_t start);

/**
 * Read BITMAPINFOHEADER structure and set AVEMStream codec width, height and
 * bits_per_encoded_sample fields. Does not read extradata.
 * @return codec tag
 */
int ff_get_bmp_header(AVEMIOContext *pb, AVEMStream *st, unsigned *esize);

void ff_put_bmp_header(AVEMIOContext *pb, AVEMCodecParameters *par, const AVEMCodecTag *tags, int for_asf, int ignore_extradata);

/**
 * Tell ff_put_wav_header() to use WAVEFORMATEX even for PCM codecs.
 */
#define FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX    0x00000001

/**
 * Tell ff_put_wav_header() to write an empty channel mask.
 */
#define FF_PUT_WAV_HEADER_SKIP_CHANNELMASK      0x00000002

/**
 * Write WAVEFORMAT header structure.
 *
 * @param flags a combination of FF_PUT_WAV_HEADER_* constants
 *
 * @return the size or -1 on error
 */
int ff_put_wav_header(AVEMFormatContext *s, AVEMIOContext *pb, AVEMCodecParameters *par, int flags);

enum AVEMCodecID ff_wav_em_codec_get_id(unsigned int tag, int bps);
int ff_get_wav_header(AVEMFormatContext *s, AVEMIOContext *pb, AVEMCodecParameters *par, int size, int big_endian);

extern const AVEMCodecTag em_codec_bmp_tags[]; // exposed through avformat_em_get_riff_video_tags()
extern const AVEMCodecTag em_codec_wav_tags[];

void ff_parse_specific_params(AVEMStream *st, int *au_rate, int *au_ssize, int *au_scale);

int ff_read_riff_info(AVEMFormatContext *s, int64_t size);

/**
 * Write all recognized RIFF tags from s->metadata
 */
void em_riff_write_info(AVEMFormatContext *s);

/**
 * Write a single RIFF info tag
 */
void em_riff_write_info_tag(AVEMIOContext *pb, const char *tag, const char *str);

typedef uint8_t ff_asf_guid[16];

typedef struct AVEMCodecGuid {
    enum AVEMCodecID id;
    ff_asf_guid guid;
} AVEMCodecGuid;

extern const AVEMCodecGuid em_codec_wav_guids[];

#define FF_PRI_GUID \
    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x " \
    "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}"

#define FF_ARG_GUID(g) \
    g[0], g[1], g[2],  g[3],  g[4],  g[5],  g[6],  g[7], \
    g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15],\
    g[3], g[2], g[1],  g[0],  g[5],  g[4],  g[7],  g[6], \
    g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]

#define FF_MEDIASUBTYPE_BASE_GUID \
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
#define FF_AMBISONIC_BASE_GUID \
    0x21, 0x07, 0xD3, 0x11, 0x86, 0x44, 0xC8, 0xC1, 0xCA, 0x00, 0x00, 0x00

static av_always_inline int ff_guidcmp(const void *g1, const void *g2)
{
    return memcmp(g1, g2, sizeof(ff_asf_guid));
}

int ff_get_guid(AVEMIOContext *s, ff_asf_guid *g);
void ff_put_guid(AVEMIOContext *s, const ff_asf_guid *g);
const ff_asf_guid *ff_get_codec_guid(enum AVEMCodecID id, const AVEMCodecGuid *av_guid);

enum AVEMCodecID em_codec_guid_get_id(const AVEMCodecGuid *guids, ff_asf_guid guid);

#endif /* AVFORMAT_RIFF_H */
