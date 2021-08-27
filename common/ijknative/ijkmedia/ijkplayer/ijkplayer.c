/*
 * ijkplayer.c
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#include "ijkplayer.h"
#include "ijkplayer_internal.h"
#include "version.h"

#define MP_RET_IF_FAILED(ret) \
    do { \
        int retval = ret; \
        if (retval != 0) return (retval); \
    } while(0)

#define MPST_RET_IF_EQ_INT(real, expected, errcode) \
    do { \
        if ((real) == (expected)) return (errcode); \
    } while(0)

#define MPST_RET_IF_EQ(real, expected) \
    MPST_RET_IF_EQ_INT(real, expected, EIJK_INVALID_STATE)

inline static void emmp_destroy(EMMediaPlayer *mp)
{
    if (!mp)
        return;

    ffp_destroy_p(&mp->ffplayer);
    if (mp->msg_thread) {
        SDL_WaitThread(mp->msg_thread, NULL);
        mp->msg_thread = NULL;
    }

    pthread_mutex_destroy(&mp->mutex);

    freep((void**)&mp->data_source);
    memset(mp, 0, sizeof(EMMediaPlayer));
    freep((void**)&mp);
    ALOGI("emmp_destroyed");
}

inline static void emmp_destroy_p(EMMediaPlayer **pmp)
{
    if (!pmp)
        return;

    emmp_destroy(*pmp);
    *pmp = NULL;
}

void emmp_global_init()
{
    ffp_global_init();
}

void emmp_global_uninit()
{
    ffp_global_uninit();
}

void emmp_global_set_log_report(int use_report)
{
    ffp_global_set_log_report(use_report);
}

void emmp_global_set_log_level(int log_level)
{
    ffp_global_set_log_level(log_level);
    ijksdl_set_log_level(log_level);
}

void emmp_global_set_log_callback(ijksdl_log_callback cb)
{
    ijksdl_set_log_callback(cb);
}

void emmp_global_set_inject_callback(ijk_inject_callback cb)
{
    ffp_global_set_inject_callback(cb);
}

void emmp_set_video_frame_present_callback(EMMediaPlayer *mp, ijk_present_video_frame_callback cb)
{
    ffp_set_video_frame_callback(mp->ffplayer, cb);
}

void emmp_set_audio_frame_present_callback(EMMediaPlayer *mp, ijk_present_audio_frame_callback cb)
{
    ffp_set_audio_frame_callback(mp->ffplayer, cb);
}

const char *emmp_version_ident()
{
    return LIBIJKPLAYER_IDENT;
}

unsigned int emmp_version_int()
{
    return LIBIJKPLAYER_VERSION_INT;
}

void emmp_io_stat_register(void (*cb)(const char *url, int type, int bytes))
{
    ffp_io_stat_register(cb);
}

void emmp_io_stat_complete_register(void (*cb)(const char *url,
                                                int64_t read_bytes, int64_t total_size,
                                                int64_t elpased_time, int64_t total_duration))
{
    ffp_io_stat_complete_register(cb);
}

void emmp_change_state_l(EMMediaPlayer *mp, int new_state)
{
    mp->mp_state = new_state;
    ffp_notify_msg1(mp->ffplayer, FFP_MSG_PLAYBACK_STATE_CHANGED);
}

EMMediaPlayer *emmp_create(int (*msg_loop)(void*))
{
    EMMediaPlayer *mp = (EMMediaPlayer *) mallocz(sizeof(EMMediaPlayer));
    if (!mp)
        goto fail;
    memset(mp, 0, sizeof(EMMediaPlayer));
    mp->ffplayer = ffp_create();
    if (!mp->ffplayer)
        goto fail;

    mp->msg_loop = msg_loop;

    emmp_inc_ref(mp);
    pthread_mutex_init(&mp->mutex, NULL);

    return mp;

    fail:
    emmp_destroy_p(&mp);
    return NULL;
}

void *emmp_set_inject_opaque(EMMediaPlayer *mp, void *opaque)
{
    assert(mp);

    MPTRACE("%s(%p)\n", __func__, opaque);
    return ffp_set_inject_opaque(mp->ffplayer, opaque);
    MPTRACE("%s()=void\n", __func__);
}

void emmp_set_option(EMMediaPlayer *mp, int opt_category, const char *name, const char *value)
{
    assert(mp);

    // MPTRACE("%s(%s, %s)\n", __func__, name, value);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_option(mp->ffplayer, opt_category, name, value);
    pthread_mutex_unlock(&mp->mutex);
    // MPTRACE("%s()=void\n", __func__);
}

void emmp_set_option_int(EMMediaPlayer *mp, int opt_category, const char *name, int64_t value)
{
    assert(mp);

    // MPTRACE("%s(%s, %"PRId64")\n", __func__, name, value);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_option_int(mp->ffplayer, opt_category, name, value);
    pthread_mutex_unlock(&mp->mutex);
    // MPTRACE("%s()=void\n", __func__);
}

int emmp_get_video_codec_info(EMMediaPlayer *mp, char **codec_info)
{
    assert(mp);

    MPTRACE("%s\n", __func__);
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_get_video_codec_info(mp->ffplayer, codec_info);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("%s()=void\n", __func__);
    return ret;
}

int emmp_get_audio_codec_info(EMMediaPlayer *mp, char **codec_info)
{
    assert(mp);

    MPTRACE("%s\n", __func__);
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_get_audio_codec_info(mp->ffplayer, codec_info);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("%s()=void\n", __func__);
    return ret;
}

void emmp_set_playback_rate(EMMediaPlayer *mp, float rate)
{
    assert(mp);

    MPTRACE("%s(%f)\n", __func__, rate);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_playback_rate(mp->ffplayer, rate);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("%s()=void\n", __func__);
}

int emmp_set_stream_selected(EMMediaPlayer *mp, int stream, int selected)
{
    assert(mp);

    MPTRACE("%s(%d, %d)\n", __func__, stream, selected);
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_set_stream_selected(mp->ffplayer, stream, selected);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("%s(%d, %d)=%d\n", __func__, stream, selected, ret);
    return ret;
}

float emmp_get_property_float(EMMediaPlayer *mp, int id, float default_value)
{
    assert(mp);

    pthread_mutex_lock(&mp->mutex);
    float ret = ffp_get_property_float(mp->ffplayer, id, default_value);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

void emmp_set_property_float(EMMediaPlayer *mp, int id, float value)
{
    assert(mp);

    pthread_mutex_lock(&mp->mutex);
    ffp_set_property_float(mp->ffplayer, id, value);
    pthread_mutex_unlock(&mp->mutex);
}

int64_t emmp_get_property_int64(EMMediaPlayer *mp, int id, int64_t default_value)
{
    assert(mp);

    pthread_mutex_lock(&mp->mutex);
    int64_t ret = ffp_get_property_int64(mp->ffplayer, id, default_value);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

void emmp_set_property_int64(EMMediaPlayer *mp, int id, int64_t value)
{
    assert(mp);

    pthread_mutex_lock(&mp->mutex);
    ffp_set_property_int64(mp->ffplayer, id, value);
    pthread_mutex_unlock(&mp->mutex);
}

IjkMediaMeta *emmp_get_meta_l(EMMediaPlayer *mp)
{
    assert(mp);

    MPTRACE("%s\n", __func__);
    IjkMediaMeta *ret = ffp_get_meta_l(mp->ffplayer);
    MPTRACE("%s()=void\n", __func__);
    return ret;
}

void emmp_shutdown_l(EMMediaPlayer *mp)
{
    assert(mp);

    MPTRACE("emmp_shutdown_l()\n");
    if (mp->ffplayer) {
        ffp_stop_l(mp->ffplayer);
        ffp_wait_stop_l(mp->ffplayer);
    }
    MPTRACE("emmp_shutdown_l()=void\n");
}

void emmp_shutdown(EMMediaPlayer *mp)
{
    return emmp_shutdown_l(mp);
}

void emmp_inc_ref(EMMediaPlayer *mp)
{
    assert(mp);
    __sync_fetch_and_add(&mp->ref_count, 1);
}

void emmp_dec_ref(EMMediaPlayer *mp)
{
    if (!mp)
        return;

    int ref_count = __sync_sub_and_fetch(&mp->ref_count, 1);
    if (ref_count == 0) {
        MPTRACE("emmp_dec_ref(): ref=0\n");
        emmp_shutdown(mp);
        emmp_destroy_p(&mp);
    }
}

void emmp_dec_ref_p(EMMediaPlayer **pmp)
{
    if (!pmp)
        return;

    emmp_dec_ref(*pmp);
    *pmp = NULL;
}

static int emmp_set_data_source_l(EMMediaPlayer *mp, const char *url)
{
    assert(mp);
    assert(url);

    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);

    freep((void**)&mp->data_source);
    mp->data_source = strdup(url);
    if (!mp->data_source)
        return EIJK_OUT_OF_MEMORY;

    emmp_change_state_l(mp, MP_STATE_INITIALIZED);
    return 0;
}

int emmp_set_data_source(EMMediaPlayer *mp, const char *url)
{
    assert(mp);
    assert(url);
    MPTRACE("emmp_set_data_source(url=\"%s\")\n", url);
    pthread_mutex_lock(&mp->mutex);
    int retval = emmp_set_data_source_l(mp, url);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_set_data_source(url=\"%s\")=%d\n", url, retval);
    return retval;
}

static int emmp_msg_loop(void *arg)
{
    EMMediaPlayer *mp = arg;
    int ret = mp->msg_loop(arg);
    return ret;
}

static int emmp_prepare_async_l(EMMediaPlayer *mp)
{
    assert(mp);

    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);

    assert(mp->data_source);

    emmp_change_state_l(mp, MP_STATE_ASYNC_PREPARING);

    msg_queue_start(&mp->ffplayer->msg_queue);
    
    // released in msg_loop
    emmp_inc_ref(mp);

    mp->msg_thread = SDL_CreateThreadEx(&mp->_msg_thread, emmp_msg_loop, mp, "ff_msg_loop");
    // msg_thread is detached inside msg_loop
    // TODO: 9 release weak_thiz if pthread_create() failed;
    int retval = ffp_prepare_async_l(mp->ffplayer, mp->data_source);
    if (retval < 0) {
        emmp_change_state_l(mp, MP_STATE_ERROR);
        return retval;
    }

    return 0;
}

void emmp_set_play_mode(EMMediaPlayer *mp, int playType)
{
    assert(mp);
    MPTRACE("emmp_set_play_mode()\n");
    pthread_mutex_lock(&mp->mutex);
    ffp_set_play_mode(mp->ffplayer, playType);
    pthread_mutex_unlock(&mp->mutex);

}

int emmp_set_record_status(EMMediaPlayer *mp, int record_on){
    assert(mp);
    MPTRACE("emmp_set_record_status");
    pthread_mutex_lock(&mp->mutex);

    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    //MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    //MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);

    ffp_set_record_status(mp->ffplayer, record_on);
    pthread_mutex_unlock(&mp->mutex);
    return 0;
}

int emmp_prepare_async(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_prepare_async()\n");
    pthread_mutex_lock(&mp->mutex);
    mp->start_msec = ijk_get_timems();
    mp->stop_request = 0;
    int retval = emmp_prepare_async_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_prepare_async()=%d\n", retval);
    return retval;
}

static int ikjmp_chkst_start_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PREPARED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_STARTED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PAUSED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);

    return 0;
}

static int emmp_start_l(EMMediaPlayer *mp)
{
    assert(mp);

    MP_RET_IF_FAILED(ikjmp_chkst_start_l(mp->mp_state));

    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    ffp_notify_msg1(mp->ffplayer, FFP_REQ_START);

    return 0;
}

int emmp_start(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_start()\n");
    pthread_mutex_lock(&mp->mutex);
    mp->stop_request = 0;
    int retval = emmp_start_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_start()=%d\n", retval);
    return retval;
}

static int ikjmp_chkst_pause_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PREPARED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_STARTED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PAUSED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);

    return 0;
}

static int emmp_pause_l(EMMediaPlayer *mp)
{
    assert(mp);

    MP_RET_IF_FAILED(ikjmp_chkst_pause_l(mp->mp_state));

    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    ffp_notify_msg1(mp->ffplayer, FFP_REQ_PAUSE);

    return 0;
}

int emmp_pause(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_pause()\n");
    pthread_mutex_lock(&mp->mutex);
    int retval = emmp_pause_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_pause()=%d\n", retval);
    return retval;
}


void emmp_mute(EMMediaPlayer *mp, int onoff)
{
    assert(mp);
    MPTRACE("emmp_mute(%d)\n", onoff);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_mute_audio(mp->ffplayer, onoff);
    pthread_mutex_unlock(&mp->mutex);
}

static int emmp_standby_l(EMMediaPlayer *mp)
{
    assert(mp);
    MP_RET_IF_FAILED(ikjmp_chkst_pause_l(mp->mp_state));
    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_STANDBY);
    ffp_notify_msg1(mp->ffplayer, FFP_REQ_STANDBY);
    return 0;
}

int emmp_standby(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_standby()\n");
    pthread_mutex_lock(&mp->mutex);
    int retval = emmp_standby_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_standby()=%d\n", retval);
    return retval;
}

static int emmp_stop_l(EMMediaPlayer *mp)
{
    assert(mp);

    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    // MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_STANDBY);
    int retval = ffp_stop_l(mp->ffplayer);
    if (retval < 0) {
        return retval;
    }

    emmp_change_state_l(mp, MP_STATE_STOPPED);
    return 0;
}

/**
 * emmp_stop(mp);
   emmp_shutdown(mp);
   usleep(emmp_get_reconnect_interval(mp) * 1000000);
   emmp_prepare_async(mp);
 */
/*int emmp_restart(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_restart()\n");
    pthread_mutex_lock(&mp->mutex);
    int retval = emmp_stop_l(mp);
    if (retval) {
        MPTRACE("emmp_stop_l error:%d\n", retval);
    }
    emmp_shutdown_l(mp);
    int restart_interval = emmp_get_reconnect_interval(mp) * 1000;
    int sleep_time = 0;
    pthread_mutex_unlock(&mp->mutex);
    while (sleep_time < restart_interval) {
        usleep(10 * 1000);
        sleep_time += 10;
        pthread_mutex_lock(&mp->mutex);
        if (mp->stop_request) {
            MPTRACE("request stop player, cancel restart\n");
            pthread_mutex_unlock(&mp->mutex);
            return retval;
        }
        pthread_mutex_unlock(&mp->mutex);
    }
    pthread_mutex_lock(&mp->mutex);
    retval = emmp_prepare_async_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_restart()=%d\n", retval);
    return retval;
}*/


int emmp_stop(EMMediaPlayer *mp)
{
    assert(mp);
    MPTRACE("emmp_stop() 1\n");
    int retval = -1;
    pthread_mutex_lock(&mp->mutex);
    MPTRACE("emmp_stop() 2\n");
    mp->stop_request = 1;
    retval = emmp_stop_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_stop()=%d\n", retval);
    return retval;
}

bool emmp_is_playing(EMMediaPlayer *mp)
{
    assert(mp);
    if (mp->mp_state == MP_STATE_PREPARED ||
        mp->mp_state == MP_STATE_STARTED) {
        return true;
    }

    return false;
}

static int ikjmp_chkst_seek_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PREPARED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_STARTED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_PAUSED);
    // MPST_RET_IF_EQ(mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);

    return 0;
}

int emmp_seek_to_l(EMMediaPlayer *mp, long msec)
{
    assert(mp);

    MP_RET_IF_FAILED(ikjmp_chkst_seek_l(mp->mp_state));

    mp->seek_req = 1;
    mp->seek_msec = msec;
    ffp_remove_msg(mp->ffplayer, FFP_REQ_SEEK);
    ffp_notify_msg2(mp->ffplayer, FFP_REQ_SEEK, (int)msec);
    // TODO: 9 64-bit long?

    return 0;
}

int emmp_seek_to(EMMediaPlayer *mp, long msec)
{
    assert(mp);
    MPTRACE("emmp_seek_to(%ld)\n", msec);
    pthread_mutex_lock(&mp->mutex);
    int retval = emmp_seek_to_l(mp, msec);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_seek_to(%ld)=%d\n", msec, retval);

    return retval;
}

int emmp_get_state(EMMediaPlayer *mp)
{
    return mp->mp_state;
}

static long emmp_get_current_position_l(EMMediaPlayer *mp)
{
    if (mp->seek_req)
        return mp->seek_msec;
    return ffp_get_current_position_l(mp->ffplayer);
}

long emmp_get_current_position(EMMediaPlayer *mp)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    long retval;
    if (mp->seek_req)
        retval = mp->seek_msec;
    else
        retval = emmp_get_current_position_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    return retval;
}

static long emmp_get_duration_l(EMMediaPlayer *mp)
{
    return ffp_get_duration_l(mp->ffplayer);
}

long emmp_get_duration(EMMediaPlayer *mp)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    long retval = emmp_get_duration_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    return retval;
}

static long emmp_get_playable_duration_l(EMMediaPlayer *mp)
{
    return ffp_get_playable_duration_l(mp->ffplayer);
}

long emmp_get_playable_duration(EMMediaPlayer *mp)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    long retval = emmp_get_playable_duration_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    return retval;
}

void emmp_set_loop(EMMediaPlayer *mp, int loop)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_loop(mp->ffplayer, loop);
    pthread_mutex_unlock(&mp->mutex);
}

int emmp_get_loop(EMMediaPlayer *mp)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    int loop = ffp_get_loop(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
    return loop;
}

int emmp_change_video_source(EMMediaPlayer *mp, char *path, int playType)
{
    int ret = 0;
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ret = ffp_change_video_source(mp->ffplayer, path, playType);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

int emmp_prepare_new_video_source(EMMediaPlayer *mp, char *video_path, int playType)
{
    int ret = 0;
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ret = ffp_prepare_new_video_source_l(mp->ffplayer, video_path, playType);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

int emmp_change_video_source_with_prepared_index(EMMediaPlayer *mp, int index)
{
    int ret = 0;
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ret = ffp_change_video_source_with_prepared_index_l(mp->ffplayer, index);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

int emmp_delete_prepared_video_source(EMMediaPlayer *mp, int index)
{
    int ret = 0;
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ret = ffp_delete_prepared_video_source(mp->ffplayer, index);
    pthread_mutex_unlock(&mp->mutex);
    return ret;
}

void emmp_set_play_channel_mode(EMMediaPlayer *mp, int mode)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    ffp_set_play_channel(mp->ffplayer, mode);
    pthread_mutex_unlock(&mp->mutex);
}

void emmp_present_audio_pcm(EMMediaPlayer *mp, uint8_t *buff, int size, int sr, int channels, int ptsms)
{
    assert(mp);
    pthread_mutex_lock(&mp->mutex);
    if(mp->ffplayer && mp->ffplayer->audio_present_callback){
        mp->ffplayer->audio_present_callback(mp->ffplayer->inject_opaque, buff, size, sr, channels, ptsms);
    }
    pthread_mutex_unlock(&mp->mutex);
}

void *emmp_get_weak_thiz(EMMediaPlayer *mp)
{
    return mp->weak_thiz;
}

void *emmp_set_weak_thiz(EMMediaPlayer *mp, void *weak_thiz)
{
    void *prev_weak_thiz = mp->weak_thiz;

    mp->weak_thiz = weak_thiz;

    return prev_weak_thiz;
}

int emmp_get_msg(EMMediaPlayer *mp, AVMessage *msg, int block)
{
    assert(mp);
    while (1) {
        int continue_wait_next_msg = 0;
        int retval = msg_queue_get(&mp->ffplayer->msg_queue, msg, block);
        if (retval <= 0)
            return retval;

        switch (msg->what) {
        case FFP_MSG_PREPARED:
            MPTRACE("emmp_get_msg: FFP_MSG_PREPARED\n");
            ALOGI("%s:prepared takes time:%lld.\n", __func__, ijk_get_timems() - mp->start_msec);
            pthread_mutex_lock(&mp->mutex);
            if (mp->mp_state == MP_STATE_ASYNC_PREPARING) {
                emmp_change_state_l(mp, MP_STATE_PREPARED);
            } else {
                // FIXME: 1: onError() ?
                av_em_log(mp->ffplayer, AV_LOG_DEBUG, "FFP_MSG_PREPARED: expecting mp_state==MP_STATE_ASYNC_PREPARING\n");
            }
            if (ffp_is_paused_l(mp->ffplayer)) {
                emmp_change_state_l(mp, MP_STATE_PAUSED);
            }
            pthread_mutex_unlock(&mp->mutex);
            break;

        case FFP_MSG_COMPLETED:
            MPTRACE("emmp_get_msg: FFP_MSG_COMPLETED\n");

            pthread_mutex_lock(&mp->mutex);
            //mp->restart = 1;
            //mp->restart_from_beginning = 1;
            emmp_change_state_l(mp, MP_STATE_COMPLETED);
            pthread_mutex_unlock(&mp->mutex);
            break;
                
        case FFP_MSG_ERROR_NET_DISCONNECT:
            /*MPTRACE("emmp_get_msg: FFP_MSG_COMPLETED\n");
            pthread_mutex_lock(&mp->mutex);
            mp->restart = 1;
            mp->error_occur = 1;
            mp->restart_offset = msg->arg1;
            //emmp_change_state_l(mp, MP_STATE_COMPLETED);
            pthread_mutex_unlock(&mp->mutex);*/
            break;
        case FFP_MSG_ERROR_CONNECT_FAILD:
            MPTRACE("FFP_MSG_ERROR_CONNECT_FAILED");
            break;

        case FFP_MSG_SEEK_COMPLETE:
            MPTRACE("emmp_get_msg: FFP_MSG_SEEK_COMPLETE\n");

            pthread_mutex_lock(&mp->mutex);
            mp->seek_req = 0;
            mp->seek_msec = 0;
            pthread_mutex_unlock(&mp->mutex);
            break;

        case FFP_REQ_START:
            MPTRACE("emmp_get_msg: FFP_REQ_START\n");
            continue_wait_next_msg = 1;
            pthread_mutex_lock(&mp->mutex);
            if (0 == ikjmp_chkst_start_l(mp->mp_state)) {
                // FIXME: 8 check seekable
                if (mp->restart && mp->ffplayer->play_mode != FFP_PLAY_MODE_FLV_LIVE && mp->ffplayer->play_mode != FFP_PLAY_MODE_RTMP) {
                    if (mp->restart_from_beginning) {
                        av_em_log(mp->ffplayer, AV_LOG_DEBUG, "emmp_get_msg: FFP_REQ_START: restart from beginning\n");
                        retval = ffp_start_from_l(mp->ffplayer, 0);
                        if (retval == 0)
                            emmp_change_state_l(mp, MP_STATE_STARTED);
                    } else if (mp->error_occur) {
                        av_em_log(mp->ffplayer, AV_LOG_DEBUG, "emmp_get_msg: FFP_REQ_START: restart from beginning\n");
                        retval = ffp_start_from_l(mp->ffplayer, mp->restart_offset);
                        if (retval == 0)
                            emmp_change_state_l(mp, MP_STATE_STARTED);
                    } else {
                        av_em_log(mp->ffplayer, AV_LOG_DEBUG, "emmp_get_msg: FFP_REQ_START: restart from seek pos\n");
                        retval = ffp_start_l(mp->ffplayer);
                        if (retval == 0)
                            emmp_change_state_l(mp, MP_STATE_STARTED);
                    }
                    mp->restart = 0;
                    mp->restart_from_beginning = 0;
                    mp->error_occur = 0;
                } else {
                    av_em_log(mp->ffplayer, AV_LOG_DEBUG, "emmp_get_msg: FFP_REQ_START: start on fly\n");
                    retval = ffp_start_l(mp->ffplayer);
                    if (retval == 0)
                        emmp_change_state_l(mp, MP_STATE_STARTED);
                }
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
                
        case FFP_CHANGE_VIDEO_SOURCE_SUCCESS:
            MPTRACE("emmp_get_msg: FFP_CHANGE_VIDEO_SOURCE_SUCCESS\n");
            pthread_mutex_lock(&mp->mutex);
            emmp_change_state_l(mp, MP_STATE_STARTED);
            pthread_mutex_unlock(&mp->mutex);
            break;

        case FFP_REQ_PAUSE:
            MPTRACE("emmp_get_msg: FFP_REQ_PAUSE\n");
            continue_wait_next_msg = 1;
            pthread_mutex_lock(&mp->mutex);
            if (0 == ikjmp_chkst_pause_l(mp->mp_state)) {
                int pause_ret = ffp_pause_l(mp->ffplayer);
                if (pause_ret == 0)
                    emmp_change_state_l(mp, MP_STATE_PAUSED);
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
        case FFP_REQ_STANDBY:
            MPTRACE("emmp_get_msg: FFP_REQ_STANDBY\n");
            continue_wait_next_msg = 1;
            pthread_mutex_lock(&mp->mutex);
            if (0 == ikjmp_chkst_pause_l(mp->mp_state)) {
            int standby_ret = ffp_standby_l(mp->ffplayer);
            if (standby_ret == 0)
                emmp_change_state_l(mp, MP_STATE_PAUSED);
            }
            pthread_mutex_unlock(&mp->mutex);
            break;

        case FFP_REQ_SEEK:
            MPTRACE("emmp_get_msg: FFP_REQ_SEEK\n");
            continue_wait_next_msg = 1;

            pthread_mutex_lock(&mp->mutex);
            if (0 == ikjmp_chkst_seek_l(mp->mp_state)) {
                mp->restart_from_beginning = 0;
                mp->error_occur = 0;
                if (0 == ffp_seek_to_l(mp->ffplayer, msg->arg1)) {
                    av_em_log(mp->ffplayer, AV_LOG_DEBUG, "emmp_get_msg: FFP_REQ_SEEK: seek to %d\n", (int)msg->arg1);
                }
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
        }

        if (continue_wait_next_msg)
            continue;

        return retval;
    }

    return -1;
}

int emmp_get_reconnect_count(EMMediaPlayer *mp)
{
    return mp->ffplayer->reconnect_count;
}

int emmp_get_reconnect_interval(EMMediaPlayer *mp)
{
    return mp->ffplayer->reconnect_interval;
}

void emmp_set_reconnect_count(EMMediaPlayer *mp, int count)
{
    pthread_mutex_lock(&mp->mutex);
    mp->ffplayer->reconnect_count = count;
    pthread_mutex_unlock(&mp->mutex);
}

void emmp_set_reconnect_interval(EMMediaPlayer *mp, int interval)
{
    pthread_mutex_lock(&mp->mutex);
    mp->ffplayer->reconnect_interval = interval;
    pthread_mutex_unlock(&mp->mutex);
}


