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

#ifndef AVFORMAT_AVIO_INTERNAL_H
#define AVFORMAT_AVIO_INTERNAL_H

#include "avio.h"
#include "url.h"

#include "libavutil/log.h"

extern const AVEMClass em_avio_class;

int emio_init_context(AVEMIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));


/**
 * Read size bytes from AVEMIOContext, returning a pointer.
 * Note that the data pointed at by the returned pointer is only
 * valid until the next call that references the same IO context.
 * @param s IO context
 * @param buf pointer to buffer into which to assemble the requested
 *    data if it is not available in contiguous addresses in the
 *    underlying buffer
 * @param size number of bytes requested
 * @param data address at which to store pointer: this will be a
 *    a direct pointer into the underlying buffer if the requested
 *    number of bytes are available at contiguous addresses, otherwise
 *    will be a copy of buf
 * @return number of bytes read or AVERROR
 */
int emio_read_indirect(AVEMIOContext *s, unsigned char *buf, int size, const unsigned char **data);

/**
 * Read size bytes from AVEMIOContext into buf.
 * This reads at most 1 packet. If that is not enough fewer bytes will be
 * returned.
 * @return number of bytes read or AVERROR
 */
int emio_read_partial(AVEMIOContext *s, unsigned char *buf, int size);

void emio_fill(AVEMIOContext *s, int b, int count);

static av_always_inline void emio_wfourcc(AVEMIOContext *pb, const uint8_t *s)
{
    avio_em_wl32(pb, MKTAG(s[0], s[1], s[2], s[3]));
}

/**
 * Rewind the AVEMIOContext using the specified buffer containing the first buf_size bytes of the file.
 * Used after probing to avoid seeking.
 * Joins buf and s->buffer, taking any overlap into consideration.
 * @note s->buffer must overlap with buf or they can't be joined and the function fails
 *
 * @param s The read-only AVEMIOContext to rewind
 * @param buf The probe buffer containing the first buf_size bytes of the file
 * @param buf_size The size of buf
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int emio_rewind_with_probe_data(AVEMIOContext *s, unsigned char **buf, int buf_size);

uint64_t emio_read_varlen(AVEMIOContext *bc);

/**
 * Read size bytes from AVEMIOContext into buf.
 * Check that exactly size bytes have been read.
 * @return number of bytes read or AVERROR
 */
int emio_read_size(AVEMIOContext *s, unsigned char *buf, int size);

/** @warning must be called before any I/O */
int emio_set_buf_size(AVEMIOContext *s, int buf_size);

/**
 * Ensures that the requested seekback buffer size will be available
 *
 * Will ensure that when reading sequentially up to buf_size, seeking
 * within the current pos and pos+buf_size is possible.
 * Once the stream position moves outside this window this guarantee is lost.
 */
int emio_ensure_seekback(AVEMIOContext *s, int64_t buf_size);

int emio_limit(AVEMIOContext *s, int size);

void emio_init_checksum(AVEMIOContext *s,
                        unsigned long (*update_checksum)(unsigned long c, const uint8_t *p, unsigned int len),
                        unsigned long checksum);
unsigned long emio_get_checksum(AVEMIOContext *s);
unsigned long em_crc04C11DB7_update(unsigned long checksum, const uint8_t *buf,
                                    unsigned int len);
unsigned long em_crcA001_update(unsigned long checksum, const uint8_t *buf,
                                unsigned int len);

/**
 * Open a write only packetized memory stream with a maximum packet
 * size of 'max_packet_size'.  The stream is stored in a memory buffer
 * with a big-endian 4 byte header giving the packet size in bytes.
 *
 * @param s new IO context
 * @param max_packet_size maximum packet size (must be > 0)
 * @return zero if no error.
 */
int emio_open_dyn_packet_buf(AVEMIOContext **s, int max_packet_size);

/**
 * Create and initialize a AVEMIOContext for accessing the
 * resource referenced by the EMURLContext h.
 * @note When the EMURLContext h has been opened in read+write mode, the
 * AVEMIOContext can be used only for writing.
 *
 * @param s Used to return the pointer to the created AVEMIOContext.
 * In case of failure the pointed to value is set to NULL.
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int emio_fdopen(AVEMIOContext **s, EMURLContext *h);

/**
 * Open a write-only fake memory stream. The written data is not stored
 * anywhere - this is only used for measuring the amount of data
 * written.
 *
 * @param s new IO context
 * @return zero if no error.
 */
int emio_open_null_buf(AVEMIOContext **s);

int emio_open_whitelist(AVEMIOContext **s, const char *url, int flags,
                         const AVEMIOInterruptCB *int_cb, AVEMDictionary **options,
                         const char *whitelist, const char *blacklist);

/**
 * Close a null buffer.
 *
 * @param s an IO context opened by emio_open_null_buf
 * @return the number of bytes written to the null buffer
 */
int emio_close_null_buf(AVEMIOContext *s);

/**
 * Free a dynamic buffer.
 *
 * @param s a pointer to an IO context opened by avio_em_open_dyn_buf()
 */
void emio_free_dyn_buf(AVEMIOContext **s);

#endif /* AVFORMAT_AVIO_INTERNAL_H */
