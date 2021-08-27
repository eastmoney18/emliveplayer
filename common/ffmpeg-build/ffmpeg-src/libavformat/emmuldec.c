//
// Created by 陈海东 on 17/11/16.
//

#include <libavutil/internal.h>
#include <libavutil/opt.h>
#include "avformat.h"
#include "internal.h"

#define EMMUL_MAX_URLS 100
#define EMMUL_MAX_URL_LENGTH 1024

struct segment{
    int64_t duration;
    int64_t start_time;
    int64_t url_offset;
    int64_t size;
    char *url;
    AVEMFormatContext *internal_ctx;
};

typedef struct EMMulContext {
    AVEMClass *class;
    AVEMFormatContext *ctx;
    AVEMIOInterruptCB *interrupt_callback;
    AVEMDictionary *avio_opts;
    struct segment *segments[EMMUL_MAX_URLS];
    char urls[EMMUL_MAX_URLS][EMMUL_MAX_URL_LENGTH];
    int url_count;
    int cur_url_index;
    int test;
}EMMulContext;

static int emmul_probe(AVEMProbeData * pd) {
    if (!strncmp(pd->filename, "emmul://", 8)) {
        av_em_log(NULL, AV_LOG_WARNING, "emmul_probe success.\n");
         return 100;
    }
    return 0;
}

static int emmul_parse_play_list_urls(EMMulContext *c, const char *url)
{
    const char *fl_num = url + strlen("emmul://");
    char *fl_addr = (char *)strtoul(fl_num, NULL, 10);
    av_em_log(c->ctx, AV_LOG_INFO, "url buffer addr:%p.\n", (void *)fl_addr);
    if (!fl_addr || strlen(fl_addr) > EMMUL_MAX_URL_LENGTH * EMMUL_MAX_URLS) {
        av_em_log(c->ctx, AV_LOG_ERROR, "invalid multi urls buffer addr.\n");
        return AVERROR_INVALIDDATA;
    }
    char *ul_temp = av_em_strdup(fl_addr);
    if (!ul_temp) {
        return AVERROR(ENOMEM);
    }
    char *ptr = NULL, *ptr1 = NULL;
    int i = 0;
    ptr = ul_temp;
    do{
        ptr1 = ptr;
        ptr = strchr(ptr, '\n');
        if (ptr != NULL) {
            *ptr = '\0';
            ptr++;
        }
        strcpy(c->urls[i++], ptr1);
        av_em_log(c->ctx, AV_LOG_INFO, "url:%d:%s.\n", i, ptr1);
    } while (ptr != NULL && *ptr != '\0');
    c->url_count = i;
    av_em_freep(&ul_temp);
    return 0;
}

static int emmul_read_header(struct AVEMFormatContext * s, AVEMDictionary **options)
{
    EMMulContext *c = s->priv_data;
    AVEMFormatContext *ic;
    int ret = 0;
    int i = 0;
    char *filename = av_em_strdup(s->filename);
    if (!filename) {
        return AVERROR(ENOMEM);
    }
    c->ctx = s;
    c->interrupt_callback = &s->interrupt_callback;
    ret = emmul_parse_play_list_urls(c, filename);
    av_em_freep(&filename);
    if (ret != 0) {
        return ret;
    }
    if (c->url_count <= 0) {
        av_em_log(c->ctx, AV_LOG_ERROR, "cannot parse any valid url.\n");
        return AVERROR_INVALIDDATA;
    }
    s->duration = 0;
    AVEMPacket pkt;
    for (i = 0; i < c->url_count; i++) {
        c->segments[i] = (struct segment *)av_em_alloc(sizeof(struct segment));
        if (!c->segments[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        c->segments[i]->internal_ctx = avformat_em_alloc_context();
        if (!c->segments[i]->internal_ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        c->segments[i]->internal_ctx->interrupt_callback = *c->interrupt_callback;
        ret = avformat_em_open_input(&c->segments[i]->internal_ctx, c->urls[i], NULL, options);
        if (ret)
            goto fail;
        ret = avformat_em_find_stream_info(c->segments[i]->internal_ctx, NULL);
        if (ret)
            goto fail;
        c->segments[i]->duration = c->segments[i]->internal_ctx->duration;
        s->duration += c->segments[i]->duration;
        c->segments[i]->duration = av_em_rescale_q(c->segments[i]->duration, AV_TIME_BASE_Q, c->segments[i]->internal_ctx->streams[0]->time_base);
        ret = av_em_read_frame(c->segments[i]->internal_ctx, &pkt);
        if (ret)
            goto fail;
        if (pkt.pts != 0 || pkt.dts != 0) {
            c->segments[i]->start_time = pkt.dts;
            c->segments[i]->duration -= pkt.dts;
        }
        //avformat_em_seek_file(c->segments[i]->internal_ctx, -1, INT_MIN, 0, INT_MAX, 0);
        avio_em_fast_seek_begin(c->segments[i]->internal_ctx->pb);
        av_em_log(c->ctx, AV_LOG_INFO, "url index:%d has %d streams.\n", i, c->segments[i]->internal_ctx->nb_streams);
    }
    ic = c->segments[0]->internal_ctx;
    for (i = 0; i< ic->nb_streams; i++) {
        AVEMStream *st = avformat_em_new_stream(s, NULL);
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        AVEMStream *ist = ic->streams[i];
        st->id = i;
        avcodec_em_parameters_copy(st->codecpar, ic->streams[i]->codecpar);
        avpriv_em_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
    }
    c->cur_url_index = 0;
    return 0;
fail:
    for (i = 0; i < c->url_count; i++) {
        if (c->segments[i] != NULL) {
            if (c->segments[i]->internal_ctx != NULL) {
                avformat_em_close_input(&c->segments[i]->internal_ctx);
                c->segments[i]->internal_ctx = NULL;
            }
            av_em_freep(&c->segments[i]);
            c->segments[i] = 0;
        }
    }
    return ret;
}

static int emmul_read_packet(struct AVEMFormatContext *s, AVEMPacket *pkt) {
    EMMulContext *c = s->priv_data;
    AVEMFormatContext *ic = c->segments[c->cur_url_index]->internal_ctx;
    int i = 0;
    int eof = 0;
    int ret = av_em_read_frame(ic, pkt);
    if (ret < 0) {
        if (ret == AVERROR_EOF || avio_em_feof(ic->pb)) {
            if (ic->pb->error != AVERROR(ETIMEDOUT) && ic->pb->error != AVERROR_EXIT) {
                eof = 1;
            }
        }
        if (!eof || c->cur_url_index + 1 >= c->url_count) {
            return ret;
        }
        c->cur_url_index++;
        ic = c->segments[c->cur_url_index]->internal_ctx;
        //avformat_em_seek_file(ic, -1, INT_MIN, 0, INT_MAX, 0);
        avio_em_fast_seek_begin(c->segments[i]->internal_ctx->pb);
        ret = av_em_read_frame(ic, pkt);
    }
    if (!ret && c->cur_url_index > 0) {
        for (i = 0; i < c->cur_url_index; ++i) {
            pkt->pts += c->segments[i]->duration;
            pkt->dts += c->segments[i]->duration;
        }
    }
    return ret;
}

static int emmul_read_close(struct AVEMFormatContext * s) {
    EMMulContext *c = s->priv_data;
    int i = 0;
    for (i = 0; i < c->url_count; i++) {
        if (c->segments[i] != NULL) {
            if (c->segments[i]->internal_ctx != NULL) {
                avformat_em_close_input(&c->segments[i]->internal_ctx);
                c->segments[i]->internal_ctx = NULL;
            }
            av_em_freep(&c->segments[i]);
            c->segments[i] = 0;
        }
    }
    return 0;
}

static int emmul_read_seek(struct AVEMFormatContext *s, int stream_index, int64_t timestamp, int flags) {
    EMMulContext *c = s->priv_data;
    AVEMFormatContext *ic = c->segments[c->cur_url_index]->internal_ctx;
    int segments_index = 0;
    int64_t duration = 0;
    int ret = -1;
    for (segments_index = 0; segments_index < c->url_count; segments_index++) {
        if (duration + c->segments[segments_index]->duration > timestamp) {
            break;
        }
        duration += c->segments[segments_index]->duration;
    }
    if (segments_index < c->url_count) {
        c->cur_url_index = segments_index;
        av_em_log(s, AV_LOG_INFO, "emmul seek timestamp:%lld, segment_index:%d, segment_timestamp:%lld.\n", timestamp, segments_index, timestamp - duration);
        ret = av_em_seek_frame(c->segments[segments_index]->internal_ctx, stream_index, timestamp - duration, flags);
        if (ret < 0) {
            ret = avformat_em_seek_file(c->segments[segments_index]->internal_ctx, stream_index, INT_MIN, timestamp - duration, INT_MAX, flags);
        }
    }
    return ret;
}

#define OFFSET(x) offsetof(EMMulContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM

static const AVOption emmul_option[] = {
        {"test", "test option", OFFSET(test), AV_OPT_TYPE_INT, {.i64 = 1}, INT_MIN, INT_MAX, FLAGS},
        {NULL}
};

static const AVEMClass emmul_class = {
    .class_name = "emmul",
    .item_name = av_em_default_item_name,
    .option = emmul_option,
    .version = LIBAVUTIL_VERSION_INT
};

AVEMInputFormat em_emmul_demuxer = {
    .name = "emmul",
    .long_name = NULL_IF_CONFIG_SMALL("eastmoney multi video src demuxer"),
    .priv_class = &emmul_class,
    .priv_data_size = sizeof(EMMulContext),
    .read_probe = emmul_probe,
    //.read_header = emmul_read_header,
    .read_header2 = emmul_read_header,
    .read_packet = emmul_read_packet,
    .read_close = emmul_read_close,
    .read_seek = emmul_read_seek,
};
