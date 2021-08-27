#!/bin/bash
FF_ARCH=$1
FF_BUILD_PATH=ffmpeg-$FF_ARCH
echo $FF_BUILD_PATH
if [ ! -d "$FF_BUILD_PATH" ]; then  
    cp -rf ../ffmpeg-src $FF_BUILD_PATH
fi  

