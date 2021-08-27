//
//  ijksdl_log.c
//  IJKMediaPlayer
//
//  Created by 陈海东 on 18/1/18.
//  Copyright © 2018年 bilibili. All rights reserved.
//

#include <stdarg.h>
#include "ijksdl_log.h"

static int g_cur_level = 0;
static ijksdl_log_callback g_log_callback = NULL;


void ijksdl_set_log_level(int level)
{
    g_cur_level = level;
}


void ijksdl_log_vprint(int level, const char *buf, va_list vl)
{
    if (level >= g_cur_level) {
        if(g_log_callback){
            g_log_callback(level, buf, vl);
        }
        else{
            vprintf(buf, vl);
        }
    }
}

void ijksdl_log_printf(int level ,const char *buf, ...)
{
    va_list args;
    va_start(args, buf);
    if (level >= g_cur_level) {
        if(g_log_callback){
            g_log_callback(level, buf, args);
        }
        else{
            vprintf(buf, args);
        }
    }
    va_end(args);
}

void ijksdl_set_log_callback(ijksdl_log_callback cb)
{
    g_log_callback = cb;
}
