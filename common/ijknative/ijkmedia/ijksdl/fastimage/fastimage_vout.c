//
// Created by 陈海东 on 17/2/23.
//
#include "fastimage_vout.h"
#include "../ijksdl_vout_internal.h"
#include "../ijksdl_vout.h"
#include "ijksdl/ffmpeg/ijksdl_vout_overlay_ffmpeg.h"
#include "ijksdl/android/ijksdl_vout_overlay_android_mediacodec.h"

typedef struct SDL_VoutFastImage_Opaque {
    SDL_Vout *vout;
} SDL_VoutFastImage_Opaque;


typedef struct SDL_Vout_Opaque {
    SDL_AMediaCodec *acodec;

} SDL_Vout_Opaque;


static void func_free_l(SDL_Vout *vout)
{
    if (!vout)
        return;

    SDL_Vout_Opaque *opaque = vout->opaque;
    if (opaque) {
    }

    SDL_Vout_FreeInternal(vout);
}

static int func_display_overlay_l(SDL_Vout *vout, SDL_VoutOverlay *overlay)
{
    ALOGE("%s: display overlay...", __func__);
    return 0;
}

static int func_display_overlay(SDL_Vout *vout, SDL_VoutOverlay *overlay)
{
    SDL_LockMutex(vout->mutex);
    int retval = func_display_overlay_l(vout, overlay);
    SDL_UnlockMutex(vout->mutex);
    return retval;
}

SDL_VoutOverlay * func_create_overlay_l(int width, int height, int frame_format, SDL_Vout *vout)
{
    ALOGE("%s: create overlay...", __func__);
    return NULL;
    /*switch (frame_format) {
        case IJK_AV_PIX_FMT__ANDROID_MEDIACODEC:
            return SDL_VoutAMediaCodec_CreateOverlay(width, height, vout);
        default:
            return SDL_VoutFFmpeg_CreateOverlay(width, height, frame_format, vout);
    }*/
}

SDL_VoutOverlay * func_create_overlay(int width, int height, int frame_format, SDL_Vout *vout)
{
    SDL_LockMutex(vout->mutex);
    SDL_VoutOverlay *ret = func_create_overlay_l(width, height, frame_format, vout);
    SDL_UnlockMutex(vout->mutex);
    return ret;
}

SDL_Vout * SDL_VoutFastImage_Create()
{
    SDL_Vout *vout = SDL_Vout_CreateInternal(sizeof(SDL_Vout_Opaque));
    if (!vout)
        return NULL;

    SDL_Vout_Opaque *opaque = vout->opaque;
    memset(opaque, 0, sizeof(SDL_Vout_Opaque));
    vout->free_l = func_free_l;
    vout->display_overlay = func_display_overlay;
    vout->create_overlay = func_create_overlay;
    return vout;
}