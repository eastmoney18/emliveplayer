/*
 * TCP protocol
 * Copyright (c) 2002 Fabrice Bellard
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
#include "avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/application.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#if HAVE_POLL_H
#include <poll.h>
#endif
#if HAVE_PTHREADS
#include <pthread.h>
#endif

typedef struct TCPContext {
    const AVEMClass *class;
    int fd;
    int listen;
    int open_timeout;
    int rw_timeout;
    int listen_timeout;
    int recv_buffer_size;
    int send_buffer_size;
//    int64_t app_ctx_intptr;
    char * app_ctx_intptr;
    int ipv6_port_workaround;
    int addrinfo_one_by_one;
    int addrinfo_timeout;

    AVApplicationContext *app_ctx;
    int dns_timeout;
    int dns_cache_count;
} TCPContext;



#define OFFSET(x) offsetof(TCPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "listen",          "Listen for incoming connections",  OFFSET(listen),         AV_OPT_TYPE_INT, { .i64 = 0 },     0,       2,       .flags = D|E },
    { "timeout",     "set timeout (in microseconds) of socket I/O operations", OFFSET(rw_timeout),     AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "listen_timeout",  "Connection awaiting timeout (in milliseconds)",      OFFSET(listen_timeout), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "send_buffer_size", "Socket send buffer size (in bytes)",                OFFSET(send_buffer_size), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
    { "recv_buffer_size", "Socket receive buffer size (in bytes)",             OFFSET(recv_buffer_size), AV_OPT_TYPE_INT, { .i64 = -1 },         -1, INT_MAX, .flags = D|E },
//    { "ijkapplication",   "AVApplicationContext",                              OFFSET(app_ctx_intptr),   AV_OPT_TYPE_INT64, { .i64 = 0 }, INT64_MIN, INT64_MAX, .flags = D },
    { "ijkapplication",   "AVApplicationContext",                              OFFSET(app_ctx_intptr),   AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D },
    { "ipv6_port_workaround",  "reset port in parsing addrinfo under ipv6",    OFFSET(ipv6_port_workaround), AV_OPT_TYPE_INT, { .i64 = 0 },         0, 1, .flags = D|E },
    { "addrinfo_one_by_one",  "parse addrinfo one by one in getaddrinfo()",    OFFSET(addrinfo_one_by_one), AV_OPT_TYPE_INT, { .i64 = 0 },         0, 1, .flags = D|E },
    { "addrinfo_timeout", "set timeout (in microseconds) for getaddrinfo()",   OFFSET(addrinfo_timeout), AV_OPT_TYPE_INT, { .i64 = -1 },       -1, INT_MAX, .flags = D|E },
    { "dns_timeout" , "set timeout (in seconds) for dns valid time",           OFFSET(dns_timeout), AV_OPT_TYPE_INT, { .i64 = 3000 }, 0, INT_MAX, .flags = D|E},
    { "dns_cache_count" , "set count for dns cache size",                      OFFSET(dns_cache_count), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 10000, .flags = D|E},
    { "rw_timeout" , "set timeout (in microseconds) of socket I/O operations", OFFSET(rw_timeout), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, .flags = D|E},
    { NULL }
};

static const AVEMClass tcp_class = {
    .class_name = "tcp",
    .item_name  = av_em_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

int ijk_tcp_getaddrinfo_nonblock(const char *hostname, const char *servname,
                                 const struct addrinfo *hints, struct addrinfo **res,
                                 int64_t timeout,
                                 const AVEMIOInterruptCB *int_cb, int one_by_one);
#ifdef HAVE_PTHREADS

typedef struct TCPAddrinfoRequest
{
    AVEMBufferRef *buffer;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    AVEMIOInterruptCB interrupt_callback;

    char            *hostname;
    char            *servname;
    struct addrinfo  hints;
    struct addrinfo *res;

    volatile int     finished;
    int              last_error;
} TCPAddrinfoRequest;

static void tcp_getaddrinfo_request_free(TCPAddrinfoRequest *req)
{
    av_assert0(req);
    if (req->res) {
        freeaddrinfo(req->res);
        req->res = NULL;
    }

    av_em_freep(&req->servname);
    av_em_freep(&req->hostname);
    pthread_cond_destroy(&req->cond);
    pthread_mutex_destroy(&req->mutex);
    av_em_freep(&req);
}

static void tcp_getaddrinfo_request_free_buffer(void *opaque, uint8_t *data)
{
    av_assert0(opaque);
    TCPAddrinfoRequest *req = (TCPAddrinfoRequest *)opaque;
    tcp_getaddrinfo_request_free(req);
}

static int tcp_getaddrinfo_request_create(TCPAddrinfoRequest **request,
                                          const char *hostname,
                                          const char *servname,
                                          const struct addrinfo *hints,
                                          const AVEMIOInterruptCB *int_cb)
{
    TCPAddrinfoRequest *req = (TCPAddrinfoRequest *) av_em_mallocz(sizeof(TCPAddrinfoRequest));
    if (!req)
        return AVERROR(ENOMEM);

    if (pthread_mutex_init(&req->mutex, NULL)) {
        av_em_freep(&req);
        return AVERROR(ENOMEM);
    }

    if (pthread_cond_init(&req->cond, NULL)) {
        pthread_mutex_destroy(&req->mutex);
        av_em_freep(&req);
        return AVERROR(ENOMEM);
    }

    if (int_cb)
        req->interrupt_callback = *int_cb;

    if (hostname) {
        req->hostname = av_em_strdup(hostname);
        if (!req->hostname)
            goto fail;
    }

    if (servname) {
        req->servname = av_em_strdup(servname);
        if (!req->hostname)
            goto fail;
    }

    if (hints) {
        req->hints.ai_family   = hints->ai_family;
        req->hints.ai_socktype = hints->ai_socktype;
        req->hints.ai_protocol = hints->ai_protocol;
        req->hints.ai_flags    = hints->ai_flags;
    }

    req->buffer = av_em_buffer_create(NULL, 0, tcp_getaddrinfo_request_free_buffer, req, 0);
    if (!req->buffer)
        goto fail;

    *request = req;
    return 0;
fail:
    tcp_getaddrinfo_request_free(req);
    return AVERROR(ENOMEM);
}

static void *tcp_getaddrinfo_worker(void *arg)
{
    TCPAddrinfoRequest *req = arg;

    getaddrinfo(req->hostname, req->servname, &req->hints, &req->res);
    pthread_mutex_lock(&req->mutex);
    req->finished = 1;
    pthread_cond_signal(&req->cond);
    pthread_mutex_unlock(&req->mutex);
    av_em_buffer_unref(&req->buffer);
    return NULL;
}

static void *tcp_getaddrinfo_one_by_one_worker(void *arg)
{
    struct addrinfo *temp_addrinfo = NULL;
    struct addrinfo *cur = NULL;
    int ret = EAI_FAIL;
    int i = 0;
    int option_length = 0;

    TCPAddrinfoRequest *req = (TCPAddrinfoRequest *)arg;

    int family_option[2] = {AF_INET, AF_INET6};

    option_length = sizeof(family_option) / sizeof(family_option[0]);

    for (; i < option_length; ++i) {
        struct addrinfo *hint = &req->hints;
        hint->ai_family = family_option[i];
        ret = getaddrinfo(req->hostname, req->servname, hint, &temp_addrinfo);
        if (ret) {
            req->last_error = ret;
            continue;
        }
        pthread_mutex_lock(&req->mutex);
        if (!req->res) {
            req->res = temp_addrinfo;
        } else {
            cur = req->res;
            while (cur->ai_next)
                cur = cur->ai_next;
            cur->ai_next = temp_addrinfo;
        }
        pthread_mutex_unlock(&req->mutex);
    }
    pthread_mutex_lock(&req->mutex);
    req->finished = 1;
    pthread_cond_signal(&req->cond);
    pthread_mutex_unlock(&req->mutex);
    av_em_buffer_unref(&req->buffer);
    return NULL;
}

int ijk_tcp_getaddrinfo_nonblock(const char *hostname, const char *servname,
                                 const struct addrinfo *hints, struct addrinfo **res,
                                 int64_t timeout,
                                 const AVEMIOInterruptCB *int_cb, int one_by_one)
{
    int     ret;
    int64_t start;
    int64_t now;
    AVEMBufferRef        *req_ref = NULL;
    TCPAddrinfoRequest *req     = NULL;
    pthread_t work_thread;

    if (hostname && !hostname[0])
        hostname = NULL;

    if (timeout <= 0)
        return getaddrinfo(hostname, servname, hints, res);

    ret = tcp_getaddrinfo_request_create(&req, hostname, servname, hints, int_cb);
    if (ret)
        goto fail;

    req_ref = av_em_buffer_ref(req->buffer);
    if (req_ref == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* FIXME: using a thread pool would be better. */
    if (one_by_one)
        ret = pthread_create(&work_thread, NULL, tcp_getaddrinfo_one_by_one_worker, req);
    else
        ret = pthread_create(&work_thread, NULL, tcp_getaddrinfo_worker, req);

    if (ret) {
        ret = AVERROR(ret);
        goto fail;
    }

    pthread_detach(work_thread);

    start = av_em_gettime();
    now   = start;

    pthread_mutex_lock(&req->mutex);
    while (1) {
        int64_t wait_time = now + 100000;
        struct timespec tv = { .tv_sec  =  wait_time / 1000000,
                               .tv_nsec = (wait_time % 1000000) * 1000 };

        if (req->finished || (start + timeout < now)) {
            if (req->res) {
                ret = 0;
                *res = req->res;
                req->res = NULL;
            } else {
                ret = req->last_error ? req->last_error : AVERROR_EXIT;
            }
            break;
        }
#if defined(__ANDROID__) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
        ret = pthread_cond_timedwait_monotonic_np(&req->cond, &req->mutex, &tv);
#else
        ret = pthread_cond_timedwait(&req->cond, &req->mutex, &tv);
#endif
        if (ret != 0 && ret != ETIMEDOUT) {
            av_em_log(NULL, AV_LOG_ERROR, "pthread_cond_timedwait failed: %d\n", ret);
            ret = AVERROR_EXIT;
            break;
        }

        if (em_check_interrupt(&req->interrupt_callback)) {
            ret = AVERROR_EXIT;
            break;
        }

        now = av_em_gettime();
        av_em_log(NULL, AV_LOG_ERROR, "tcp dns cost time is %lld:", now - start);
    }
    pthread_mutex_unlock(&req->mutex);
fail:
    av_em_buffer_unref(&req_ref);
    return ret;
}

#else
int ijk_tcp_getaddrinfo_nonblock(const char *hostname, const char *servname,
                                 const struct addrinfo *hints, struct addrinfo **res,
                                 int64_t timeout,
                                 const AVEMIOInterruptCB *int_cb)
{
    return getaddrinfo(hostname, servname, hints, res);
}
#endif

typedef struct EMDNS{
    struct addrinfo *ai;
    int64_t time;
}EMDNS;

static AVEMDictionary *s_dns_dict = NULL;
static int s_dns_cache_count = -1;
static int s_dns_timeout = -1;
static int em_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints,
                          struct addrinfo **res, 
                          int *freeai)
{
    EMDNS *dns = NULL;
    AVEMDictionaryEntry *entry = NULL;
    int64_t now = av_em_gettime() / (1000 * 1000); //microseconds->senconds
    *freeai = 0;
    char dict_key[2048];
    memset(dict_key, sizeof(dict_key), 0);
    sprintf(dict_key, "%s:%s", hostname, servname);
    if(s_dns_dict != NULL){
        entry = av_em_dict_get(s_dns_dict, dict_key, NULL, 0);
    }
    if(entry != NULL){
        int64_t ptr = strtoull(entry->value, NULL, 10);
        dns = (EMDNS *)ptr;
        av_em_log(NULL, AV_LOG_ERROR, "hostname[%s] cache dns ptr=0x%lld\n", dict_key, ptr);
        if (now - dns->time <= s_dns_timeout){
            *res = dns->ai;
            av_em_log(NULL, AV_LOG_ERROR, "hostname[%s] dns from cache\n", dict_key);
            return 0;
        }
        else{
            freeaddrinfo(dns->ai);
            free(dns);
            av_em_dict_set(&s_dns_dict, dict_key, NULL, 0);
            av_em_log(NULL, AV_LOG_ERROR, "beyond the valid time for hostname[%s] cache dns!!\n", dict_key);
        }
    }
    int ret = getaddrinfo(hostname, servname, hints, res);
    if (ret){
        return ret;
    }
    int dict_count = s_dns_dict ? av_em_dict_count(s_dns_dict) : 0;
    if(dict_count >= s_dns_cache_count){
        av_em_log(NULL, AV_LOG_ERROR, "current dns cache size[%d] overload setting count, "
        "this dns not put int cache!!\n", dict_count);
        *freeai = 1;
        return 0;
    }
    dns = malloc(sizeof(EMDNS));
    if(dns == NULL){
        *freeai = 1;
        return 0;
    }
    dns->ai = *res;
    dns->time = now;
    av_em_dict_set_int(&s_dns_dict, dict_key, (int64_t)dns, 0);
    av_em_log(NULL, AV_LOG_ERROR, "hostname[%s] get new dns %p %lld!!\n", dict_key, dns, (int64_t)dns);
    return 0;
}

/* return non zero if error */

static int tcp_open(EMURLContext *h, const char *uri, int flags)
{
    struct addrinfo hints = { 0 }, *ai, *cur_ai;
    int port, fd = -1;
    TCPContext *s = h->priv_data;
    const char *p;
    char buf[256];
    int ret;
    int64_t start;
    int64_t now;
    int  freeai = 1;
    char hostname[1024],proto[1024],path[1024];
    char portstr[10];
    s->open_timeout = 5000000;
//    s->app_ctx = (AVApplicationContext *)(intptr_t)s->app_ctx_intptr;
    s->app_ctx = (AVApplicationContext *)av_em_dict_strtoptr(s->app_ctx_intptr);

    av_em_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto, "tcp"))
        return AVERROR(EINVAL);
    if (port <= 0 || port >= 65536) {
        av_em_log(h, AV_LOG_ERROR, "Port missing in uri\n");
        return AVERROR(EINVAL);
    }
    p = strchr(uri, '?');
    if (p) {
        if (av_em_find_info_tag(buf, sizeof(buf), "listen", p)) {
            char *endptr = NULL;
            s->listen = strtol(buf, &endptr, 10);
            /* assume if no digits were found it is a request to enable it */
            if (buf == endptr)
                s->listen = 1;
        }
        if (av_em_find_info_tag(buf, sizeof(buf), "timeout", p)) {
            s->rw_timeout = strtol(buf, NULL, 10);
        }
        if (av_em_find_info_tag(buf, sizeof(buf), "listen_timeout", p)) {
            s->listen_timeout = strtol(buf, NULL, 10);
        }
    }
    if (s->rw_timeout >= 0) {
        s->open_timeout =
        h->rw_timeout   = s->rw_timeout;
    }
    //hints.ai_family = AF_UNSPEC;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (s->listen)
        hints.ai_flags |= AI_PASSIVE;

    start = av_em_gettime();
    now = start;   

    // s->dns_cache_count == 0 disable dns cache
    if (s->dns_cache_count >= 0)
        s_dns_cache_count = s->dns_cache_count;
    if(s->dns_timeout > 0)
        s_dns_timeout = s->dns_timeout;
    if(s_dns_cache_count > 0 && s_dns_timeout > 0){
        ret = em_getaddrinfo(hostname, portstr, &hints, &ai, &freeai);
    }
    else{
#ifdef HAVE_PTHREADS
        ret = ijk_tcp_getaddrinfo_nonblock(hostname, portstr, &hints, &ai, s->addrinfo_timeout, &h->interrupt_callback, s->addrinfo_one_by_one);
#else
        if (s->addrinfo_timeout > 0)
            av_em_log(h, AV_LOG_WARNING, "Ignore addrinfo_timeout without pthreads support.\n");
        if (!hostname[0])
            ret = getaddrinfo(NULL, portstr, &hints, &ai);
        else
            ret = getaddrinfo(hostname, portstr, &hints, &ai);
#endif
    }

    now = av_em_gettime();
    av_em_log(NULL, AV_LOG_ERROR, "tcp dns cost time is %lld:\n", now - start);
    if (ret) {
        av_em_log(h, AV_LOG_ERROR,
               "Failed to resolve hostname %s: %s\n",
               hostname, gai_strerror(ret));
        return AVERROR(EIO);
    }

    cur_ai = ai;

 restart:
    if (s->ipv6_port_workaround && cur_ai->ai_family == AF_INET6 && port != 0) {
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)cur_ai->ai_addr;
        in6->sin6_port = htons(port);
    }
    fd = em_socket(cur_ai->ai_family,
                   cur_ai->ai_socktype,
                   cur_ai->ai_protocol);
    if (fd < 0) {
        ret = ff_neterrno();
        goto fail;
    }

    if (s->listen == 2) {
        // multi-client
        if ((ret = em_listen(fd, cur_ai->ai_addr, cur_ai->ai_addrlen)) < 0)
            goto fail1;
    } else if (s->listen == 1) {
        // single client
        if ((ret = em_listen_bind(fd, cur_ai->ai_addr, cur_ai->ai_addrlen,
                                  s->listen_timeout, h)) < 0)
            goto fail1;
        // Socket descriptor already closed here. Safe to overwrite to client one.
        fd = ret;
    } else {
        ret = av_em_application_on_tcp_will_open(s->app_ctx);
        if (ret) {
            av_em_log(NULL, AV_LOG_WARNING, "terminated by application in AVAPP_CTRL_WILL_TCP_OPEN");
            goto fail1;
        }

        if ((ret = em_listen_connect(fd, cur_ai->ai_addr, cur_ai->ai_addrlen,
                                     s->open_timeout / 1000, h, !!cur_ai->ai_next)) < 0) {
            if (av_em_application_on_tcp_did_open(s->app_ctx, ret, fd))
                goto fail1;
            if (ret == AVERROR_EXIT)
                goto fail1;
            else
                goto fail;
        } else {
            ret = av_em_application_on_tcp_did_open(s->app_ctx, 0, fd);
            if (ret) {
                av_em_log(NULL, AV_LOG_WARNING, "terminated by application in AVAPP_CTRL_DID_TCP_OPEN");
                goto fail1;
            }
        }
    }

    h->is_streamed = 1;
    s->fd = fd;
    /* Set the socket's send or receive buffer sizes, if specified.
       If unspecified or setting fails, system default is used. */
    if (s->recv_buffer_size > 0) {
        setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &s->recv_buffer_size, sizeof (s->recv_buffer_size));
    }
    if (s->send_buffer_size > 0) {
        setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &s->send_buffer_size, sizeof (s->send_buffer_size));
    }

    if(freeai)
        freeaddrinfo(ai);
    return 0;

 fail:
    if (cur_ai->ai_next) {
        /* Retry with the next sockaddr */
        cur_ai = cur_ai->ai_next;
        if (fd >= 0)
            closesocket(fd);
        ret = 0;
        goto restart;
    }
 fail1:
    if (fd >= 0)
        closesocket(fd);
    if(freeai)
      freeaddrinfo(ai);
    return ret;
}

static int tcp_accept(EMURLContext *s, EMURLContext **c)
{
    TCPContext *sc = s->priv_data;
    TCPContext *cc;
    int ret;
    av_assert0(sc->listen);
    if ((ret = ffurl_em_alloc(c, s->filename, s->flags, &s->interrupt_callback)) < 0)
        return ret;
    cc = (*c)->priv_data;
    ret = em_accept(sc->fd, sc->listen_timeout, s);
    if (ret < 0)
        return ff_neterrno();
    cc->fd = ret;
    return 0;
}

static int tcp_read(EMURLContext *h, uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = em_network_wait_fd_timeout(s->fd, 0, h->rw_timeout, &h->interrupt_callback);
        if (ret)
            return ret;
    }
    ret = recv(s->fd, buf, size, 0);
    if (ret > 0)
        av_em_application_did_io_tcp_read(s->app_ctx, (void*)h, ret);
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_write(EMURLContext *h, const uint8_t *buf, int size)
{
    TCPContext *s = h->priv_data;
    int ret;

    if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
        ret = em_network_wait_fd_timeout(s->fd, 1, h->rw_timeout, &h->interrupt_callback);
        if (ret)
            return ret;
    }
    ret = send(s->fd, buf, size, MSG_NOSIGNAL);
    if (ret > 0) {
	av_em_application_did_io_tcp_read(s->app_ctx, (void*)h, ret);
    }
    return ret < 0 ? ff_neterrno() : ret;
}

static int tcp_shutdown(EMURLContext *h, int flags)
{
    TCPContext *s = h->priv_data;
    int how;

    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ) {
        how = SHUT_RDWR;
    } else if (flags & AVIO_FLAG_WRITE) {
        how = SHUT_WR;
    } else {
        how = SHUT_RD;
    }

    return shutdown(s->fd, how);
}

static int tcp_close(EMURLContext *h)
{
    TCPContext *s = h->priv_data;
    closesocket(s->fd);
    return 0;
}

static int tcp_get_file_handle(EMURLContext *h)
{
    TCPContext *s = h->priv_data;
    return s->fd;
}

const EMURLProtocol em_tcp_protocol = {
    .name                = "tcp",
    .url_open            = tcp_open,
    .url_accept          = tcp_accept,
    .url_read            = tcp_read,
    .url_write           = tcp_write,
    .url_close           = tcp_close,
    .url_get_file_handle = tcp_get_file_handle,
    .url_shutdown        = tcp_shutdown,
    .priv_data_size      = sizeof(TCPContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &tcp_class,
};
