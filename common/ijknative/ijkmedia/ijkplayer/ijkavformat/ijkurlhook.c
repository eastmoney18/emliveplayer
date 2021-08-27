/*
 * Copyright (c) 2015 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "libavutil/application.h"

typedef struct Context {
    AVEMClass        *class;
    EMURLContext     *inner;

    int64_t         logical_pos;
    int64_t         logical_size;
    int             io_error;

    AVAppIOControl  app_io_ctrl;
    const char     *scheme;
    const char     *inner_scheme;

    /* options */
    int             inner_flags;
    AVEMDictionary   *inner_options;
    int             segment_index;
    int64_t         test_fail_point;
    int64_t         test_fail_point_next;
    int64_t         app_ctx_intptr;
    
    AVApplicationContext *app_ctx;
} Context;

static int ijkurlhook_call_inject(EMURLContext *h)
{
    Context *c = h->priv_data;
    int ret = 0;

    if (em_check_interrupt(&h->interrupt_callback)) {
        ret = AVERROR_EXIT;
        goto fail;
    }

    if (c->app_ctx) {
        AVAppIOControl control_data_backup = c->app_io_ctrl;

        c->app_io_ctrl.is_handled = 0;
        c->app_io_ctrl.is_url_changed = 0;
        ret = av_em_application_on_io_control(c->app_ctx, AVAPP_CTRL_WILL_HTTP_OPEN, &c->app_io_ctrl);
        if (ret || !c->app_io_ctrl.url[0]) {
            ret = AVERROR_EXIT;
            goto fail;
        }
        if (!c->app_io_ctrl.is_url_changed && strcmp(control_data_backup.url, c->app_io_ctrl.url)) {
            // force a url compare
            c->app_io_ctrl.is_url_changed = 1;
        }

        av_em_log(h, AV_LOG_INFO, "%s %s (%s)\n", h->prot->name, c->app_io_ctrl.url, c->app_io_ctrl.is_url_changed ? "changed" : "remain");
    }

    if (em_check_interrupt(&h->interrupt_callback)) {
        ret = AVERROR_EXIT;
        av_em_log(h, AV_LOG_ERROR, "%s %s (%s)\n", h->prot->name, c->app_io_ctrl.url, c->app_io_ctrl.is_url_changed ? "changed" : "remain");
        goto fail;
    }

fail:
    return ret;
}

static int ijkurlhook_reconnect(EMURLContext *h, AVEMDictionary *extra)
{
    Context *c = h->priv_data;
    int ret = 0;
    EMURLContext *new_url = NULL;
    AVEMDictionary *inner_options = NULL;

    c->test_fail_point_next += c->test_fail_point;

    assert(c->inner_options);
    av_em_dict_copy(&inner_options, c->inner_options, 0);
    if (extra)
        av_em_dict_copy(&inner_options, extra, 0);

    ret = ffurl_em_open_whitelist(&new_url,
                               c->app_io_ctrl.url,
                               c->inner_flags,
                               &h->interrupt_callback,
                               &inner_options,
                               h->protocol_whitelist,
                               h->protocol_blacklist,
                               h);
    if (ret)
        goto fail;

    ffurl_em_closep(&c->inner);

    c->inner        = new_url;
    h->is_streamed  = c->inner->is_streamed;
    c->logical_pos  = ffurl_em_seek(c->inner, 0, SEEK_CUR);
    if (c->inner->is_streamed)
        c->logical_size = -1;
    else
        c->logical_size = ffurl_em_seek(c->inner, 0, AVSEEK_SIZE);

    c->io_error = 0;
fail:
    av_em_dict_free(&inner_options);
    return ret;
}

static int ijkurlhook_init(EMURLContext *h, const char *arg, int flags, AVEMDictionary **options)
{
    Context *c = h->priv_data;
    int ret = 0;

    av_em_strstart(arg, c->scheme, &arg);

    c->inner_flags = flags;

    if (options)
        av_em_dict_copy(&c->inner_options, *options, 0);

    av_em_dict_set_int(&c->inner_options, "ijkapplication", c->app_ctx_intptr, 0);
    av_em_dict_set_int(&c->inner_options, "ijkinject-segment-index", c->segment_index, 0);

    c->app_io_ctrl.size = sizeof(c->app_io_ctrl);
    c->app_io_ctrl.segment_index = c->segment_index;
    c->app_io_ctrl.retry_counter = 0;

    if (av_em_strstart(arg, c->inner_scheme, NULL)) {
        snprintf(c->app_io_ctrl.url, sizeof(c->app_io_ctrl.url), "%s", arg);
    } else {
        snprintf(c->app_io_ctrl.url, sizeof(c->app_io_ctrl.url), "%s%s", c->inner_scheme, arg);
    }

    return ret;
}

static int ijktcphook_open(EMURLContext *h, const char *arg, int flags, AVEMDictionary **options)
{
    Context *c = h->priv_data;
    int ret = 0;

    c->app_ctx = (AVApplicationContext *)(intptr_t)c->app_ctx_intptr;
    c->scheme = "ijktcphook:";
    c->inner_scheme = "tcp:";
    ret = ijkurlhook_init(h, arg, flags, options);
    if (ret)
        goto fail;

    ret = ijkurlhook_reconnect(h, NULL);
    if (ret)
        goto fail;
    
fail:
    return ret;
}

static int ijkurlhook_close(EMURLContext *h)
{
    Context *c = h->priv_data;
    c->inner->flags = h->flags;
    av_em_dict_free(&c->inner_options);
    return ffurl_em_closep(&c->inner);
}

static int ijkurlhook_read(EMURLContext *h, unsigned char *buf, int size)
{
    Context *c = h->priv_data;
    c->inner->flags = h->flags;
    int ret = 0;
    if (c->io_error < 0)
        return c->io_error;

    if (c->test_fail_point_next > 0 && c->logical_pos >= c->test_fail_point_next) {
        av_em_log(h, AV_LOG_ERROR, "test fail point:%"PRId64"\n", c->test_fail_point_next);
        c->io_error = AVERROR(EIO);
        return AVERROR(EIO);
    }
    ret = ffurl_em_read(c->inner, buf, size);
    if (ret > 0){
        c->logical_pos += ret;
        av_em_application_did_io_tcp_read(c->app_ctx, (void *)h, ret);
    }
    else if (ret == AVERROR(EAGAIN) || ret == AVERROR(ETIMEDOUT)) {
        c->io_error = ret;
    }
    return ret;
}

static int ijkurlhook_write(EMURLContext *h, const unsigned char *buf, int size)
{
    Context *c = h->priv_data;
    c->inner->flags = h->flags;
    return ffurl_em_write(c->inner, buf, size);
}

static int64_t ijkurlhook_seek(EMURLContext *h, int64_t pos, int whence)
{
    printf("=======================\n==========seek file, pos:%lld==============\n", pos);
    Context *c = h->priv_data;
    int64_t seek_ret = 0;

    seek_ret = ffurl_em_seek(c->inner, pos, whence);
    if (seek_ret < 0) {
        c->io_error = (int)seek_ret;
        return seek_ret;
    }

    c->logical_pos = seek_ret;
    if (c->test_fail_point)
        c->test_fail_point_next = c->logical_pos + c->test_fail_point;

    c->io_error = 0;
    return seek_ret;
}

static int ijkhttphook_reconnect_at(EMURLContext *h, int64_t offset)
{
    int           ret        = 0;
    AVEMDictionary *extra_opts = NULL;

    av_em_dict_set_int(&extra_opts, "offset", offset, 0);
    ret = ijkurlhook_reconnect(h, extra_opts);
    av_em_dict_free(&extra_opts);
    return ret;
}

static int ijkhttphook_open(EMURLContext *h, const char *arg, int flags, AVEMDictionary **options)
{
    Context *c = h->priv_data;
    int ret = 0;

    c->app_ctx = (AVApplicationContext *)(intptr_t)c->app_ctx_intptr;
    c->scheme = "ijkhttphook:";
    c->inner_scheme = "http:";

    ret = ijkurlhook_init(h, arg, flags, options);
    if (ret)
        goto fail;

    ret = ijkurlhook_call_inject(h);
    if (ret)
        goto fail;

    ret = ijkurlhook_reconnect(h, NULL);
    while (ret) {
        int inject_ret = 0;

        switch (ret) {
            case AVERROR_EXIT:
                goto fail;
        }

        c->app_io_ctrl.retry_counter++;
        inject_ret = ijkurlhook_call_inject(h);
        if (inject_ret) {
            ret = AVERROR_EXIT;
            goto fail;
        }

        if (!c->app_io_ctrl.is_handled)
            goto fail;

        av_em_log(h, AV_LOG_INFO, "%s: will reconnect at start\n", __func__);
        ret = ijkurlhook_reconnect(h, NULL);
        av_em_log(h, AV_LOG_INFO, "%s: did reconnect at start: %d\n", __func__, ret);
        if (ret)
            c->app_io_ctrl.retry_counter++;
    }

fail:
    return ret;
}

static int ijkhttphook_read(EMURLContext *h, unsigned char *buf, int size)
{
    Context *c = h->priv_data;
    int ret = 0;
    int read_ret = 0;

    c->app_io_ctrl.retry_counter = 0;

    read_ret = ijkurlhook_read(h, buf, size);
    while (read_ret < 0 && !h->is_streamed && c->logical_pos < c->logical_size) {
        switch (read_ret) {
            case AVERROR_EXIT:
                goto fail;
        }

        c->app_io_ctrl.retry_counter++;
        ret = ijkurlhook_call_inject(h);
        if (ret)
            goto fail;

        if (!c->app_io_ctrl.is_handled)
            goto fail;

        av_em_log(h, AV_LOG_INFO, "%s: will reconnect(%d) at %"PRId64"\n", __func__, c->app_io_ctrl.retry_counter, c->logical_pos);
        read_ret = ijkhttphook_reconnect_at(h, c->logical_pos);
        av_em_log(h, AV_LOG_INFO, "%s: did reconnect(%d) at %"PRId64": %d\n", __func__, c->app_io_ctrl.retry_counter, c->logical_pos, read_ret);
        if (ret < 0)
            continue;

        read_ret = ijkurlhook_read(h, buf, size);
    }

fail:
    if (read_ret <= 0) {
        c->io_error = read_ret;
    }
    return read_ret;
}

static int64_t ijkhttphook_reseek_at(EMURLContext *h, int64_t pos, int whence, int force_reconnect)
{
    Context *c = h->priv_data;
    int ret = 0;

    if (!force_reconnect)
        return ijkurlhook_seek(h, pos, whence);

    if (whence == SEEK_CUR)
        pos += c->logical_pos;
    else if (whence == SEEK_END)
        pos += c->logical_size;
    else if (whence != SEEK_SET)
        return AVERROR(EINVAL);
    if (pos < 0)
        return AVERROR(EINVAL);

    ret = ijkhttphook_reconnect_at(h, pos);
    if (ret) {
        c->io_error = ret;
        return ret;
    }

    c->io_error = 0;
    return c->logical_pos;
}

static int64_t ijkhttphook_seek(EMURLContext *h, int64_t pos, int whence)
{
    Context *c = h->priv_data;
    int     ret      = 0;
    int64_t seek_ret = -1;

    if (whence == AVSEEK_SIZE)
        return c->logical_size;
    else if ((whence == SEEK_CUR && pos == 0) ||
             (whence == SEEK_SET && pos == c->logical_pos))
        return c->logical_pos;
    else if ((c->logical_size < 0 && whence == SEEK_END) || h->is_streamed)
        return AVERROR(ENOSYS);
    c->app_io_ctrl.retry_counter = 0;
    ret = ijkurlhook_call_inject(h);
    if (ret) {
        ret = AVERROR_EXIT;
        goto fail;
    }

    seek_ret = ijkhttphook_reseek_at(h, pos, whence, c->app_io_ctrl.is_url_changed);
    while (seek_ret < 0) {
        switch (seek_ret) {
            case AVERROR_EXIT:
            case AVERROR_EOF:
                goto fail;
        }

        c->app_io_ctrl.retry_counter++;
        ret = ijkurlhook_call_inject(h);
        if (ret) {
            ret = AVERROR_EXIT;
            goto fail;
        }

        if (!c->app_io_ctrl.is_handled)
            goto fail;

        av_em_log(h, AV_LOG_INFO, "%s: will reseek(%d) at pos=%"PRId64", whence=%d\n", __func__, c->app_io_ctrl.retry_counter, pos, whence);
        seek_ret = ijkhttphook_reseek_at(h, pos, whence, c->app_io_ctrl.is_url_changed);
        av_em_log(h, AV_LOG_INFO, "%s: did reseek(%d) at pos=%"PRId64", whence=%d: %"PRId64"\n", __func__, c->app_io_ctrl.retry_counter, pos, whence, seek_ret);
    }

    if (c->test_fail_point)
        c->test_fail_point_next = c->logical_pos + c->test_fail_point;
    c->io_error = 0;
    return c->logical_pos;
fail:
    return ret;
}

#define OFFSET(x) offsetof(Context, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption ijktcphook_options[] = {
    { "ijktcphook-test-fail-point",     "test fail point, in bytes",
        OFFSET(test_fail_point),        AV_OPT_TYPE_INT,   {.i64 = 0}, 0,         INT_MAX, D },
    { "ijkapplication", "AVApplicationContext", OFFSET(app_ctx_intptr), AV_OPT_TYPE_INT64, { .i64 = 0 }, INT64_MIN, INT64_MAX, .flags = D },

    { NULL }
};

static const AVOption ijkhttphook_options[] = {
    { "ijkinject-segment-index",        "segment index of current url",
        OFFSET(segment_index),          AV_OPT_TYPE_INT,   {.i64 = 0}, 0,         INT_MAX, D },
    { "ijkhttphook-test-fail-point",    "test fail point, in bytes",
        OFFSET(test_fail_point),        AV_OPT_TYPE_INT,   {.i64 = 0}, 0,         INT_MAX, D },
    { "ijkapplication", "AVApplicationContext", OFFSET(app_ctx_intptr), AV_OPT_TYPE_INT64, { .i64 = 0 }, INT64_MIN, INT64_MAX, .flags = D },

    { NULL }
};

#undef D
#undef OFFSET

static const AVEMClass ijktcphook_context_class = {
    .class_name = "TcpHook",
    .item_name  = av_em_default_item_name,
    .option     = ijktcphook_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

EMURLProtocol ijkem_ijktcphook_protocol = {
    .name                = "ijktcphook",
    .url_open2           = ijktcphook_open,
    .url_read            = ijkurlhook_read,
    .url_write           = ijkurlhook_write,
    .url_close           = ijkurlhook_close,
    .priv_data_size      = sizeof(Context),
    .priv_data_class     = &ijktcphook_context_class,
};

static const AVEMClass ijkhttphook_context_class = {
    .class_name = "HttpHook",
    .item_name  = av_em_default_item_name,
    .option     = ijkhttphook_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

EMURLProtocol ijkem_ijkhttphook_protocol = {
    .name                = "ijkhttphook",
    .url_open2           = ijkhttphook_open,
    .url_read            = ijkhttphook_read,
    .url_write           = ijkurlhook_write,
    .url_seek            = ijkhttphook_seek,
    .url_close           = ijkurlhook_close,
    .priv_data_size      = sizeof(Context),
    .priv_data_class     = &ijkhttphook_context_class,
};


/*
static const AVEMClass emtcp_context_class= {
    .class_name          = "emtcp",
    .item_name           = av_em_default_item_name,
    .option              = NULL,
    .version             = LIBAVUTIL_VERSION_INT,
};

EMURLProtocol ff_emtcp_protocol = {
    
};
 */

