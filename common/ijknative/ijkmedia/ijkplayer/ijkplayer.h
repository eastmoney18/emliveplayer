/*
 * ijkplayer.h
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

#ifndef IJKPLAYER_ANDROID__IJKPLAYER_H
#define IJKPLAYER_ANDROID__IJKPLAYER_H

#include <stdbool.h>
#include <unistd.h>
#include "ff_ffmsg_queue.h"

#include "ijkmeta.h"
#include "ijkutil.h"

#ifndef MPTRACE
#define MPTRACE ALOGD
#endif

typedef struct EMMediaPlayer EMMediaPlayer;
struct FFPlayer;
SDL_Vout;

/*-
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_IDLE);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_INITIALIZED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ASYNC_PREPARING);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PREPARED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STARTED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PAUSED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_COMPLETED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STOPPED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ERROR);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_END);
 */

/*-
 * emmp_set_data_source()  -> MP_STATE_INITIALIZED
 *
 * emmp_reset              -> self
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_IDLE               0

/*-
 * emmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_INITIALIZED        1

/*-
 *                   ...    -> MP_STATE_PREPARED
 *                   ...    -> MP_STATE_ERROR
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_ASYNC_PREPARING    2

/*-
 * emmp_seek_to()          -> self
 * emmp_start()            -> MP_STATE_STARTED
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_PREPARED           3

/*-
 * emmp_seek_to()          -> self
 * emmp_start()            -> self
 * emmp_pause()            -> MP_STATE_PAUSED
 * emmp_stop()             -> MP_STATE_STOPPED
 *                   ...    -> MP_STATE_COMPLETED
 *                   ...    -> MP_STATE_ERROR
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_STARTED            4

/*-
 * emmp_seek_to()          -> self
 * emmp_start()            -> MP_STATE_STARTED
 * emmp_pause()            -> self
 * emmp_stop()             -> MP_STATE_STOPPED
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_PAUSED             5

/*-
 * emmp_seek_to()          -> self
 * emmp_start()            -> MP_STATE_STARTED (from beginning)
 * emmp_pause()            -> self
 * emmp_stop()             -> MP_STATE_STOPPED
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_COMPLETED          6

/*-
 * emmp_stop()             -> self
 * emmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_STOPPED            7

/*-
 * emmp_reset              -> MP_STATE_IDLE
 * emmp_release            -> MP_STATE_END
 */
#define MP_STATE_ERROR              8

/*-
 * emmp_release            -> self
 */
#define MP_STATE_END                9


#define IJKMP_IO_STAT_READ 1


#define IJKMP_OPT_CATEGORY_FORMAT FFP_OPT_CATEGORY_FORMAT
#define IJKMP_OPT_CATEGORY_CODEC  FFP_OPT_CATEGORY_CODEC
#define IJKMP_OPT_CATEGORY_SWS    FFP_OPT_CATEGORY_SWS
#define IJKMP_OPT_CATEGORY_PLAYER FFP_OPT_CATEGORY_PLAYER
#define IJKMP_OPT_CATEGORY_SWR    FFP_OPT_CATEGORY_SWR


void            emmp_global_init();
void            emmp_global_uninit();
void            emmp_global_set_log_report(int use_report);
void            emmp_global_set_log_level(int log_level);   // log_level = AV_LOG_xxx
void            emmp_global_set_log_callback(ijksdl_log_callback cb);
void            emmp_global_set_inject_callback(ijk_inject_callback cb);
void            emmp_set_video_frame_present_callback(EMMediaPlayer *mp, ijk_present_video_frame_callback cb);
void            emmp_set_audio_frame_present_callback(EMMediaPlayer *mp, ijk_present_audio_frame_callback cb);
const char     *emmp_version_ident();
unsigned int    emmp_version_int();
void            emmp_io_stat_register(void (*cb)(const char *url, int type, int bytes));
void            emmp_io_stat_complete_register(void (*cb)(const char *url,
                                                           int64_t read_bytes, int64_t total_size,
                                                           int64_t elpased_time, int64_t total_duration));

// ref_count is 1 after open
EMMediaPlayer *emmp_create(int (*msg_loop)(void*));
void           *emmp_set_inject_opaque(EMMediaPlayer *mp, void *opaque);

void            emmp_set_option(EMMediaPlayer *mp, int opt_category, const char *name, const char *value);
void            emmp_set_option_int(EMMediaPlayer *mp, int opt_category, const char *name, int64_t value);

int             emmp_get_video_codec_info(EMMediaPlayer *mp, char **codec_info);
int             emmp_get_audio_codec_info(EMMediaPlayer *mp, char **codec_info);
void            emmp_set_playback_rate(EMMediaPlayer *mp, float rate);
int             emmp_set_stream_selected(EMMediaPlayer *mp, int stream, int selected);

float           emmp_get_property_float(EMMediaPlayer *mp, int id, float default_value);
void            emmp_set_property_float(EMMediaPlayer *mp, int id, float value);
int64_t         emmp_get_property_int64(EMMediaPlayer *mp, int id, int64_t default_value);
void            emmp_set_property_int64(EMMediaPlayer *mp, int id, int64_t value);

// must be freed with free();
IjkMediaMeta   *emmp_get_meta_l(EMMediaPlayer *mp);

// preferred to be called explicity, can be called multiple times
// NOTE: emmp_shutdown may block thread
void            emmp_shutdown(EMMediaPlayer *mp);

void            emmp_inc_ref(EMMediaPlayer *mp);

// call close at last release, also free memory
// NOTE: emmp_dec_ref may block thread
void            emmp_dec_ref(EMMediaPlayer *mp);
void            emmp_dec_ref_p(EMMediaPlayer **pmp);
void            emmp_set_play_mode(EMMediaPlayer *mp, int playType);

int             emmp_set_data_source(EMMediaPlayer *mp, const char *url);
int             emmp_prepare_async(EMMediaPlayer *mp);
int             emmp_restart(EMMediaPlayer *mp);
int             emmp_start(EMMediaPlayer *mp);
int             emmp_pause(EMMediaPlayer *mp);
void            emmp_mute(EMMediaPlayer *mp, int onoff);
int             emmp_standby(EMMediaPlayer *mp);
int             emmp_stop(EMMediaPlayer *mp);
int             emmp_seek_to(EMMediaPlayer *mp, long msec);
int             emmp_get_state(EMMediaPlayer *mp);
bool            emmp_is_playing(EMMediaPlayer *mp);
long            emmp_get_current_position(EMMediaPlayer *mp);
long            emmp_get_duration(EMMediaPlayer *mp);
long            emmp_get_playable_duration(EMMediaPlayer *mp);
void            emmp_set_loop(EMMediaPlayer *mp, int loop);
int             emmp_get_loop(EMMediaPlayer *mp);
int             emmp_change_video_source(EMMediaPlayer *mp, char *path, int playType);

        void           *emmp_get_weak_thiz(EMMediaPlayer *mp);
void           *emmp_set_weak_thiz(EMMediaPlayer *mp, void *weak_thiz);

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int             emmp_get_msg(EMMediaPlayer *mp, AVMessage *msg, int block);
int             emmp_get_reconnect_count(EMMediaPlayer *mp);int             emmp_get_reconnect_interval(EMMediaPlayer *mp);
void             emmp_set_reconnect_count(EMMediaPlayer *mp, int count);
void             emmp_set_reconnect_interval(EMMediaPlayer *mp, int interval);
int             emmp_set_record_status(EMMediaPlayer *mp, int record_on);
int             emmp_prepare_new_video_source(EMMediaPlayer *mp, char *video_path, int playType);
int             emmp_change_video_source_with_prepared_index(EMMediaPlayer *mp, int index);
int             emmp_delete_prepared_video_source(EMMediaPlayer *mp, int index);
void            emmp_set_play_channel_mode(EMMediaPlayer *mp, int mode);

void            emmp_present_audio_pcm(EMMediaPlayer *mp, uint8_t *buff, int size, int sr, int channels, int ptsms);

#endif
