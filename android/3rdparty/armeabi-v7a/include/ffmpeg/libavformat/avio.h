/*
 * copyright (c) 2001 Fabrice Bellard
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
#ifndef AVFORMAT_AVIO_H
#define AVFORMAT_AVIO_H

/**
 * @file
 * @ingroup lavf_io
 * Buffered I/O operations
 */

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"

#include "libavformat/version.h"

#define AVIO_SEEKABLE_NORMAL 0x0001 /**< Seeking works like for a local file */

/**
 * Callback for checking whether to abort blocking functions.
 * AVERROR_EXIT is returned in this case by the interrupted
 * function. During blocking operations, callback is called with
 * opaque as parameter. If the callback returns 1, the
 * blocking operation will be aborted.
 *
 * No members can be added to this struct without a major bump, if
 * new elements have been added after this struct in AVEMFormatContext
 * or AVEMIOContext.
 */
typedef struct AVEMIOInterruptCB {
    int (*callback)(void*);
    void *opaque;
} AVEMIOInterruptCB;

/**
 * Directory entry types.
 */
enum AVEMIODirEntryType {
    AVIO_ENTRY_UNKNOWN,
    AVIO_ENTRY_BLOCK_DEVICE,
    AVIO_ENTRY_CHARACTER_DEVICE,
    AVIO_ENTRY_DIRECTORY,
    AVIO_ENTRY_NAMED_PIPE,
    AVIO_ENTRY_SYMBOLIC_LINK,
    AVIO_ENTRY_SOCKET,
    AVIO_ENTRY_FILE,
    AVIO_ENTRY_SERVER,
    AVIO_ENTRY_SHARE,
    AVIO_ENTRY_WORKGROUP,
};

/**
 * Describes single entry of the directory.
 *
 * Only name and type fields are guaranteed be set.
 * Rest of fields are protocol or/and platform dependent and might be unknown.
 */
typedef struct AVEMIODirEntry {
    char *name;                           /**< Filename */
    int type;                             /**< Type of the entry */
    int utf8;                             /**< Set to 1 when name is encoded with UTF-8, 0 otherwise.
                                               Name can be encoded with UTF-8 even though 0 is set. */
    int64_t size;                         /**< File size in bytes, -1 if unknown. */
    int64_t modification_timestamp;       /**< Time of last modification in microseconds since unix
                                               epoch, -1 if unknown. */
    int64_t access_timestamp;             /**< Time of last access in microseconds since unix epoch,
                                               -1 if unknown. */
    int64_t status_change_timestamp;      /**< Time of last status change in microseconds since unix
                                               epoch, -1 if unknown. */
    int64_t user_id;                      /**< User ID of owner, -1 if unknown. */
    int64_t group_id;                     /**< Group ID of owner, -1 if unknown. */
    int64_t filemode;                     /**< Unix file mode, -1 if unknown. */
} AVEMIODirEntry;

typedef struct AVEMIODirContext {
    struct EMURLContext *url_context;
} AVEMIODirContext;

/**
 * Different data types that can be returned via the AVIO
 * write_data_type callback.
 */
enum AVEMIODataMarkerType {
    /**
     * Header data; this needs to be present for the stream to be decodeable.
     */
    AVIO_DATA_MARKER_HEADER,
    /**
     * A point in the output bytestream where a decoder can start decoding
     * (i.e. a keyframe). A demuxer/decoder given the data flagged with
     * AVIO_DATA_MARKER_HEADER, followed by any AVIO_DATA_MARKER_SYNC_POINT,
     * should give decodeable results.
     */
    AVIO_DATA_MARKER_SYNC_POINT,
    /**
     * A point in the output bytestream where a demuxer can start parsing
     * (for non self synchronizing bytestream formats). That is, any
     * non-keyframe packet start point.
     */
    AVIO_DATA_MARKER_BOUNDARY_POINT,
    /**
     * This is any, unlabelled data. It can either be a muxer not marking
     * any positions at all, it can be an actual boundary/sync point
     * that the muxer chooses not to mark, or a later part of a packet/fragment
     * that is cut into multiple write callbacks due to limited IO buffer size.
     */
    AVIO_DATA_MARKER_UNKNOWN,
    /**
     * Trailer data, which doesn't contain actual content, but only for
     * finalizing the output file.
     */
    AVIO_DATA_MARKER_TRAILER
};

/**
 * Bytestream IO Context.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 * sizeof(AVEMIOContext) must not be used outside libav*.
 *
 * @note None of the function pointers in AVEMIOContext should be called
 *       directly, they should only be set by the client application
 *       when implementing custom I/O. Normally these are set to the
 *       function pointers specified in avio_em_alloc_context()
 */
typedef struct AVEMIOContext {
    /**
     * A class for private options.
     *
     * If this AVEMIOContext is created by avio_em_open2(), av_class is set and
     * passes the options down to protocols.
     *
     * If this AVEMIOContext is manually allocated, then av_class may be set by
     * the caller.
     *
     * warning -- this field can be NULL, be sure to not pass this AVEMIOContext
     * to any av_opt_* functions in that case.
     */
    const AVEMClass *av_class;

    /*
     * The following shows the relationship between buffer, buf_ptr, buf_end, buf_size,
     * and pos, when reading and when writing (since AVEMIOContext is used for both):
     *
     **********************************************************************************
     *                                   READING
     **********************************************************************************
     *
     *                            |              buffer_size              |
     *                            |---------------------------------------|
     *                            |                                       |
     *
     *                         buffer          buf_ptr       buf_end
     *                            +---------------+-----------------------+
     *                            |/ / / / / / / /|/ / / / / / /|         |
     *  read buffer:              |/ / consumed / | to be read /|         |
     *                            |/ / / / / / / /|/ / / / / / /|         |
     *                            +---------------+-----------------------+
     *
     *                                                         pos
     *              +-------------------------------------------+-----------------+
     *  input file: |                                           |                 |
     *              +-------------------------------------------+-----------------+
     *
     *
     **********************************************************************************
     *                                   WRITING
     **********************************************************************************
     *
     *                                          |          buffer_size          |
     *                                          |-------------------------------|
     *                                          |                               |
     *
     *                                       buffer              buf_ptr     buf_end
     *                                          +-------------------+-----------+
     *                                          |/ / / / / / / / / /|           |
     *  write buffer:                           | / to be flushed / |           |
     *                                          |/ / / / / / / / / /|           |
     *                                          +-------------------+-----------+
     *
     *                                         pos
     *               +--------------------------+-----------------------------------+
     *  output file: |                          |                                   |
     *               +--------------------------+-----------------------------------+
     *
     */
    unsigned char *buffer;  /**< Start of the buffer. */
    int buffer_size;        /**< Maximum buffer size */
    unsigned char *buf_ptr; /**< Current position in the buffer */
    unsigned char *buf_end; /**< End of the data, may be less than
                                 buffer+buffer_size if the read function returned
                                 less data than requested, e.g. for streams where
                                 no more data has been received yet. */
    unsigned char *buffer_prefix; /** stream prefix buffer**/
    int prefix_buffer_size; /** prefix buffer size **/
    int prefix_buffer_max_size; /** prefix buffer max size **/
    int b_save_prefix_buffer;
    int b_need_seek_to_prefix_buffer_size;
    void *opaque;           /**< A private pointer, passed to the read/write/seek/...
                                 functions. */
    int (*read_packet)(void *opaque, uint8_t *buf, int buf_size);
    int (*write_packet)(void *opaque, uint8_t *buf, int buf_size);
    int64_t (*seek)(void *opaque, int64_t offset, int whence);
    int64_t pos;            /**< position in the file of the current buffer */
    int must_flush;         /**< true if the next seek should flush */
    int eof_reached;        /**< true if eof reached */
    int write_flag;         /**< true if open for writing */
    int max_packet_size;
    unsigned long checksum;
    unsigned char *checksum_ptr;
    unsigned long (*update_checksum)(unsigned long checksum, const uint8_t *buf, unsigned int size);
    int error;              /**< contains the error code or 0 if no error happened */
    /**
     * Pause or resume playback for network streaming protocols - e.g. MMS.
     */
    int (*read_pause)(void *opaque, int pause);
    /**
     * Seek to a given timestamp in stream with the specified stream_index.
     * Needed for some network streaming protocols which don't support seeking
     * to byte position.
     */
    int64_t (*read_seek)(void *opaque, int stream_index,
                         int64_t timestamp, int flags);
    /**
     * A combination of AVIO_SEEKABLE_ flags or 0 when the stream is not seekable.
     */
    int seekable;

    /**
     * max filesize, used to limit allocations
     * This field is internal to libavformat and access from outside is not allowed.
     */
    int64_t maxsize;

    /**
     * avio_em_read and avio_em_write should if possible be satisfied directly
     * instead of going through a buffer, and avio_em_seek will always
     * call the underlying seek function directly.
     */
    int direct;

    /**
     * Bytes read statistic
     * This field is internal to libavformat and access from outside is not allowed.
     */
    int64_t bytes_read;

    /**
     * seek statistic
     * This field is internal to libavformat and access from outside is not allowed.
     */
    int seek_count;

    /**
     * writeout statistic
     * This field is internal to libavformat and access from outside is not allowed.
     */
    int writeout_count;

    /**
     * Original buffer size
     * used internally after probing and ensure seekback to reset the buffer size
     * This field is internal to libavformat and access from outside is not allowed.
     */
    int orig_buffer_size;

    /**
     * Threshold to favor readahead over seek.
     * This is current internal only, do not use from outside.
     */
    int short_seek_threshold;

    /**
     * ',' separated list of allowed protocols.
     */
    const char *protocol_whitelist;

    /**
     * ',' separated list of disallowed protocols.
     */
    const char *protocol_blacklist;

    /**
     * A callback that is used instead of write_packet.
     */
    int (*write_data_type)(void *opaque, uint8_t *buf, int buf_size,
                           enum AVEMIODataMarkerType type, int64_t time);
    /**
     * If set, don't call write_data_type separately for AVIO_DATA_MARKER_BOUNDARY_POINT,
     * but ignore them and treat them as AVIO_DATA_MARKER_UNKNOWN (to avoid needlessly
     * small chunks of data returned from the callback).
     */
    int ignore_boundary_point;

    /**
     * Internal, not meant to be used from outside of AVEMIOContext.
     */
    enum AVEMIODataMarkerType current_type;
    int64_t last_time;
} AVEMIOContext;

/**
 * Return the name of the protocol that will handle the passed URL.
 *
 * NULL is returned if no protocol could be found for the given URL.
 *
 * @return Name of the protocol or NULL.
 */
const char *avio_em_find_protocol_name(const char *url);

/**
 * Return AVIO_FLAG_* access flags corresponding to the access permissions
 * of the resource in url, or a negative value corresponding to an
 * AVERROR code in case of failure. The returned access flags are
 * masked by the value in flags.
 *
 * @note This function is intrinsically unsafe, in the sense that the
 * checked resource may change its existence or permission status from
 * one call to another. Thus you should not trust the returned value,
 * unless you are sure that no other processes are accessing the
 * checked resource.
 */
int avio_em_check(const char *url, int flags);

/**
 * Move or rename a resource.
 *
 * @note url_src and url_dst should share the same protocol and authority.
 *
 * @param url_src url to resource to be moved
 * @param url_dst new url to resource if the operation succeeded
 * @return >=0 on success or negative on error.
 */
int avpriv_em_io_move(const char *url_src, const char *url_dst);

/**
 * Delete a resource.
 *
 * @param url resource to be deleted.
 * @return >=0 on success or negative on error.
 */
int avpriv_em_io_delete(const char *url);

/**
 * Open directory for reading.
 *
 * @param s       directory read context. Pointer to a NULL pointer must be passed.
 * @param url     directory to be listed.
 * @param options A dictionary filled with protocol-private options. On return
 *                this parameter will be destroyed and replaced with a dictionary
 *                containing options that were not found. May be NULL.
 * @return >=0 on success or negative on error.
 */
int avio_em_open_dir(AVEMIODirContext **s, const char *url, AVEMDictionary **options);

/**
 * Get next directory entry.
 *
 * Returned entry must be freed with avio_em_free_directory_entry(). In particular
 * it may outlive AVEMIODirContext.
 *
 * @param s         directory read context.
 * @param[out] next next entry or NULL when no more entries.
 * @return >=0 on success or negative on error. End of list is not considered an
 *             error.
 */
int avio_em_read_dir(AVEMIODirContext *s, AVEMIODirEntry **next);

/**
 * Close directory.
 *
 * @note Entries created using avio_em_read_dir() are not deleted and must be
 * freeded with avio_em_free_directory_entry().
 *
 * @param s         directory read context.
 * @return >=0 on success or negative on error.
 */
int avio_em_close_dir(AVEMIODirContext **s);

/**
 * Free entry allocated by avio_em_read_dir().
 *
 * @param entry entry to be freed.
 */
void avio_em_free_directory_entry(AVEMIODirEntry **entry);

/**
 * Allocate and initialize an AVEMIOContext for buffered I/O. It must be later
 * freed with av_em_free().
 *
 * @param buffer Memory block for input/output operations via AVEMIOContext.
 *        The buffer must be allocated with av_em_alloc() and friends.
 *        It may be freed and replaced with a new buffer by libavformat.
 *        AVEMIOContext.buffer holds the buffer currently in use,
 *        which must be later freed with av_em_free().
 * @param buffer_size The buffer size is very important for performance.
 *        For protocols with fixed blocksize it should be set to this blocksize.
 *        For others a typical size is a cache page, e.g. 4kb.
 * @param write_flag Set to 1 if the buffer should be writable, 0 otherwise.
 * @param opaque An opaque pointer to user-specific data.
 * @param read_packet  A function for refilling the buffer, may be NULL.
 * @param write_packet A function for writing the buffer contents, may be NULL.
 *        The function may not change the input buffers content.
 * @param seek A function for seeking to specified byte position, may be NULL.
 *
 * @return Allocated AVEMIOContext or NULL on failure.
 */
AVEMIOContext *avio_em_alloc_context(
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));

void avio_em_w8(AVEMIOContext *s, int b);
void avio_em_write(AVEMIOContext *s, const unsigned char *buf, int size);
void avio_em_wl64(AVEMIOContext *s, uint64_t val);
void avio_em_wb64(AVEMIOContext *s, uint64_t val);
void avio_em_wl32(AVEMIOContext *s, unsigned int val);
void avio_em_wb32(AVEMIOContext *s, unsigned int val);
void avio_em_wl24(AVEMIOContext *s, unsigned int val);
void avio_em_wb24(AVEMIOContext *s, unsigned int val);
void avio_em_wl16(AVEMIOContext *s, unsigned int val);
void avio_em_wb16(AVEMIOContext *s, unsigned int val);

/**
 * Write a NULL-terminated string.
 * @return number of bytes written.
 */
int avio_em_put_str(AVEMIOContext *s, const char *str);

/**
 * Convert an UTF-8 string to UTF-16LE and write it.
 * @param s the AVEMIOContext
 * @param str NULL-terminated UTF-8 string
 *
 * @return number of bytes written.
 */
int avio_em_put_str16le(AVEMIOContext *s, const char *str);

/**
 * Convert an UTF-8 string to UTF-16BE and write it.
 * @param s the AVEMIOContext
 * @param str NULL-terminated UTF-8 string
 *
 * @return number of bytes written.
 */
int avio_em_put_str16be(AVEMIOContext *s, const char *str);

/**
 * Mark the written bytestream as a specific type.
 *
 * Zero-length ranges are omitted from the output.
 *
 * @param time the stream time the current bytestream pos corresponds to
 *             (in AV_TIME_BASE units), or AV_NOPTS_VALUE if unknown or not
 *             applicable
 * @param type the kind of data written starting at the current pos
 */
void avio_em_write_marker(AVEMIOContext *s, int64_t time, enum AVEMIODataMarkerType type);

/**
 * ORing this as the "whence" parameter to a seek function causes it to
 * return the filesize without seeking anywhere. Supporting this is optional.
 * If it is not supported then the seek function will return <0.
 */
#define AVSEEK_SIZE 0x10000

/**
 * Passing this flag as the "whence" parameter to a seek function causes it to
 * seek by any means (like reopening and linear reading) or other normally unreasonable
 * means that can be extremely slow.
 * This may be ignored by the seek code.
 */
#define AVSEEK_FORCE 0x20000

/**
 * fseek() equivalent for AVEMIOContext.
 * @return new position or AVERROR.
 */
int64_t avio_em_seek(AVEMIOContext *s, int64_t offset, int whence);

/**
 * Skip given number of bytes forward
 * @return new position or AVERROR.
 */
int64_t avio_em_skip(AVEMIOContext *s, int64_t offset);

/**
 * ftell() equivalent for AVEMIOContext.
 * @return position or AVERROR.
 */
static av_always_inline int64_t avio_em_tell(AVEMIOContext *s)
{
    return avio_em_seek(s, 0, SEEK_CUR);
}

/**
 * Get the filesize.
 * @return filesize or AVERROR
 */
int64_t avio_em_size(AVEMIOContext *s);

/**
 * feof() equivalent for AVEMIOContext.
 * @return non zero if and only if end of file
 */
int avio_em_feof(AVEMIOContext *s);
#if FF_API_URL_FEOF
/**
 * @deprecated use avio_em_feof()
 */
attribute_deprecated
int url_em_feof(AVEMIOContext *s);
#endif

/** @warning Writes up to 4 KiB per call */
int avio_em_printf(AVEMIOContext *s, const char *fmt, ...) av_printf_format(2, 3);

/**
 * Force flushing of buffered data.
 *
 * For write streams, force the buffered data to be immediately written to the output,
 * without to wait to fill the internal buffer.
 *
 * For read streams, discard all currently buffered data, and advance the
 * reported file position to that of the underlying stream. This does not
 * read new data, and does not perform any seeks.
 */
void avio_em_flush(AVEMIOContext *s);

/**
 * Read size bytes from AVEMIOContext into buf.
 * @return number of bytes read or AVERROR
 */
int avio_em_read(AVEMIOContext *s, unsigned char *buf, int size);

/**
 * @name Functions for reading from AVEMIOContext
 * @{
 *
 * @note return 0 if EOF, so you cannot use it if EOF handling is
 *       necessary
 */
int          avio_em_r8  (AVEMIOContext *s);
unsigned int avio_em_rl16(AVEMIOContext *s);
unsigned int avio_em_rl24(AVEMIOContext *s);
unsigned int avio_em_rl32(AVEMIOContext *s);
uint64_t     avio_em_rl64(AVEMIOContext *s);
unsigned int avio_em_rb16(AVEMIOContext *s);
unsigned int avio_em_rb24(AVEMIOContext *s);
unsigned int avio_em_rb32(AVEMIOContext *s);
uint64_t     avio_em_rb64(AVEMIOContext *s);
/**
 * @}
 */

/**
 * Read a string from pb into buf. The reading will terminate when either
 * a NULL character was encountered, maxlen bytes have been read, or nothing
 * more can be read from pb. The result is guaranteed to be NULL-terminated, it
 * will be truncated if buf is too small.
 * Note that the string is not interpreted or validated in any way, it
 * might get truncated in the middle of a sequence for multi-byte encodings.
 *
 * @return number of bytes read (is always <= maxlen).
 * If reading ends on EOF or error, the return value will be one more than
 * bytes actually read.
 */
int avio_em_get_str(AVEMIOContext *pb, int maxlen, char *buf, int buflen);

/**
 * Read a UTF-16 string from pb and convert it to UTF-8.
 * The reading will terminate when either a null or invalid character was
 * encountered or maxlen bytes have been read.
 * @return number of bytes read (is always <= maxlen)
 */
int avio_em_get_str16le(AVEMIOContext *pb, int maxlen, char *buf, int buflen);
int avio_em_get_str16be(AVEMIOContext *pb, int maxlen, char *buf, int buflen);


/**
 * @name URL open modes
 * The flags argument to avio_em_open must be one of the following
 * constants, optionally ORed with other flags.
 * @{
 */
#define AVIO_FLAG_READ  1                                      /**< read-only */
#define AVIO_FLAG_WRITE 2                                      /**< write-only */
#define AVIO_FLAG_READ_WRITE (AVIO_FLAG_READ|AVIO_FLAG_WRITE)  /**< read-write pseudo flag */
/**
 * @}
 */

/**
 * Use non-blocking mode.
 * If this flag is set, operations on the context will return
 * AVERROR(EAGAIN) if they can not be performed immediately.
 * If this flag is not set, operations on the context will never return
 * AVERROR(EAGAIN).
 * Note that this flag does not affect the opening/connecting of the
 * context. Connecting a protocol will always block if necessary (e.g. on
 * network protocols) but never hang (e.g. on busy devices).
 * Warning: non-blocking protocols is work-in-progress; this flag may be
 * silently ignored.
 */
#define AVIO_FLAG_NONBLOCK 8

/**
 * Use direct mode.
 * avio_em_read and avio_em_write should if possible be satisfied directly
 * instead of going through a buffer, and avio_em_seek will always
 * call the underlying seek function directly.
 */
#define AVIO_FLAG_DIRECT 0x8000

/**
 * Create and initialize a AVEMIOContext for accessing the
 * resource indicated by url.
 * @note When the resource indicated by url has been opened in
 * read+write mode, the AVEMIOContext can be used only for writing.
 *
 * @param s Used to return the pointer to the created AVEMIOContext.
 * In case of failure the pointed to value is set to NULL.
 * @param url resource to access
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int avio_em_open(AVEMIOContext **s, const char *url, int flags);

/**
 * Create and initialize a AVEMIOContext for accessing the
 * resource indicated by url.
 * @note When the resource indicated by url has been opened in
 * read+write mode, the AVEMIOContext can be used only for writing.
 *
 * @param s Used to return the pointer to the created AVEMIOContext.
 * In case of failure the pointed to value is set to NULL.
 * @param url resource to access
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @param int_cb an interrupt callback to be used at the protocols level
 * @param options  A dictionary filled with protocol-private options. On return
 * this parameter will be destroyed and replaced with a dict containing options
 * that were not found. May be NULL.
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int avio_em_open2(AVEMIOContext **s, const char *url, int flags,
               const AVEMIOInterruptCB *int_cb, AVEMDictionary **options);

/**
 * Close the resource accessed by the AVEMIOContext s and free it.
 * This function can only be used if s was opened by avio_em_open().
 *
 * The internal buffer is automatically flushed before closing the
 * resource.
 *
 * @return 0 on success, an AVERROR < 0 on error.
 * @see avio_em_closep
 */
int avio_em_close(AVEMIOContext *s);

/**
 * Close the resource accessed by the AVEMIOContext *s, free it
 * and set the pointer pointing to it to NULL.
 * This function can only be used if s was opened by avio_em_open().
 *
 * The internal buffer is automatically flushed before closing the
 * resource.
 *
 * @return 0 on success, an AVERROR < 0 on error.
 * @see avio_em_close
 */
int avio_em_closep(AVEMIOContext **s);


/**
 * Open a write only memory stream.
 *
 * @param s new IO context
 * @return zero if no error.
 */
int avio_em_open_dyn_buf(AVEMIOContext **s);

/**
 * Return the written size and a pointer to the buffer. The buffer
 * must be freed with av_em_free().
 * Padding of AV_INPUT_BUFFER_PADDING_SIZE is added to the buffer.
 *
 * @param s IO context
 * @param pbuffer pointer to a byte buffer
 * @return the length of the byte buffer
 */
int avio_em_close_dyn_buf(AVEMIOContext *s, uint8_t **pbuffer);

/**
 * Iterate through names of available protocols.
 *
 * @param opaque A private pointer representing current protocol.
 *        It must be a pointer to NULL on first iteration and will
 *        be updated by successive calls to avio_em_enum_protocols.
 * @param output If set to 1, iterate over output protocols,
 *               otherwise over input protocols.
 *
 * @return A static string containing the name of current protocol or NULL
 */
const char *avio_em_enum_protocols(void **opaque, int output);

/**
 * Pause and resume playing - only meaningful if using a network streaming
 * protocol (e.g. MMS).
 *
 * @param h     IO context from which to call the read_pause function pointer
 * @param pause 1 for pause, 0 for resume
 */
int     avio_em_pause(AVEMIOContext *h, int pause);

/**
 * Seek to a given timestamp relative to some component stream.
 * Only meaningful if using a network streaming protocol (e.g. MMS.).
 *
 * @param h IO context from which to call the seek function pointers
 * @param stream_index The stream index that the timestamp is relative to.
 *        If stream_index is (-1) the timestamp should be in AV_TIME_BASE
 *        units from the beginning of the presentation.
 *        If a stream_index >= 0 is used and the protocol does not support
 *        seeking based on component streams, the call will fail.
 * @param timestamp timestamp in AVEMStream.time_base units
 *        or if there is no stream specified then in AV_TIME_BASE units.
 * @param flags Optional combination of AVSEEK_FLAG_BACKWARD, AVSEEK_FLAG_BYTE
 *        and AVSEEK_FLAG_ANY. The protocol may silently ignore
 *        AVSEEK_FLAG_BACKWARD and AVSEEK_FLAG_ANY, but AVSEEK_FLAG_BYTE will
 *        fail if used and not supported.
 * @return >= 0 on success
 * @see AVEMInputFormat::read_seek
 */
int64_t avio_em_seek_time(AVEMIOContext *h, int stream_index,
                       int64_t timestamp, int flags);

/* Avoid a warning. The header can not be included because it breaks c++. */
struct AVEMBPrint;

/**
 * Read contents of h into print buffer, up to max_size bytes, or up to EOF.
 *
 * @return 0 for success (max_size bytes read or EOF reached), negative error
 * code otherwise
 */
int avio_em_read_to_bprint(AVEMIOContext *h, struct AVEMBPrint *pb, size_t max_size);

/**
 * Accept and allocate a client context on a server context.
 * @param  s the server context
 * @param  c the client context, must be unallocated
 * @return   >= 0 on success or a negative value corresponding
 *           to an AVERROR on failure
 */
int avio_em_accept(AVEMIOContext *s, AVEMIOContext **c);

/**
 * Perform one step of the protocol handshake to accept a new client.
 * This function must be called on a client returned by avio_em_accept() before
 * using it as a read/write context.
 * It is separate from avio_em_accept() because it may block.
 * A step of the handshake is defined by places where the application may
 * decide to change the proceedings.
 * For example, on a protocol with a request header and a reply header, each
 * one can constitute a step because the application may use the parameters
 * from the request to change parameters in the reply; or each individual
 * chunk of the request can constitute a step.
 * If the handshake is already finished, avio_em_handshake() does nothing and
 * returns 0 immediately.
 *
 * @param  c the client context to perform the handshake on
 * @return   0   on a complete and successful handshake
 *           > 0 if the handshake progressed, but is not complete
 *           < 0 for an AVERROR code
 */
int avio_em_handshake(AVEMIOContext *c);

int64_t avio_em_fast_seek_begin(AVEMIOContext *s);

#endif /* AVFORMAT_AVIO_H */
