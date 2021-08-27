/*
 * ijkplayer_android.c
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

#include "ijkplayer_android.h"
#include <assert.h>
#include "ijksdl/android/ijksdl_android.h"
#include "ijksdl/fastimage/fastimage_vout.h"
#include "j4a/class/tv/danmaku/ijk/media/player/IjkMediaPlayer.h"
#include "../ff_fferror.h"
#include "../ff_ffplay.h"
#include "../ijkplayer_internal.h"
#include "../pipeline/ffpipeline_ffplay.h"
#include "pipeline/ffpipeline_android.h"

void emmp_present_overlay_buffer(void *env_, void *opaque, SDL_VoutOverlay *overlay)
{
    //ALOGE("draw overlay buffer");
    int colorFormat = 0;
    int length = 0;
    JNIEnv *env = env_;
    if (env && opaque && overlay) {
        EMMediaPlayer *mp = (EMMediaPlayer *) opaque;
        jbyteArray presentBuffer = (jbyteArray) mp->presentBuffer;
        switch (overlay->format) {
            case SDL_FCC_RV32:
                colorFormat = IJK_MEDIA_RGBA32;
                length = overlay->pitches[0] * overlay->h;
                if (length > mp->presentBufferLength || !presentBuffer) {
                    if (mp->presentBuffer) {
                        (*env)->DeleteGlobalRef(env, mp->presentBuffer);
                    }
                    ALOGI("env:%p", env);
                    presentBuffer = (*env)->NewByteArray(env, length);
                    presentBuffer = (*env)->NewGlobalRef(env, presentBuffer);
                    if (!presentBuffer) {
                        ALOGE("alloc present buffer failed, length:%d\n", length);
                        return;
                    }
                    mp->presentBuffer = (void *)presentBuffer;
                    mp->presentBufferLength = length;
                }
                (*env)->SetByteArrayRegion(env, presentBuffer, 0, length, (jbyte *)overlay->pixels[0]);
                break;
            default:
                ALOGE("fast image unsupported color format.");
        }
        J4AC_IjkMediaPlayer__postPresentBufferFromNative((JNIEnv *)env, mp->weak_thiz, overlay->format, mp->presentBuffer, overlay->pitches[0] / 4, overlay->h);
    }
}

EMMediaPlayer *emmp_android_create(int(*msg_loop)(void*))
{
    EMMediaPlayer *mp = emmp_create(msg_loop);
    if (!mp)
        goto fail;

    mp->ffplayer->vout = SDL_VoutAndroid_CreateForAndroidSurface();
    //mp->ffplayer->vout = SDL_VoutFastImage_Create();
    if (!mp->ffplayer->vout)
        goto fail;
    mp->ffplayer->vout->callerOpaque = mp;
    mp->ffplayer->vout->present_overlayBuffer = emmp_present_overlay_buffer;
    mp->ffplayer->pipeline = ffpipeline_create_from_android(mp->ffplayer);
    if (!mp->ffplayer->pipeline)
        goto fail;

    ffpipeline_set_vout(mp->ffplayer->pipeline, mp->ffplayer->vout);

    return mp;

fail:
    emmp_dec_ref_p(&mp);
    return NULL;
}

void emmp_android_set_surface_l(JNIEnv *env, EMMediaPlayer *mp, jobject android_surface)
{
    if (!mp || !mp->ffplayer || !mp->ffplayer->vout)
        return;

    //SDL_VoutAndroid_SetAndroidSurface(env, mp->ffplayer->vout, android_surface);
    ffpipeline_set_surface(env, mp->ffplayer->pipeline, android_surface);
}

void emmp_android_set_surface(JNIEnv *env, EMMediaPlayer *mp, jobject android_surface)
{
    if (!mp)
        return;

    MPTRACE("emmp_set_android_surface(surface=%p)", (void*)android_surface);
    pthread_mutex_lock(&mp->mutex);
    emmp_android_set_surface_l(env, mp, android_surface);
    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_set_android_surface(surface=%p)=void", (void*)android_surface);
}

void emmp_android_set_volume(JNIEnv *env, EMMediaPlayer *mp, float left, float right)
{
    if (!mp)
        return;

    MPTRACE("emmp_android_set_volume(%f, %f)", left, right);
    pthread_mutex_lock(&mp->mutex);

    if (mp && mp->ffplayer && mp->ffplayer->pipeline) {
        ffpipeline_set_volume(mp->ffplayer->pipeline, left, right);
    }

    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_android_set_volume(%f, %f)=void", left, right);
}

int emmp_android_get_audio_session_id(JNIEnv *env, EMMediaPlayer *mp)
{
    int audio_session_id = 0;
    if (!mp)
        return audio_session_id;

    MPTRACE("%s()", __func__);
    pthread_mutex_lock(&mp->mutex);

    if (mp && mp->ffplayer && mp->ffplayer->aout) {
        audio_session_id = SDL_AoutGetAudioSessionId(mp->ffplayer->aout);
    }

    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("%s()=%d", __func__, audio_session_id);

    return audio_session_id;
}

void emmp_android_set_mediacodec_select_callback(EMMediaPlayer *mp, bool (*callback)(void *opaque, emmp_mediacodecinfo_context *mcc), void *opaque)
{
    if (!mp)
        return;

    MPTRACE("emmp_android_set_mediacodec_select_callback()");
    pthread_mutex_lock(&mp->mutex);

    if (mp && mp->ffplayer && mp->ffplayer->pipeline) {
        ffpipeline_set_mediacodec_select_callback(mp->ffplayer->pipeline, callback, opaque);
    }

    pthread_mutex_unlock(&mp->mutex);
    MPTRACE("emmp_android_set_mediacodec_select_callback()=void");
}

