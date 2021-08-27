/*
 * ijkplayer_android.h
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

#ifndef IJKPLAYER_ANDROID__IJKPLAYER_ANDROID_H
#define IJKPLAYER_ANDROID__IJKPLAYER_ANDROID_H

#include <jni.h>
#include "ijkplayer_android_def.h"
#include "../ijkplayer.h"

typedef struct emmp_android_media_format_context {
    const char *mime_type;
    int         profile;
    int         level;
} emmp_android_media_format_context;

// ref_count is 1 after open
EMMediaPlayer *emmp_android_create(int(*msg_loop)(void*));
void emmp_android_present_decode_buffer(JNIEnv *env, EMMediaPlayer *mp, int color, jbyteArray frame, int width, int height);
void emmp_android_set_surface(JNIEnv *env, EMMediaPlayer *mp, jobject android_surface);
void emmp_android_set_volume(JNIEnv *env, EMMediaPlayer *mp, float left, float right);
int  emmp_android_get_audio_session_id(JNIEnv *env, EMMediaPlayer *mp);
void emmp_android_set_mediacodec_select_callback(EMMediaPlayer *mp, bool (*callback)(void *opaque, emmp_mediacodecinfo_context *mcc), void *opaque);

#endif
