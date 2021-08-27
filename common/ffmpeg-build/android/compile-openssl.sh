# This script is based on projects below
# https://github.com/yixia/FFmpeg-Android
# http://git.videolan.org/?p=vlc-ports/android.git;a=summary

#----------
UNI_BUILD_ROOT=`pwd`
FF_TARGET=$1
FF_TARGET_EXTRA=$2
set -e
set +x

FF_ACT_ARCHS_32="armv5 armv7a x86"
FF_ACT_ARCHS_64="armv5 armv7a arm64 x86 x86_64"
FF_ACT_ARCHS_ALL=$FF_ACT_ARCHS_64

echo_archs() {
    echo "===================="
    echo "[*] check archs"
    echo "===================="
    echo "FF_ALL_ARCHS = $FF_ACT_ARCHS_ALL"
    echo "FF_ACT_ARCHS = $*"
    echo ""
}

echo_usage() {
    echo "Usage:"
    echo "  compile-openssl.sh armv5|armv7a|arm64|x86|x86_64"
    echo "  compile-openssl.sh all|all32"
    echo "  compile-openssl.sh all64"
    echo "  compile-openssl.sh clean"
    echo "  compile-openssl.sh check"
    exit 1
}

echo_nextstep_help() {
    echo ""
    echo "--------------------"
    echo "[*] Finished"
    echo "--------------------"
    echo "# to continue to build ijkplayer, run script below,"
    echo "sh compile-ffmpeg.sh "
}

#----------
case "$FF_TARGET" in
    "")
        echo_archs armv7a
        sh tools/do-compile-openssl.sh armv7a
    ;;
    armv5|armv7a|arm64|x86|x86_64)
        echo_archs $FF_TARGET $FF_TARGET_EXTRA
        sh tools/do-compile-openssl.sh $FF_TARGET $FF_TARGET_EXTRA
        #echo_nextstep_help
    ;;
    all32)
        echo_archs $FF_ACT_ARCHS_32
        for ARCH in $FF_ACT_ARCHS_32
        do
            sh tools/do-compile-openssl.sh $ARCH $FF_TARGET_EXTRA
        done
        echo_nextstep_help
    ;;
    all|all64)
        echo_archs $FF_ACT_ARCHS_64
        for ARCH in $FF_ACT_ARCHS_64
        do
            sh tools/do-compile-openssl.sh $ARCH $FF_TARGET_EXTRA
        done
        echo_nextstep_help
    ;;
    clean)
        echo_archs FF_ACT_ARCHS_64
        for ARCH in $FF_ACT_ARCHS_ALL
        do
            if [ -d openssl-$ARCH ]; then
                cd openssl-$ARCH && git clean -xdf && cd -
            fi
        done
        rm -rf ./openssl-*
        rm -rf ./build/openssl-*
    ;;
    check)
        echo_archs FF_ACT_ARCHS_ALL
    ;;
    *)
        echo_usage
        exit 1
    ;;
esac
