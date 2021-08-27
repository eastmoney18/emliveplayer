# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_VERBOSE_MAKEFILE on)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -frtti -fexceptions -Wall")

if (CMAKE_HOST_UNIX)
    set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ijknative/ijkmedia)
else()
    set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../common/ijknative/ijkmedia)
endif()

set(3RDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rdparty/${ANDROID_ABI}/include)

include_directories(
        ${SOURCE_DIR}
        ${SOURCE_DIR}/ijkj4a
        ${SOURCE_DIR}/ijkplayer
        ${SOURCE_DIR}/ijksdl
        ${SOURCE_DIR}/ijkyuv
        ${SOURCE_DIR}/ijkyuv/include
        ${3RDPARTY_DIR}/ffmpeg
        ${3RDPARTY_DIR}/openssl
)

set(SOURCE_FILES
        ${SOURCE_DIR}/../android-ndk-prof/prof.c
        ${SOURCE_DIR}/ijkj4a/j4a/j4a_allclasses.c
        ${SOURCE_DIR}/ijkj4a/j4a/j4a_base.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/media/AudioTrack.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/media/MediaCodec.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/media/MediaFormat.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/media/PlaybackParams.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/os/Build.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/android/os/Bundle.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/java/nio/Buffer.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/java/nio/ByteBuffer.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/java/util/ArrayList.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/tv/danmaku/ijk/media/player/misc/IMediaDataSource.c
        ${SOURCE_DIR}/ijkj4a/j4a/class/tv/danmaku/ijk/media/player/IjkMediaPlayer.c
        ${SOURCE_DIR}/ijkj4a/j4au/class/android/media/AudioTrack.util.c
        ${SOURCE_DIR}/ijkj4a/j4au/class/java/nio/ByteBuffer.util.c
        ${SOURCE_DIR}/ijkplayer/ff_cmdutils.c
        ${SOURCE_DIR}/ijkplayer/ff_ffplay.c
        ${SOURCE_DIR}/ijkplayer/ff_ffpipeline.c
        ${SOURCE_DIR}/ijkplayer/ff_ffpipenode.c
        ${SOURCE_DIR}/ijkplayer/ijkmeta.c
        ${SOURCE_DIR}/ijkplayer/ijkplayer.c
        ${SOURCE_DIR}/ijkplayer/ijkutil.c
        ${SOURCE_DIR}/ijkplayer/ijksonic.c
        ${SOURCE_DIR}/ijkplayer/pipeline/ffpipeline_ffplay.c
        ${SOURCE_DIR}/ijkplayer/pipeline/ffpipenode_ffplay_vdec.c
        ${SOURCE_DIR}/ijkplayer/android/ffmpeg_api_jni.c
        ${SOURCE_DIR}/ijkplayer/android/ijkplayer_android.c
        ${SOURCE_DIR}/ijkplayer/android/ijkplayer_jni.c
        ${SOURCE_DIR}/ijkplayer/android/pipeline/ffpipeline_android.c
        ${SOURCE_DIR}/ijkplayer/android/pipeline/ffpipenode_android_mediacodec_vdec.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/allformats.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijklivehook.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijkmediadatasource.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijkasync.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijkurlhook.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijklongurl.c
        ${SOURCE_DIR}/ijkplayer/ijkavformat/ijksegment.c
        ${SOURCE_DIR}/ijksdl/ijksdl_aout.c
        ${SOURCE_DIR}/ijksdl/ijksdl_audio.c
        ${SOURCE_DIR}/ijksdl/ijksdl_egl.c
        ${SOURCE_DIR}/ijksdl/ijksdl_error.c
        ${SOURCE_DIR}/ijksdl/ijksdl_log.c
        ${SOURCE_DIR}/ijksdl/ijksdl_mutex.c
        ${SOURCE_DIR}/ijksdl/ijksdl_stdinc.c
        ${SOURCE_DIR}/ijksdl/ijksdl_thread.c
        ${SOURCE_DIR}/ijksdl/ijksdl_timer.c
        ${SOURCE_DIR}/ijksdl/ijksdl_vout.c
        ${SOURCE_DIR}/ijksdl/gles2/color.c
        ${SOURCE_DIR}/ijksdl/gles2/common.c
        ${SOURCE_DIR}/ijksdl/gles2/renderer.c
        ${SOURCE_DIR}/ijksdl/gles2/renderer_rgb.c
        ${SOURCE_DIR}/ijksdl/gles2/renderer_yuv420p.c
        ${SOURCE_DIR}/ijksdl/gles2/renderer_yuv444p10le.c
        ${SOURCE_DIR}/ijksdl/gles2/shader.c
        ${SOURCE_DIR}/ijksdl/gles2/fsh/rgb.fsh.c
        ${SOURCE_DIR}/ijksdl/gles2/fsh/yuv420p.fsh.c
        ${SOURCE_DIR}/ijksdl/gles2/fsh/yuv444p10le.fsh.c
        ${SOURCE_DIR}/ijksdl/gles2/vsh/mvp.vsh.c
        ${SOURCE_DIR}/ijksdl/dummy/ijksdl_vout_dummy.c
        ${SOURCE_DIR}/ijksdl/fastimage/fastimage_vout.c
        ${SOURCE_DIR}/ijksdl/ffmpeg/ijksdl_vout_overlay_ffmpeg.c
        ${SOURCE_DIR}/ijksdl/ffmpeg/abi_all/image_convert.c
        ${SOURCE_DIR}/ijksdl/android/android_audiotrack.c
        ${SOURCE_DIR}/ijksdl/android/android_nativewindow.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_android_jni.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_aout_android_audiotrack.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_aout_android_opensles.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediacodec_dummy.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediacodec_internal.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediacodec_java.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediacodec.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediadef.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediaformat_java.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_codec_android_mediaformat.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_vout_android_nativewindow.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_vout_android_surface.c
        ${SOURCE_DIR}/ijksdl/android/ijksdl_vout_overlay_android_mediacodec.c
        ${SOURCE_DIR}/ijkyuv/source/compare.cc
        ${SOURCE_DIR}/ijkyuv/source/compare_common.cc
        ${SOURCE_DIR}/ijkyuv/source/compare_posix.cc
        ${SOURCE_DIR}/ijkyuv/source/convert.cc
        ${SOURCE_DIR}/ijkyuv/source/convert_argb.cc
        ${SOURCE_DIR}/ijkyuv/source/convert_from.cc
        ${SOURCE_DIR}/ijkyuv/source/convert_from_argb.cc
        ${SOURCE_DIR}/ijkyuv/source/convert_to_argb.cc
        ${SOURCE_DIR}/ijkyuv/source/convert_to_i420.cc
        ${SOURCE_DIR}/ijkyuv/source/cpu_id.cc
        ${SOURCE_DIR}/ijkyuv/source/format_conversion.cc
        ${SOURCE_DIR}/ijkyuv/source/planar_functions.cc
        ${SOURCE_DIR}/ijkyuv/source/rotate.cc
        ${SOURCE_DIR}/ijkyuv/source/rotate_argb.cc
        ${SOURCE_DIR}/ijkyuv/source/rotate_mips.cc
        ${SOURCE_DIR}/ijkyuv/source/row_any.cc
        ${SOURCE_DIR}/ijkyuv/source/row_common.cc
        ${SOURCE_DIR}/ijkyuv/source/row_mips.cc
        ${SOURCE_DIR}/ijkyuv/source/row_posix.cc
        ${SOURCE_DIR}/ijkyuv/source/scale.cc
        ${SOURCE_DIR}/ijkyuv/source/scale_argb.cc
        ${SOURCE_DIR}/ijkyuv/source/scale_common.cc
        ${SOURCE_DIR}/ijkyuv/source/scale_mips.cc
        ${SOURCE_DIR}/ijkyuv/source/scale_posix.cc
        ${SOURCE_DIR}/ijkyuv/source/video_common.cc
        )

if(ANDROID_ABI STREQUAL arm64-v8a)
    add_definitions(-DLIBYUV_NEON)
    set(SOURCE_FILES
            ${SOURCE_FILES}
            ${SOURCE_DIR}/ijkyuv/source/compare_neon64.cc
            ${SOURCE_DIR}/ijkyuv/source/rotate_neon64.cc
            ${SOURCE_DIR}/ijkyuv/source/row_neon64.cc
            ${SOURCE_DIR}/ijkyuv/source/scale_neon64.cc
            )
endif()

add_library( # Sets the name of the library.
        ijkplayer

        # Sets the library as a shared library.
        STATIC # SHARED #STATIC

        # Provides a relative path to your source file(s).
        ${SOURCE_FILES})

set(distribution_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../distribution)

set_target_properties(ijkplayer
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
        "${distribution_DIR}/ijkplayer/lib/${ANDROID_ABI}")