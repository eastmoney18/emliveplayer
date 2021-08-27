//
// Created by 陈海东 on 17/11/16.
//

#include "url.h"


static int emmul_url_open(EMURLContext *h, const char *url, int flags) {
    return 0;
}

static int emmul_url_read(EMURLContext *h, unsigned char *buf, int size) {
    return size;
}

static int emmul_url_close(EMURLContext *h) {
    return 0;
}

const EMURLProtocol em_emmul_protocol = {
        .name = "emmul",
        .url_open = emmul_url_open,
        .url_read = emmul_url_read,
        .url_close = emmul_url_close,
        .flags = URL_PROTOCOL_FLAG_NESTED_SCHEME,
        .priv_data_size = 0,
};