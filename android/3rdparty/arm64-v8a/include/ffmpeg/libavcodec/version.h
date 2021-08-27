/*
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

#ifndef avcodec_em_version_H
#define avcodec_em_version_H

/**
 * @file
 * @ingroup libavc
 * Libavcodec version macros.
 */

#include "libavutil/version.h"

#define LIBavcodec_em_version_MAJOR  57
#define LIBavcodec_em_version_MINOR  48
#define LIBavcodec_em_version_MICRO 101

#define LIBavcodec_em_version_INT  AV_VERSION_INT(LIBavcodec_em_version_MAJOR, \
                                               LIBavcodec_em_version_MINOR, \
                                               LIBavcodec_em_version_MICRO)
#define LIBavcodec_em_version      AV_VERSION(LIBavcodec_em_version_MAJOR,    \
                                           LIBavcodec_em_version_MINOR,    \
                                           LIBavcodec_em_version_MICRO)
#define LIBAVCODEC_BUILD        LIBavcodec_em_version_INT

#define LIBAVCODEC_IDENT        "Lavc" AV_STRINGIFY(LIBavcodec_em_version)

/**
 * FF_API_* defines may be placed below to indicate public API that will be
 * dropped at a future version bump. The defines themselves are not part of
 * the public API and may change, break or disappear at any time.
 *
 * @note, when bumping the major version it is recommended to manually
 * disable each FF_API_* in its own commit instead of disabling them all
 * at once through the bump. This improves the git bisect-ability of the change.
 */

#ifndef FF_API_VIMA_DECODER
#define FF_API_VIMA_DECODER     (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AUDIO_CONVERT
#define FF_API_AUDIO_CONVERT     (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AVCODEC_RESAMPLE
#define FF_API_AVCODEC_RESAMPLE  FF_API_AUDIO_CONVERT
#endif
#ifndef FF_API_GETCHROMA
#define FF_API_GETCHROMA         (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_MISSING_SAMPLE
#define FF_API_MISSING_SAMPLE    (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_LOWRES
#define FF_API_LOWRES            (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_CAP_VDPAU
#define FF_API_CAP_VDPAU         (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_BUFS_VDPAU
#define FF_API_BUFS_VDPAU        (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_VOXWARE
#define FF_API_VOXWARE           (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_SET_DIMENSIONS
#define FF_API_SET_DIMENSIONS    (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_DEBUG_MV
#define FF_API_DEBUG_MV          (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AC_VLC
#define FF_API_AC_VLC            (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_OLD_MSMPEG4
#define FF_API_OLD_MSMPEG4       (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_ASPECT_EXTENDED
#define FF_API_ASPECT_EXTENDED   (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_ARCH_ALPHA
#define FF_API_ARCH_ALPHA        (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_XVMC
#define FF_API_XVMC              (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_ERROR_RATE
#define FF_API_ERROR_RATE        (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_QSCALE_TYPE
#define FF_API_QSCALE_TYPE       (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_MB_TYPE
#define FF_API_MB_TYPE           (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_MAX_BFRAMES
#define FF_API_MAX_BFRAMES       (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_NEG_LINESIZES
#define FF_API_NEG_LINESIZES     (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_EMU_EDGE
#define FF_API_EMU_EDGE          (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_ARCH_SH4
#define FF_API_ARCH_SH4          (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_ARCH_SPARC
#define FF_API_ARCH_SPARC        (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_UNUSED_MEMBERS
#define FF_API_UNUSED_MEMBERS    (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_IDCT_XVIDMMX
#define FF_API_IDCT_XVIDMMX      (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_INPUT_PRESERVED
#define FF_API_INPUT_PRESERVED   (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_NORMALIZE_AQP
#define FF_API_NORMALIZE_AQP     (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_GMC
#define FF_API_GMC               (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_MV0
#define FF_API_MV0               (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_CODEC_NAME
#define FF_API_CODEC_NAME        (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AFD
#define FF_API_AFD               (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_VISMV
/* XXX: don't forget to drop the -vismv documentation */
#define FF_API_VISMV             (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AUDIOENC_DELAY
#define FF_API_AUDIOENC_DELAY    (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_VAAPI_CONTEXT
#define FF_API_VAAPI_CONTEXT     (LIBavcodec_em_version_MAJOR < 58)
#endif
#ifndef FF_API_AVCTX_TIMEBASE
#define FF_API_AVCTX_TIMEBASE    (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_MPV_OPT
#define FF_API_MPV_OPT           (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_STREAM_CODEC_TAG
#define FF_API_STREAM_CODEC_TAG  (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_QUANT_BIAS
#define FF_API_QUANT_BIAS        (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_RC_STRATEGY
#define FF_API_RC_STRATEGY       (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_CODED_FRAME
#define FF_API_CODED_FRAME       (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_MOTION_EST
#define FF_API_MOTION_EST        (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_WITHOUT_PREFIX
#define FF_API_WITHOUT_PREFIX    (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_SIDEDATA_ONLY_PKT
#define FF_API_SIDEDATA_ONLY_PKT (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_VDPAU_PROFILE
#define FF_API_VDPAU_PROFILE     (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_CONVERGENCE_DURATION
#define FF_API_CONVERGENCE_DURATION (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_AVPICTURE
#define FF_API_AVPICTURE         (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_AVPACKET_OLD_API
#define FF_API_AVPACKET_OLD_API (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_RTP_CALLBACK
#define FF_API_RTP_CALLBACK      (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_VBV_DELAY
#define FF_API_VBV_DELAY         (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_CODER_TYPE
#define FF_API_CODER_TYPE        (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_STAT_BITS
#define FF_API_STAT_BITS         (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_PRIVATE_OPT
#define FF_API_PRIVATE_OPT      (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_ASS_TIMING
#define FF_API_ASS_TIMING       (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_OLD_BSF
#define FF_API_OLD_BSF          (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_COPY_CONTEXT
#define FF_API_COPY_CONTEXT     (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_GET_CONTEXT_DEFAULTS
#define FF_API_GET_CONTEXT_DEFAULTS (LIBavcodec_em_version_MAJOR < 59)
#endif
#ifndef FF_API_NVENC_OLD_NAME
#define FF_API_NVENC_OLD_NAME    (LIBavcodec_em_version_MAJOR < 59)
#endif

#endif /* avcodec_em_version_H */
