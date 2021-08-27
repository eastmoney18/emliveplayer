# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_VERBOSE_MAKEFILE on)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -DNO_CRYPTO")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -frtti -fexceptions -Wall")

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(3RDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../3rdparty)
set(SO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../distribution)

set(SOURCE_FILES
        ${SOURCE_DIR}/jni_interface.cpp
)

add_library( # Sets the name of the library.
        emlivenative

        # Sets the library as a shared library.
        SHARED #STATIC

        # Provides a relative path to your source file(s).
        ${SOURCE_FILES})

target_link_libraries( # Specifies the target library.
        emlivenative

        log
        android
        c
        m
        z
        EGL
        GLESv2
        OpenSLES
        ${SO_DIR}/ijkplayer/lib/${ANDROID_ABI}/libijkplayer.a
        ${3RDPARTY_DIR}/${ANDROID_ABI}/lib/libijkffmpeg.a
        ${3RDPARTY_DIR}/${ANDROID_ABI}/lib/libssl.a
        ${3RDPARTY_DIR}/${ANDROID_ABI}/lib/libcrypto.a
        )

