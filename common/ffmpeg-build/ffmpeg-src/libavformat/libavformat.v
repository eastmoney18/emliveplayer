LIBAVFORMAT_$MAJOR {
        global: av*;
                #FIXME those are for ffserver
                em_inet_aton;
                em_socket_nonblock;
                ff_rtsp_parse_line;
                ff_rtp_get_local_rtp_port;
                ff_rtp_get_local_rtcp_port;
                emio_open_dyn_packet_buf;
                emio_set_buf_size;
                ffurl_em_close;
                ffurl_em_open;
                ffurl_em_write;
                #those are deprecated, remove on next bump
                url_em_feof;
        local: *;
};
