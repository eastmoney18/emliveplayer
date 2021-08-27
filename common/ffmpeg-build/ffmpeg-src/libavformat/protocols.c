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

#include "config.h"

#include "libavutil/avstring.h"
#include "libavutil/mem.h"

#include "url.h"

extern const EMURLProtocol em_async_protocol;
extern const EMURLProtocol em_bluray_protocol;
extern const EMURLProtocol em_cache_protocol;
extern const EMURLProtocol em_concat_protocol;
extern const EMURLProtocol em_crypto_protocol;
extern const EMURLProtocol em_data_protocol;
extern const EMURLProtocol em_ffrtmpcrypt_protocol;
extern const EMURLProtocol em_ffrtmphttp_protocol;
extern const EMURLProtocol em_file_protocol;
extern const EMURLProtocol em_ftp_protocol;
extern const EMURLProtocol em_gopher_protocol;
extern const EMURLProtocol em_hls_protocol;
extern const EMURLProtocol em_http_protocol;
extern const EMURLProtocol em_http_proxy_protocol;
extern const EMURLProtocol em_http_s_protocol;
extern const EMURLProtocol em_icecast_protocol;
extern const EMURLProtocol em_ijkhttphook_protocol;
extern const EMURLProtocol em_ijklongurl_protocol;
extern const EMURLProtocol em_ijkmediadatasource_protocol;
extern const EMURLProtocol em_ijksegment_protocol;
extern const EMURLProtocol em_ijktcphook_protocol;
extern const EMURLProtocol em_mmsh_protocol;
extern const EMURLProtocol em_mmst_protocol;
extern const EMURLProtocol em_md5_protocol;
extern const EMURLProtocol em_pipe_protocol;
extern const EMURLProtocol em_rtmp_protocol;
extern const EMURLProtocol em_rtmpe_protocol;
extern const EMURLProtocol em_rtmps_protocol;
extern const EMURLProtocol em_rtmpt_protocol;
extern const EMURLProtocol em_rtmpte_protocol;
extern const EMURLProtocol em_rtmpts_protocol;
extern const EMURLProtocol em_rtp_protocol;
extern const EMURLProtocol em_sctp_protocol;
extern const EMURLProtocol em_srtp_protocol;
extern const EMURLProtocol em_subfile_protocol;
extern const EMURLProtocol em_tcp_protocol;
extern const EMURLProtocol em_tls_gnutls_protocol;
extern const EMURLProtocol em_tls_schannel_protocol;
extern const EMURLProtocol em_tls_securetransport_protocol;
extern const EMURLProtocol em_tls_openssl_protocol;
extern const EMURLProtocol em_udp_protocol;
extern const EMURLProtocol em_udplite_protocol;
extern const EMURLProtocol em_unix_protocol;
extern const EMURLProtocol em_librtmp_protocol;
extern const EMURLProtocol em_librtmpe_protocol;
extern const EMURLProtocol em_librtmps_protocol;
extern const EMURLProtocol em_librtmpt_protocol;
extern const EMURLProtocol em_librtmpte_protocol;
extern const EMURLProtocol em_libssh_protocol;
extern const EMURLProtocol em_libsmbclient_protocol;
extern const EMURLProtocol em_emmul_protocol;

#include "libavformat/protocol_list.c"

const AVEMClass *em_urlcontext_child_class_next(const AVEMClass *prev)
{
    int i;

    /* find the protocol that corresponds to prev */
    for (i = 0; prev && url_protocols[i]; i++) {
        if (url_protocols[i]->priv_data_class == prev) {
            i++;
            break;
        }
    }

    /* find next protocol with priv options */
    for (; url_protocols[i]; i++)
        if (url_protocols[i]->priv_data_class)
            return url_protocols[i]->priv_data_class;
    return NULL;
}


const char *avio_em_enum_protocols(void **opaque, int output)
{
    const EMURLProtocol **p = *opaque;

    p = p ? p + 1 : url_protocols;
    *opaque = p;
    if (!*p) {
        *opaque = NULL;
        return NULL;
    }
    if ((output && (*p)->url_write) || (!output && (*p)->url_read))
        return (*p)->name;
    return avio_em_enum_protocols(opaque, output);
}

const EMURLProtocol **ffurl_em_get_protocols(const char *whitelist,
                                        const char *blacklist)
{
    const EMURLProtocol **ret;
    int i, ret_idx = 0;

    ret = av_em_mallocz_array(FF_ARRAY_ELEMS(url_protocols), sizeof(*ret));
    if (!ret)
        return NULL;

    for (i = 0; url_protocols[i]; i++) {
        const EMURLProtocol *up = url_protocols[i];

        if (whitelist && *whitelist && !av_em_match_name(up->name, whitelist))
            continue;
        if (blacklist && *blacklist && av_em_match_name(up->name, blacklist))
            continue;

        ret[ret_idx++] = up;
    }

    return ret;
}
