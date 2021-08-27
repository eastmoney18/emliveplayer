#include <jni.h>
#include <unistd.h>

extern "C" {

extern jint IJK_JNI_OnLoad(JavaVM *vm, void *reserved);

#ifndef BUILD_PUSH_ONLY

extern jint JNICALL SDL_JNI_OnLoad(JavaVM *vm, void *reserved);

extern void IJK_JNI_OnUnload(JavaVM *jvm, void *reserved);

extern void JNICALL SDL_JNI_OnUnload(JavaVM *jvm, void *reserved);

#endif


jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    jint ret = 0;

#ifndef BUILD_PUSH_ONLY
    IJK_JNI_OnLoad(vm, reserved);
    return SDL_JNI_OnLoad(vm, reserved);
#else
    return ret;
#endif
}

void JNICALL JNI_OnUnload(JavaVM *jvm, void *reserved) {

#ifndef BUILD_PUSH_ONLY
    IJK_JNI_OnUnload(jvm, reserved);
    SDL_JNI_OnUnload(jvm, reserved);
#endif
}
}
