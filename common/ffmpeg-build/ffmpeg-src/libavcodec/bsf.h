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

#ifndef AVCODEC_BSF_H
#define AVCODEC_BSF_H

#include "avcodec.h"

/**
 * Called by the bitstream filters to get the next packet for filtering.
 * The filter is responsible for either freeing the packet or passing it to the
 * caller.
 */
int em_bsf_get_packet(AVBSFContext *ctx, AVEMPacket **pkt);

const AVEMClass *em_bsf_child_class_next(const AVEMClass *prev);

#endif /* AVCODEC_BSF_H */
