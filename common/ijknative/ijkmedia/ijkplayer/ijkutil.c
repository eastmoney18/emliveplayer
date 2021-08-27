//
// Created by eastmoney_pcfs on 2017/8/22.
//
#include "ijkutil.h"
#include <sys/time.h>

int64_t ijk_get_timems()
{
    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);
    return (int64_t)cur_time.tv_sec*1000 + (int64_t)cur_time.tv_usec/1000;
}
