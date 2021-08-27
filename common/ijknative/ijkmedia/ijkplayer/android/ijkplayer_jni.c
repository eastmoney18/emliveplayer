/*
 * ijkplayer_jni.c
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

#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <jni.h>
#include <unistd.h>
#include <ijkplayer/ijkplayer_internal.h>
#include "j4a/class/java/util/ArrayList.h"
#include "j4a/class/android/os/Bundle.h"
#include "j4a/class/tv/danmaku/ijk/media/player/IjkMediaPlayer.h"
#include "j4a/class/tv/danmaku/ijk/media/player/misc/IMediaDataSource.h"
#include "ijksdl/ijksdl_log.h"
#include "../ff_ffplay.h"
#include "ffmpeg_api_jni.h"
#include "ijkplayer_android_def.h"
#include "ijkplayer_android.h"
#include "ijksdl/android/ijksdl_android_jni.h"
#include "ijksdl/android/ijksdl_codec_android_mediadef.h"
#include "ijkavformat/ijkavformat.h"

#define JNI_MODULE_PACKAGE      "tv/danmaku/ijk/media/player"
#define JNI_CLASS_IJKPLAYER     "tv/danmaku/ijk/media/player/IjkMediaPlayer"
#define JNI_IJK_MEDIA_EXCEPTION "tv/danmaku/ijk/media/player/IjkMediaException"

#define IJK_CHECK_MPRET_GOTO(retval, env, label) \
    JNI_CHECK_GOTO((retval != EIJK_INVALID_STATE), env, "java/lang/IllegalStateException", NULL, label); \
    JNI_CHECK_GOTO((retval != EIJK_OUT_OF_MEMORY), env, "java/lang/OutOfMemoryError", NULL, label); \
    JNI_CHECK_GOTO((retval == 0), env, JNI_IJK_MEDIA_EXCEPTION, NULL, label);

static JavaVM* g_jvm;

typedef struct player_fields_t {
    pthread_mutex_t mutex;
    jclass clazz;
} player_fields_t;
static player_fields_t g_clazz;

static int inject_callback(void *opaque, int type, void *data, size_t data_size);
static bool mediacodec_select_callback(void *opaque, emmp_mediacodecinfo_context *mcc);

static EMMediaPlayer *jni_get_media_player(JNIEnv* env, jobject thiz)
{
    pthread_mutex_lock(&g_clazz.mutex);

    EMMediaPlayer *mp = (EMMediaPlayer *) (intptr_t) J4AC_IjkMediaPlayer__mNativeMediaPlayer__get__catchAll(env, thiz);
    if (mp) {
        emmp_inc_ref(mp);
    }

    pthread_mutex_unlock(&g_clazz.mutex);
    return mp;
}

static EMMediaPlayer *jni_set_media_player(JNIEnv* env, jobject thiz, EMMediaPlayer *mp)
{
    pthread_mutex_lock(&g_clazz.mutex);

    EMMediaPlayer *old = (EMMediaPlayer*) (intptr_t) J4AC_IjkMediaPlayer__mNativeMediaPlayer__get__catchAll(env, thiz);
    if (mp) {
        emmp_inc_ref(mp);
    }
    J4AC_IjkMediaPlayer__mNativeMediaPlayer__set__catchAll(env, thiz, (intptr_t) mp);

    pthread_mutex_unlock(&g_clazz.mutex);

    // NOTE: emmp_dec_ref may block thread
    if (old != NULL ) {
        emmp_dec_ref_p(&old);
    }

    return old;
}

static int64_t jni_set_media_data_source(JNIEnv* env, jobject thiz, jobject media_data_source)
{
    int64_t nativeMediaDataSource = 0;

    pthread_mutex_lock(&g_clazz.mutex);

    jobject old = (jobject) (intptr_t) J4AC_IjkMediaPlayer__mNativeMediaDataSource__get__catchAll(env, thiz);
    if (old) {
        J4AC_IMediaDataSource__close__catchAll(env, old);
        J4A_DeleteGlobalRef__p(env, &old);
        J4AC_IjkMediaPlayer__mNativeMediaDataSource__set__catchAll(env, thiz, 0);
    }

    if (media_data_source) {
        jobject global_media_data_source = (*env)->NewGlobalRef(env, media_data_source);
        if (J4A_ExceptionCheck__catchAll(env) || !global_media_data_source)
            goto fail;

        nativeMediaDataSource = (int64_t) (intptr_t) global_media_data_source;
        J4AC_IjkMediaPlayer__mNativeMediaDataSource__set__catchAll(env, thiz, (jlong) nativeMediaDataSource);
    }

fail:
    pthread_mutex_unlock(&g_clazz.mutex);
    return nativeMediaDataSource;
}

static int message_loop(void *arg);

static void
IjkMediaPlayer_setMultiDataSourceAndHeaders(
        JNIEnv *env, jobject thiz, jstring path,
        jobjectArray keys, jobjectArray values)
{
            MPTRACE("%s\n", __func__);
    int retval = 0;
    const char *c_path = NULL;
    char addr_path[128];
    int c_length = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(path, env, "java/lang/IllegalArgumentException", "mpjni: setDataSource: null path", LABEL_RETURN);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setDataSource: null mp", LABEL_RETURN);

    c_path = (*env)->GetStringUTFChars(env, path, NULL );
    JNI_CHECK_GOTO(c_path, env, "java/lang/OutOfMemoryError", "mpjni: setDataSource: path.string oom", LABEL_RETURN);

    ALOGV("setDataSource: path %s", c_path);
    c_length = (*env)->GetStringUTFLength(env, path) + 1;
    if (c_length > mp->multi_urls_buf_length) {
        if (mp->multi_urls_buf != NULL) {
            free(mp->multi_urls_buf);
        }
        mp->multi_urls_buf = (char *)malloc(c_length);
        if (!mp->multi_urls_buf) {
            ALOGE("create multi urls buf failed, malloc failed, size:%d.\n", c_length);
            (*env)->ReleaseStringUTFChars(env, path, c_path);
            mp->multi_urls_buf_length = 0;
            goto LABEL_RETURN;
        }
        mp->multi_urls_buf_length = c_length;
    }
    strcpy(mp->multi_urls_buf, c_path);
    sprintf(addr_path, "emmul://%lu", (unsigned long)mp->multi_urls_buf);
    retval = emmp_set_data_source(mp, addr_path);
    (*env)->ReleaseStringUTFChars(env, path, c_path);

    IJK_CHECK_MPRET_GOTO(retval, env, LABEL_RETURN);

    LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setDataSourceAndHeaders(
    JNIEnv *env, jobject thiz, jstring path,
    jobjectArray keys, jobjectArray values)
{
    MPTRACE("%s\n", __func__);
    int retval = 0;
    const char *c_path = NULL;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(path, env, "java/lang/IllegalArgumentException", "mpjni: setDataSource: null path", LABEL_RETURN);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setDataSource: null mp", LABEL_RETURN);

    c_path = (*env)->GetStringUTFChars(env, path, NULL );
    JNI_CHECK_GOTO(c_path, env, "java/lang/OutOfMemoryError", "mpjni: setDataSource: path.string oom", LABEL_RETURN);

    ALOGV("setDataSource: path %s", c_path);
    retval = emmp_set_data_source(mp, c_path);
    (*env)->ReleaseStringUTFChars(env, path, c_path);

    IJK_CHECK_MPRET_GOTO(retval, env, LABEL_RETURN);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setDataSourceFd(JNIEnv *env, jobject thiz, jint fd)
{
    MPTRACE("%s\n", __func__);
    int retval = 0;
    int dupFd = 0;
    char uri[128];
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(fd > 0, env, "java/lang/IllegalArgumentException", "mpjni: setDataSourceFd: null fd", LABEL_RETURN);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setDataSourceFd: null mp", LABEL_RETURN);

    dupFd = dup(fd);

    ALOGV("setDataSourceFd: dup(%d)=%d\n", fd, dupFd);
    snprintf(uri, sizeof(uri), "pipe:%d", dupFd);
    retval = emmp_set_data_source(mp, uri);

    IJK_CHECK_MPRET_GOTO(retval, env, LABEL_RETURN);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setDataSourceCallback(JNIEnv *env, jobject thiz, jobject callback)
{
    MPTRACE("%s\n", __func__);
    int retval = 0;
    char uri[128];
    int64_t nativeMediaDataSource = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(callback, env, "java/lang/IllegalArgumentException", "mpjni: setDataSourceCallback: null fd", LABEL_RETURN);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setDataSourceCallback: null mp", LABEL_RETURN);

    nativeMediaDataSource = jni_set_media_data_source(env, thiz, callback);
    JNI_CHECK_GOTO(nativeMediaDataSource, env, "java/lang/IllegalStateException", "mpjni: jni_set_media_data_source: NewGlobalRef", LABEL_RETURN);

    ALOGV("setDataSourceCallback: %"PRId64"\n", nativeMediaDataSource);
    snprintf(uri, sizeof(uri), "ijkmediadatasource:%"PRId64, nativeMediaDataSource);

    retval = emmp_set_data_source(mp, uri);

    IJK_CHECK_MPRET_GOTO(retval, env, LABEL_RETURN);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setVideoSurface(JNIEnv *env, jobject thiz, jobject jsurface)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setVideoSurface: null mp", LABEL_RETURN);

    emmp_android_set_surface(env, mp, jsurface);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return;
}

static void
IjkMediaPlayer_prepareAsync(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    int retval = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: prepareAsync: null mp", LABEL_RETURN);

    retval = emmp_prepare_async(mp);
    IJK_CHECK_MPRET_GOTO(retval, env, LABEL_RETURN);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setPlayType(JNIEnv *env, jobject thiz, jint playType)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: start: null mp", LABEL_RETURN);
    emmp_set_play_mode(mp, playType);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_start(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: start: null mp", LABEL_RETURN);

    emmp_start(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_stop(JNIEnv *env, jobject thiz)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: stop: null mp", LABEL_RETURN);

    emmp_stop(mp);
    if (mp->multi_urls_buf && mp->multi_urls_buf_length > 0) {
        free(mp->multi_urls_buf);
        mp->multi_urls_buf_length = 0;
    }
LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_pause(JNIEnv *env, jobject thiz)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: pause: null mp", LABEL_RETURN);

    emmp_pause(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_standby(JNIEnv *env, jobject thiz)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni:standby null mp", LABEL_RETURN);
    emmp_standby(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_seekTo(JNIEnv *env, jobject thiz, jlong msec)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: seekTo: null mp", LABEL_RETURN);

    emmp_seek_to(mp, msec);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static jboolean
IjkMediaPlayer_isPlaying(JNIEnv *env, jobject thiz)
{
    jboolean retval = JNI_FALSE;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: isPlaying: null mp", LABEL_RETURN);

    retval = emmp_is_playing(mp) ? JNI_TRUE : JNI_FALSE;

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return retval;
}

static jlong
IjkMediaPlayer_getCurrentPosition(JNIEnv *env, jobject thiz)
{
    jlong retval = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getCurrentPosition: null mp", LABEL_RETURN);

    retval = emmp_get_current_position(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return retval;
}

static jlong
IjkMediaPlayer_getDuration(JNIEnv *env, jobject thiz)
{
    jlong retval = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getDuration: null mp", LABEL_RETURN);

    retval = emmp_get_duration(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return retval;
}

static jint
IjkMediaPlayer_changeVideoSource(JNIEnv *env, jobject thiz, jstring path, jint type)
{
    jint ret = 0;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getDuration: null mp", LABEL_RETURN);
    const char *c_path = (*env)->GetStringUTFChars(env, path, NULL);
    ret = emmp_change_video_source(mp, (char*)c_path, type);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    (*env)->ReleaseStringUTFChars(env, path, c_path);
    return ret;
}

static jint
IjkMediaPlayer_changeMultiVideoSource(JNIEnv *env, jobject thiz, jstring path, jint type)
{
    jint ret = 0;
    int c_length = 0;
    char addr_path[128];
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getDuration: null mp", LABEL_RETURN);
    const char *c_path = (*env)->GetStringUTFChars(env, path, NULL);
    c_length = (*env)->GetStringUTFLength(env, path) + 1;
    if (c_length > mp->multi_urls_buf_length) {
        if (mp->multi_urls_buf) {
            free(mp->multi_urls_buf);
        }
        mp->multi_urls_buf = (char *)malloc(c_length);
        if (!mp->multi_urls_buf) {
            ALOGE("change multi video source failed, malloc faile, size:%d.\n", c_length);
            mp->multi_urls_buf_length = 0;
            goto  LABEL_RETURN;
        }
        mp->multi_urls_buf_length = c_length;
    }
    strcpy(mp->multi_urls_buf, c_path);
    sprintf(addr_path, "emmul://%lu", (unsigned long)mp->multi_urls_buf);
    ret = emmp_change_video_source(mp, (char*)addr_path, type);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    (*env)->ReleaseStringUTFChars(env, path, c_path);
    return ret;
}

static void
IjkMediaPlayer_release(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (!mp)
        return;
    struct timeval tm;
    gettimeofday(&tm, NULL);
    int64_t  a_time = tm.tv_sec * 1000 + tm.tv_usec / 1000;
    int64_t cur_time = a_time;
    emmp_android_set_surface(env, mp, NULL);
    gettimeofday(&tm, NULL);
    cur_time = tm.tv_sec * 1000 + tm.tv_usec / 1000;
    MPTRACE("set_surface takes time:%lldms.\n", cur_time - a_time);
    a_time = cur_time;
    // explicit shutdown mp, in case it is not the last mp-ref here
    emmp_shutdown(mp);
    gettimeofday(&tm, NULL);
    cur_time = tm.tv_sec * 1000 + tm.tv_usec / 1000;
    MPTRACE("emmp_shutdown takes time:%lldms.\n", cur_time - a_time);
    a_time = cur_time;
    jni_set_media_player(env, thiz, NULL);
    jni_set_media_data_source(env, thiz, NULL);
    gettimeofday(&tm, NULL);
    cur_time = tm.tv_sec * 1000 + tm.tv_usec / 1000;
    MPTRACE("set media player source takes time:%lldms.\n", cur_time - a_time);
    a_time = cur_time;
    jobject weak_thiz = (jobject) emmp_set_weak_thiz(mp, NULL );
    emmp_dec_ref_p(&mp);
    //only delete weak_thiz at release
    (*env)->DeleteGlobalRef(env, weak_thiz);
}

static void IjkMediaPlayer_native_setup(JNIEnv *env, jobject thiz, jobject weak_this);
static void
IjkMediaPlayer_reset(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (!mp)
        return;

    jobject weak_thiz = (jobject) emmp_set_weak_thiz(mp, NULL );

    IjkMediaPlayer_release(env, thiz);
    IjkMediaPlayer_native_setup(env, thiz, weak_thiz);

    emmp_dec_ref_p(&mp);
}

static void IjkMediaPlayer_set_record_status(JNIEnv *env, jobject thiz, jint record_on){
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if(!mp)
        return;

    emmp_set_record_status(mp, record_on);
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setLoopCount(JNIEnv *env, jobject thiz, jint loop_count)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setLoopCount: null mp", LABEL_RETURN);

    emmp_set_loop(mp, loop_count);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static jint
IjkMediaPlayer_getLoopCount(JNIEnv *env, jobject thiz)
{
    jint loop_count = 1;
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getLoopCount: null mp", LABEL_RETURN);

    loop_count = emmp_get_loop(mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return loop_count;
}

static jfloat
ijkMediaPlayer_getPropertyFloat(JNIEnv *env, jobject thiz, jint id, jfloat default_value)
{
    jfloat value = default_value;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getPropertyFloat: null mp", LABEL_RETURN);

    value = emmp_get_property_float(mp, id, default_value);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return value;
}

static void
ijkMediaPlayer_setPropertyFloat(JNIEnv *env, jobject thiz, jint id, jfloat value)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setPropertyFloat: null mp", LABEL_RETURN);

    emmp_set_property_float(mp, id, value);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return;
}

static jlong
ijkMediaPlayer_getPropertyLong(JNIEnv *env, jobject thiz, jint id, jlong default_value)
{
    jlong value = default_value;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getPropertyLong: null mp", LABEL_RETURN);

    value = emmp_get_property_int64(mp, id, default_value);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return value;
}

static void
ijkMediaPlayer_setPropertyLong(JNIEnv *env, jobject thiz, jint id, jlong value)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setPropertyLong: null mp", LABEL_RETURN);

    emmp_set_property_int64(mp, id, value);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return;
}

static void
ijkMediaPlayer_setStreamSelected(JNIEnv *env, jobject thiz, jint stream, jboolean selected)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    int ret = 0;
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setStreamSelected: null mp", LABEL_RETURN);

    ret = emmp_set_stream_selected(mp, stream, selected);
    if (ret < 0) {
        ALOGE("failed to %s %d", selected ? "select" : "deselect", stream);
        goto LABEL_RETURN;
    }

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return;
}

static void
IjkMediaPlayer_setVolume(JNIEnv *env, jobject thiz, jfloat leftVolume, jfloat rightVolume)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: setVolume: null mp", LABEL_RETURN);

    emmp_android_set_volume(env, mp, leftVolume, rightVolume);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static jint
IjkMediaPlayer_getAudioSessionId(JNIEnv *env, jobject thiz)
{
    jint audio_session_id = 0;
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: getAudioSessionId: null mp", LABEL_RETURN);

    audio_session_id = emmp_android_get_audio_session_id(env, mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    return audio_session_id;
}

static void
IjkMediaPlayer_setOption(JNIEnv *env, jobject thiz, jint category, jobject name, jobject value)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    const char *c_name = NULL;
    const char *c_value = NULL;
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setOption: null mp", LABEL_RETURN);

    c_name = (*env)->GetStringUTFChars(env, name, NULL );
    JNI_CHECK_GOTO(c_name, env, "java/lang/OutOfMemoryError", "mpjni: setOption: name.string oom", LABEL_RETURN);

    if (value) {
        c_value = (*env)->GetStringUTFChars(env, value, NULL );
        JNI_CHECK_GOTO(c_name, env, "java/lang/OutOfMemoryError", "mpjni: setOption: name.string oom", LABEL_RETURN);
    }

    emmp_set_option(mp, category, c_name, c_value);

LABEL_RETURN:
    if (c_name)
        (*env)->ReleaseStringUTFChars(env, name, c_name);
    if (c_value)
        (*env)->ReleaseStringUTFChars(env, value, c_value);
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_setOptionLong(JNIEnv *env, jobject thiz, jint category, jobject name, jlong value)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    const char *c_name = NULL;
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: setOptionLong: null mp", LABEL_RETURN);

    c_name = (*env)->GetStringUTFChars(env, name, NULL );
    JNI_CHECK_GOTO(c_name, env, "java/lang/OutOfMemoryError", "mpjni: setOptionLong: name.string oom", LABEL_RETURN);

    emmp_set_option_int(mp, category, c_name, value);

LABEL_RETURN:
    if (c_name)
        (*env)->ReleaseStringUTFChars(env, name, c_name);
    emmp_dec_ref_p(&mp);
}

static jstring
IjkMediaPlayer_getColorFormatName(JNIEnv *env, jclass clazz, jint mediaCodecColorFormat)
{
    const char *codec_name = SDL_AMediaCodec_getColorFormatName(mediaCodecColorFormat);
    if (!codec_name)
        return NULL ;

    return (*env)->NewStringUTF(env, codec_name);
}

static jstring
IjkMediaPlayer_getVideoCodecInfo(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    jstring jcodec_info = NULL;
    int ret = 0;
    char *codec_info = NULL;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: getVideoCodecInfo: null mp", LABEL_RETURN);

    ret = emmp_get_video_codec_info(mp, &codec_info);
    if (ret < 0 || !codec_info)
        goto LABEL_RETURN;

    jcodec_info = (*env)->NewStringUTF(env, codec_info);
LABEL_RETURN:
    if (codec_info)
        free(codec_info);

    emmp_dec_ref_p(&mp);
    return jcodec_info;
}

static jstring
IjkMediaPlayer_getAudioCodecInfo(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    jstring jcodec_info = NULL;
    int ret = 0;
    char *codec_info = NULL;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: getAudioCodecInfo: null mp", LABEL_RETURN);

    ret = emmp_get_audio_codec_info(mp, &codec_info);
    if (ret < 0 || !codec_info)
        goto LABEL_RETURN;

    jcodec_info = (*env)->NewStringUTF(env, codec_info);
LABEL_RETURN:
    if (codec_info)
        free(codec_info);

    emmp_dec_ref_p(&mp);
    return jcodec_info;
}

inline static void fillMetaInternal(JNIEnv *env, jobject jbundle, IjkMediaMeta *meta, const char *key, const char *default_value)
{
    const char *value = ijkmeta_get_string_l(meta, key);
    if (value == NULL )
        value = default_value;

    J4AC_Bundle__putString__withCString__catchAll(env, jbundle, key, value);
}

static jobject
IjkMediaPlayer_getMediaMeta(JNIEnv *env, jobject thiz)
{
    MPTRACE("%s\n", __func__);
    bool is_locked = false;
    jobject jret_bundle = NULL;
    jobject jlocal_bundle = NULL;
    jobject jstream_bundle = NULL;
    jobject jarray_list = NULL;
    IjkMediaMeta *meta = NULL;
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    JNI_CHECK_GOTO(mp, env, "java/lang/IllegalStateException", "mpjni: getMediaMeta: null mp", LABEL_RETURN);

    meta = emmp_get_meta_l(mp);
    if (!meta)
        goto LABEL_RETURN;

    ijkmeta_lock(meta);
    is_locked = true;

    jlocal_bundle = J4AC_Bundle__Bundle(env);
    if (J4A_ExceptionCheck__throwAny(env)) {
        goto LABEL_RETURN;
    }

    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_FORMAT, NULL );
    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_DURATION_US, NULL );
    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_START_US, NULL );
    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_BITRATE, NULL );

    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_VIDEO_STREAM, "-1");
    fillMetaInternal(env, jlocal_bundle, meta, IJKM_KEY_AUDIO_STREAM, "-1");

    jarray_list = J4AC_ArrayList__ArrayList(env);
    if (J4A_ExceptionCheck__throwAny(env)) {
        goto LABEL_RETURN;
    }

    size_t count = ijkmeta_get_children_count_l(meta);
    for (size_t i = 0; i < count; ++i) {
        IjkMediaMeta *streamRawMeta = ijkmeta_get_child_l(meta, i);
        if (streamRawMeta) {
            jstream_bundle = J4AC_Bundle__Bundle(env);
            if (J4A_ExceptionCheck__throwAny(env)) {
                goto LABEL_RETURN;
            }

            fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_TYPE,     IJKM_VAL_TYPE__UNKNOWN);
            fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_LANGUAGE, NULL);
            const char *type = ijkmeta_get_string_l(streamRawMeta, IJKM_KEY_TYPE);
            if (type) {
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_NAME, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_PROFILE, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_LEVEL, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_LONG_NAME, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_PIXEL_FORMAT, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_BITRATE, NULL );
                fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CODEC_PROFILE_ID, NULL );

                if (0 == strcmp(type, IJKM_VAL_TYPE__VIDEO)) {
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_WIDTH, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_HEIGHT, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_FPS_NUM, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_FPS_DEN, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_TBR_NUM, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_TBR_DEN, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_SAR_NUM, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_SAR_DEN, NULL );
                } else if (0 == strcmp(type, IJKM_VAL_TYPE__AUDIO)) {
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_SAMPLE_RATE, NULL );
                    fillMetaInternal(env, jstream_bundle, streamRawMeta, IJKM_KEY_CHANNEL_LAYOUT, NULL );
                }
                J4AC_ArrayList__add(env, jarray_list, jstream_bundle);
                if (J4A_ExceptionCheck__throwAny(env)) {
                    goto LABEL_RETURN;
                }
            }

            SDL_JNI_DeleteLocalRefP(env, &jstream_bundle);
        }
    }

    J4AC_Bundle__putParcelableArrayList__withCString__catchAll(env, jlocal_bundle, IJKM_KEY_STREAMS, jarray_list);
    jret_bundle = jlocal_bundle;
    jlocal_bundle = NULL;
LABEL_RETURN:
    if (is_locked && meta)
        ijkmeta_unlock(meta);

    SDL_JNI_DeleteLocalRefP(env, &jstream_bundle);
    SDL_JNI_DeleteLocalRefP(env, &jlocal_bundle);
    SDL_JNI_DeleteLocalRefP(env, &jarray_list);

    emmp_dec_ref_p(&mp);
    return jret_bundle;
}

static void
IjkMediaPlayer_native_init(JNIEnv *env)
{
    MPTRACE("%s\n", __func__);
}

static void
IjkMediaPlayer_native_setup(JNIEnv *env, jobject thiz, jobject weak_this)
{
    MPTRACE("%s\n", __func__);
    EMMediaPlayer *mp = emmp_android_create(message_loop);
    JNI_CHECK_GOTO(mp, env, "java/lang/OutOfMemoryError", "mpjni: native_setup: emmp_create() failed", LABEL_RETURN);

    jni_set_media_player(env, thiz, mp);
    emmp_set_weak_thiz(mp, (*env)->NewGlobalRef(env, weak_this));
    emmp_set_inject_opaque(mp, emmp_get_weak_thiz(mp));
    emmp_android_set_mediacodec_select_callback(mp, mediacodec_select_callback, emmp_get_weak_thiz(mp));

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
}

static void
IjkMediaPlayer_native_finalize(JNIEnv *env, jobject thiz, jobject name, jobject value)
{
    MPTRACE("%s\n", __func__);
    IjkMediaPlayer_release(env, thiz);
}

// NOTE: support to be called from read_thread
static int
inject_callback(void *opaque, int what, void *data, size_t data_size)
{
    JNIEnv     *env     = NULL;
    jobject     jbundle = NULL;
    int         ret     = -1;
    SDL_JNI_SetupThreadEnv(&env);

    jobject weak_thiz = (jobject) opaque;
    if (weak_thiz == NULL )
        goto fail;
    switch (what) {
        case AVAPP_CTRL_WILL_HTTP_OPEN:
        case AVAPP_CTRL_WILL_LIVE_OPEN:
        case AVAPP_CTRL_WILL_CONCAT_SEGMENT_OPEN: {
            AVAppIOControl *real_data = (AVAppIOControl *)data;
            real_data->is_handled = 0;

            jbundle = J4AC_Bundle__Bundle__catchAll(env);
            if (!jbundle) {
                ALOGE("%s: J4AC_Bundle__Bundle__catchAll failed for case %d\n", __func__, what);
                goto fail;
            }
            J4AC_Bundle__putString__withCString__catchAll(env, jbundle, "url", real_data->url);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "segment_index", real_data->segment_index);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "retry_counter", real_data->retry_counter);
            real_data->is_handled = J4AC_IjkMediaPlayer__onNativeInvoke(env, weak_thiz, what, jbundle);
            if (J4A_ExceptionCheck__catchAll(env)) {
                goto fail;
            }

            J4AC_Bundle__getString__withCString__asCBuffer(env, jbundle, "url", real_data->url, sizeof(real_data->url));
            if (J4A_ExceptionCheck__catchAll(env)) {
                goto fail;
            }
            ret = 0;
            break;
        }
        case AVAPP_EVENT_WILL_HTTP_OPEN:
        case AVAPP_EVENT_DID_HTTP_OPEN:
        case AVAPP_EVENT_WILL_HTTP_SEEK:
        case AVAPP_EVENT_DID_HTTP_SEEK: {
            AVAppHttpEvent *real_data = (AVAppHttpEvent *) data;
            jbundle = J4AC_Bundle__Bundle__catchAll(env);
            if (!jbundle) {
                ALOGE("%s: J4AC_Bundle__Bundle__catchAll failed for case %d\n", __func__, what);
                goto fail;
            }
            J4AC_Bundle__putString__withCString__catchAll(env, jbundle, "url", real_data->url);
            J4AC_Bundle__putLong__withCString__catchAll(env, jbundle, "offset", real_data->offset);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "error", real_data->error);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "http_code", real_data->http_code);
            J4AC_IjkMediaPlayer__onNativeInvoke(env, weak_thiz, what, jbundle);
            if (J4A_ExceptionCheck__catchAll(env))
                goto fail;
            ret = 0;
            break;
        }
        case AVAPP_CTRL_DID_TCP_OPEN:
        case AVAPP_CTRL_WILL_TCP_OPEN: {
            AVAppTcpIOControl *real_data = (AVAppTcpIOControl *)data;
            jbundle = J4AC_Bundle__Bundle__catchAll(env);
            if (!jbundle) {
                ALOGE("%s: J4AC_Bundle__Bundle__catchAll failed for case %d\n", __func__, what);
                goto fail;
            }
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "error", real_data->error);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "family", real_data->family);
            J4AC_Bundle__putString__withCString__catchAll(env, jbundle, "ip", real_data->ip);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "port", real_data->port);
            J4AC_Bundle__putInt__withCString__catchAll(env, jbundle, "fd", real_data->fd);
            J4AC_IjkMediaPlayer__onNativeInvoke(env, weak_thiz, what, jbundle);
            if (J4A_ExceptionCheck__catchAll(env))
                goto fail;
            ret = 0;
            break;
        }
        default: {
            ret = 0;
        }
    }
fail:
    SDL_JNI_DeleteLocalRefP(env, &jbundle);
    return ret;
}

static bool mediacodec_select_callback(void *opaque, emmp_mediacodecinfo_context *mcc)
{
    JNIEnv *env = NULL;
    jobject weak_this = (jobject) opaque;
    const char *found_codec_name = NULL;

    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("%s: SetupThreadEnv failed\n", __func__);
        return -1;
    }

    found_codec_name = J4AC_IjkMediaPlayer__onSelectCodec__withCString__asCBuffer(env, weak_this, mcc->mime_type, mcc->profile, mcc->level, mcc->codec_name, sizeof(mcc->codec_name));
    if (J4A_ExceptionCheck__catchAll(env) || !found_codec_name) {
        ALOGE("%s: onSelectCodec failed\n", __func__);
        goto fail;
    }

fail:
    return found_codec_name;
}

inline static void post_event(JNIEnv *env, jobject weak_this, int what, int arg1, int arg2)
{
     //MPTRACE("post_event(%p, %p, %d, %d, %d)", (void*)env, (void*) weak_this, what, arg1, arg2);
    J4AC_IjkMediaPlayer__postEventFromNative(env, weak_this, what, arg1, arg2, NULL);
    // MPTRACE("post_event()=void");
}

static void message_loop_n(JNIEnv *env, EMMediaPlayer *mp)
{
    jobject weak_thiz = (jobject) emmp_get_weak_thiz(mp);
    JNI_CHECK_GOTO(weak_thiz, env, NULL, "mpjni: message_loop_n: null weak_thiz", LABEL_RETURN);

    while (1) {
        AVMessage msg;

        int retval = emmp_get_msg(mp, &msg, 1);
        if (retval < 0)
            break;

        // block-get should never return 0
        assert(retval > 0);

        switch (msg.what) {
        case FFP_MSG_FLUSH:
            MPTRACE("FFP_MSG_FLUSH:\n");
            post_event(env, weak_thiz, MEDIA_NOP, 0, 0);
            break;
        case FFP_MSG_ERROR:
            MPTRACE("FFP_MSG_ERROR: %d\n", msg.arg1);
            post_event(env, weak_thiz, MEDIA_ERROR, MEDIA_ERROR_IJK_PLAYER, msg.arg1);
            break;
        case FFP_MSG_PREPARED:
            MPTRACE("FFP_MSG_PREPARED:\n");
            post_event(env, weak_thiz, MEDIA_PREPARED, 0, 0);
            break;
        case FFP_MSG_COMPLETED:
            MPTRACE("FFP_MSG_COMPLETED:\n");
            post_event(env, weak_thiz, MEDIA_PLAYBACK_COMPLETE, 0, 0);
            break;
        case FFP_MSG_VIDEO_SIZE_CHANGED:
            MPTRACE("FFP_MSG_VIDEO_SIZE_CHANGED: %d, %d\n", msg.arg1, msg.arg2);
            post_event(env, weak_thiz, MEDIA_SET_VIDEO_SIZE, msg.arg1, msg.arg2);
            break;
        case FFP_MSG_SAR_CHANGED:
            MPTRACE("FFP_MSG_SAR_CHANGED: %d, %d\n", msg.arg1, msg.arg2);
            post_event(env, weak_thiz, MEDIA_SET_VIDEO_SAR, msg.arg1, msg.arg2);
            break;
        case FFP_MSG_VIDEO_RENDERING_START:
            MPTRACE("FFP_MSG_VIDEO_RENDERING_START:\n");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_VIDEO_RENDERING_START, 0);
            break;
        case FFP_MSG_VIDEO_DECODE_FIRST_I_FRAME:
            MPTRACE("FFP_MSG_VIDEO_DECODE_FIRST_I_FRAME;\n");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_VIDEO_FIRST_I_FRAME_DECODED, 0);
            break;
        case FFP_MSG_AUDIO_RENDERING_START:
            MPTRACE("FFP_MSG_AUDIO_RENDERING_START:\n");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_AUDIO_RENDERING_START, 0);
            break;
        case FFP_MSG_VIDEO_ROTATION_CHANGED:
            MPTRACE("FFP_MSG_VIDEO_ROTATION_CHANGED: %d\n", msg.arg1);
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_VIDEO_ROTATION_CHANGED, msg.arg1);
            break;
        case FFP_MSG_BUFFERING_START:
            MPTRACE("FFP_MSG_BUFFERING_START:\n");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_BUFFERING_START, 0);
            break;
        case FFP_MSG_BUFFERING_END:
            MPTRACE("FFP_MSG_BUFFERING_END:\n");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_BUFFERING_END, 0);
            break;
         case FFP_MSG_WARN_RECONNECT:
             MPTRACE("FFP_MSG_WARN_RECONNECT\n");
             post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_NETWORK_RECONNECT, 0);
             break;
        case FFP_MSG_ERROR_NET_DISCONNECT:
            MPTRACE("FFP_MSG_ERROR_NET_DISCONNECT\n");
            //if (emmp_get_reconnect_count(mp) > 0) {
            //    emmp_restart(mp);
            //}else {
            post_event(env, weak_thiz, MEDIA_ERROR, MEDIA_ERROR_NETWORK_DISCONNECT, 0);
            //}
            break;
        case FFP_MSG_BUFFERING_UPDATE:
            // MPTRACE("FFP_MSG_BUFFERING_UPDATE: %d, %d", msg.arg1, msg.arg2);
            post_event(env, weak_thiz, MEDIA_BUFFERING_UPDATE, msg.arg1, msg.arg2);
            break;
        case FFP_MSG_BUFFERING_BYTES_UPDATE:
            break;
        case FFP_MSG_BUFFERING_TIME_UPDATE:
            break;
        case FFP_MSG_SEEK_COMPLETE:
            MPTRACE("FFP_MSG_SEEK_COMPLETE:\n");
            post_event(env, weak_thiz, MEDIA_SEEK_COMPLETE, 0, 0);
            break;
        case FFP_MSG_PLAYBACK_STATE_CHANGED:
            break;
        case FFP_MSG_ERROR_UNSUPPORTED_FORMAT:
            MPTRACE("FFP_MSG_ERROR_UNSUPPORTED_FORMAT");
            post_event(env, weak_thiz, MEDIA_ERROR_UNSUPPORTED, 0, 0);
            break;
        case FFP_MSG_ERROR_CONNECT_FAILD:
            MPTRACE("FFP_MSG_ERROR_CONNECT_FAILD");
            post_event(env, weak_thiz, MEDIA_ERROR, MEDIA_ERROR_NETWORK_DISCONNECT, msg.arg1);
            break;
        case FFP_MSG_PROGRESS:
            //MPTRACE("FFP_MSG_PROGRESS");
            post_event(env, weak_thiz, MEDIA_PLAY_PROGRESS, msg.arg1, msg.arg2);
            break;
        case FFP_MSG_STREAM_UNIX_TIME:
            post_event(env, weak_thiz, MEDIA_PLAY_STREAM_UNIX_TIME, msg.arg1, msg.arg2);
            break;
        case FFP_CHANGE_VIDEO_SOURCE_SUCCESS:
            MPTRACE("FFP_CHANGE_VIDEO_SOURCE_SUCCESS");
            post_event(env, weak_thiz, MEDIA_INFO, MEDIA_INFO_VIDEO_SOURCE_CHANGED, 0);
            break;
        case FFP_MSG_EXIT_READ_THREAD:
            post_event(env, weak_thiz, MEDIA_EXIT_READ, 0, 0);
            break;

        default:
            ALOGE("unknown FFP_MSG_xxx(%d)\n", msg.what);
            break;
        }
    }

LABEL_RETURN:
    ALOGI("ijkplayer message loop exit.\n");
}

static int message_loop(void *arg)
{
    MPTRACE("%s\n", __func__);

    JNIEnv *env = NULL;
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL );

    EMMediaPlayer *mp = (EMMediaPlayer*) arg;
    JNI_CHECK_GOTO(mp, env, NULL, "mpjni: native_message_loop: null mp", LABEL_RETURN);

    message_loop_n(env, mp);

LABEL_RETURN:
    emmp_dec_ref_p(&mp);
    (*g_jvm)->DetachCurrentThread(g_jvm);

    MPTRACE("message_loop exit");
    return 0;
}

// ----------------------------------------------------------------------------
void monstartup(const char *libname);
void moncleanup(void);

static void
IjkMediaPlayer_native_profileBegin(JNIEnv *env, jclass clazz, jstring libName)
{
    MPTRACE("%s\n", __func__);

    const char *c_lib_name = NULL;
    static int s_monstartup = 0;

    if (!libName)
        return;

    if (s_monstartup) {
        ALOGW("monstartup already called\b");
        return;
    }

    c_lib_name = (*env)->GetStringUTFChars(env, libName, NULL );
    JNI_CHECK_GOTO(c_lib_name, env, "java/lang/OutOfMemoryError", "mpjni: monstartup: libName.string oom", LABEL_RETURN);

    s_monstartup = 1;
    monstartup(c_lib_name);
    ALOGD("monstartup: %s\n", c_lib_name);

LABEL_RETURN:
    if (c_lib_name)
        (*env)->ReleaseStringUTFChars(env, libName, c_lib_name);
}

static void
IjkMediaPlayer_native_set_mute(JNIEnv *env, jobject thiz, int mute)
{
    MPTRACE("%s(%d)\n", __func__, mute);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        emmp_mute(mp, mute);
        emmp_dec_ref_p(&mp);
    }
}

static void
IjkMediaPlayer_native_set_play_audio_channle_mode(JNIEnv *env, jobject thiz, int mode)
{
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        emmp_set_play_channel_mode(mp, mode);
    }
}

static void
IjkMediaPlayer_native_profileEnd(JNIEnv *env, jclass clazz)
{
    MPTRACE("%s\n", __func__);
    static int s_moncleanup = 0;

    if (s_moncleanup) {
        ALOGW("moncleanu already called\b");
        return;
    }

    s_moncleanup = 1;
    moncleanup();
    ALOGD("moncleanup\n");
}

static void
IjkMediaPlayer_native_setLogLevel(JNIEnv *env, jclass clazz, jint level)
{
    MPTRACE("%s(%d)\n", __func__, level);
    emmp_global_set_log_level(level);
    ALOGD("moncleanup\n");
}

static void
IjkMediaPlayer_native_setPlaybackRate(JNIEnv *env, jclass thiz, jfloat rate)
{
    MPTRACE("%s(%.2f)\n", __func__, rate);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        emmp_set_playback_rate(mp, rate);
        ALOGD("set playback rate to %.2f\n", rate);
        emmp_dec_ref_p(&mp);
    }
}

static int
IjkMediaPlayer_native_prepareNewVideoSource(JNIEnv *env, jclass thiz, jstring path, jint type)
{
    const char *path_cstr = (*env)->GetStringUTFChars(env, path, NULL);
    if (!path_cstr) {
        ALOGE("get path failed.\n");
        return -1;
    }
    MPTRACE("%s(%s)\n", __func__, path_cstr);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        int ret = emmp_prepare_new_video_source(mp, (char *) path_cstr, type);
        emmp_dec_ref_p(&mp);
        (*env)->ReleaseStringUTFChars(env, path, path_cstr);
        return ret;
    }
    return -1;
}

static int
IjkMediaPlayer_native_changeVideoSourceWithPreparedIndex(JNIEnv *env, jclass thiz, jint index)
{
    MPTRACE("%s(%d)\n", __func__, index);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        int ret = emmp_change_video_source_with_prepared_index(mp, index);
        emmp_dec_ref(mp);
        return ret;
    }
    return -1;
}

static int
IjkMediaPlayer_native_deletePreparedVideoSource(JNIEnv *env, jclass thiz, int index)
{
    MPTRACE("%s(%d)\n", __func__, index);
    EMMediaPlayer *mp = jni_get_media_player(env, thiz);
    if (mp) {
        int ret = emmp_delete_prepared_video_source(mp, index);
        emmp_dec_ref(mp);
        return ret;
    }
    return -1;
}



// ----------------------------------------------------------------------------

static JNINativeMethod g_methods[] = {
    {
        "_setDataSource",
        "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
        (void *) IjkMediaPlayer_setDataSourceAndHeaders
    },
    { "_setMultiDataSource", "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V", (void *) IjkMediaPlayer_setMultiDataSourceAndHeaders},
    { "_setDataSourceFd",       "(I)V",     (void *) IjkMediaPlayer_setDataSourceFd },
    { "_setDataSource",         "(Ltv/danmaku/ijk/media/player/misc/IMediaDataSource;)V", (void *)IjkMediaPlayer_setDataSourceCallback },

    { "_setVideoSurface",       "(Landroid/view/Surface;)V", (void *) IjkMediaPlayer_setVideoSurface },
    { "_setPlayType",            "(I)V",     (void *) IjkMediaPlayer_setPlayType},
    { "_prepareAsync",          "()V",      (void *) IjkMediaPlayer_prepareAsync },
    { "_start",                 "()V",      (void *) IjkMediaPlayer_start },
    { "_stop",                  "()V",      (void *) IjkMediaPlayer_stop },
    { "seekTo",                 "(J)V",     (void *) IjkMediaPlayer_seekTo },
    { "_pause",                 "()V",      (void *) IjkMediaPlayer_pause },
    { "isPlaying",              "()Z",      (void *) IjkMediaPlayer_isPlaying },
    {"_set_record_status",      "(I)V",     (void *) IjkMediaPlayer_set_record_status},
    { "getCurrentPosition",     "()J",      (void *) IjkMediaPlayer_getCurrentPosition },
    { "getDuration",            "()J",      (void *) IjkMediaPlayer_getDuration },
    { "_release",               "()V",      (void *) IjkMediaPlayer_release },
    { "_reset",                 "()V",      (void *) IjkMediaPlayer_reset },
    { "setVolume",              "(FF)V",    (void *) IjkMediaPlayer_setVolume },
    { "getAudioSessionId",      "()I",      (void *) IjkMediaPlayer_getAudioSessionId },
    { "native_init",            "()V",      (void *) IjkMediaPlayer_native_init },
    { "native_setup",           "(Ljava/lang/Object;)V", (void *) IjkMediaPlayer_native_setup },
    { "native_finalize",        "()V",      (void *) IjkMediaPlayer_native_finalize },

    { "_setOption",             "(ILjava/lang/String;Ljava/lang/String;)V", (void *) IjkMediaPlayer_setOption },
    { "_setOption",             "(ILjava/lang/String;J)V",                  (void *) IjkMediaPlayer_setOptionLong },

    { "_getColorFormatName",    "(I)Ljava/lang/String;",    (void *) IjkMediaPlayer_getColorFormatName },
    { "_getVideoCodecInfo",     "()Ljava/lang/String;",     (void *) IjkMediaPlayer_getVideoCodecInfo },
    { "_getAudioCodecInfo",     "()Ljava/lang/String;",     (void *) IjkMediaPlayer_getAudioCodecInfo },
    { "_getMediaMeta",          "()Landroid/os/Bundle;",    (void *) IjkMediaPlayer_getMediaMeta },
    { "_setLoopCount",          "(I)V",                     (void *) IjkMediaPlayer_setLoopCount },
    { "_getLoopCount",          "()I",                      (void *) IjkMediaPlayer_getLoopCount },
    { "_getPropertyFloat",      "(IF)F",                    (void *) ijkMediaPlayer_getPropertyFloat },
    { "_setPropertyFloat",      "(IF)V",                    (void *) ijkMediaPlayer_setPropertyFloat },
    { "_getPropertyLong",       "(IJ)J",                    (void *) ijkMediaPlayer_getPropertyLong },
    { "_setPropertyLong",       "(IJ)V",                    (void *) ijkMediaPlayer_setPropertyLong },
    { "_setStreamSelected",     "(IZ)V",                    (void *) ijkMediaPlayer_setStreamSelected },

    { "native_profileBegin",    "(Ljava/lang/String;)V",    (void *) IjkMediaPlayer_native_profileBegin },
    { "native_profileEnd",      "()V",                      (void *) IjkMediaPlayer_native_profileEnd },

    { "native_setLogLevel",     "(I)V",                     (void *) IjkMediaPlayer_native_setLogLevel },
    { "_setPlaybackRate",       "(F)V",                     (void *) IjkMediaPlayer_native_setPlaybackRate },
    { "_changeVideoSource",     "(Ljava/lang/String;I)I",    (void *) IjkMediaPlayer_changeVideoSource},
    { "_changeMultiVideoSource",     "(Ljava/lang/String;I)I",(void *) IjkMediaPlayer_changeMultiVideoSource},
    { "_prepareNewVideoSource", "(Ljava/lang/String;I)I",    (void *) IjkMediaPlayer_native_prepareNewVideoSource},
    { "_changeVideoSourceWithPreparedIndex", "(I)I",        (void *) IjkMediaPlayer_native_changeVideoSourceWithPreparedIndex},
    { "_deletePreparedVideoSource",     "(I)I",             (void *) IjkMediaPlayer_native_deletePreparedVideoSource},
    { "_standby",               "()V",                      (void *) IjkMediaPlayer_standby},
    { "_setMute",               "(I)V",                     (void *) IjkMediaPlayer_native_set_mute},
    { "_setPlayAudioChannelMode",  "(I)V",                  (void *) IjkMediaPlayer_native_set_play_audio_channle_mode},
};

/*JNIEXPORT*/ jint IJK_JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv* env = NULL;

    g_jvm = vm;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        return -1;
    }
    assert(env != NULL);

    pthread_mutex_init(&g_clazz.mutex, NULL );

    // FindClass returns LocalReference
    IJK_FIND_JAVA_CLASS(env, g_clazz.clazz, JNI_CLASS_IJKPLAYER);
    (*env)->RegisterNatives(env, g_clazz.clazz, g_methods, NELEM(g_methods) );

    emmp_global_init();
    emmp_global_set_inject_callback(inject_callback);

    FFmpegApi_global_init(env);

    return JNI_VERSION_1_4;
}

/*JNIEXPORT*/ void IJK_JNI_OnUnload(JavaVM *jvm, void *reserved)
{
    MPTRACE("%s\n", __func__);

    emmp_global_uninit();

    pthread_mutex_destroy(&g_clazz.mutex);
}
