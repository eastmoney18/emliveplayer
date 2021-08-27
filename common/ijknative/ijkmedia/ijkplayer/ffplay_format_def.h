//
// Created by eastmoney_pcfs on 2017/8/31.
//

#ifndef IJKMEDIA_FFPLAY_FORMAT_DEF_H
#define IJKMEDIA_FFPLAY_FORMAT_DEF_H

#include "libavformat/avformat.h"

typedef struct {
    AVEMFormatContext *ic;
    int play_after_prepared;
    int stream_index[AVMEDIA_TYPE_NB];
    char filename[1024];
    int fileType;
}ffplay_format_t;

typedef struct ffplay_format_queue *ffplay_format_queue_t;

struct ffplay_format_queue{
    ffplay_format_t *ffplay_format;
    ffplay_format_queue_t *next;
};

#endif //IJKMEDIA_FFPLAY_FORMAT_DEF_H
