
#----------
# modify for your build tool

SSL_ALL_ARCHS_IOS6_SDK="armv7 armv7s i386"
SSL_ALL_ARCHS_IOS7_SDK="armv7 armv7s arm64 i386 x86_64"
SSL_ALL_ARCHS_IOS8_SDK="armv7 arm64 i386 x86_64"

SSL_ALL_ARCHS=$SSL_ALL_ARCHS_IOS8_SDK

#----------
UNI_BUILD_ROOT=`pwd`
UNI_TMP="$UNI_BUILD_ROOT/tmp"
UNI_TMP_LLVM_VER_FILE="$UNI_TMP/llvm.ver.txt"
SSL_TARGET=$1
SSL_TARGET_EXTRA=$2
set -e

#----------
echo_archs() {
    echo "===================="
    echo "[*] check xcode version"
    echo "===================="
    echo "SSL_ALL_ARCHS = $SSL_ALL_ARCHS"
}



#----------
if [ "$SSL_TARGET" = "armv7" -o "$SSL_TARGET" = "armv7s" -o "$SSL_TARGET" = "arm64" ]; then
    echo_archs
    sh tools/do-compile-openssl.sh $SSL_TARGET $SSL_TARGET_EXTRA
elif [ "$SSL_TARGET" = "i386" -o "$SSL_TARGET" = "x86_64" ]; then
    echo_archs
    sh tools/do-compile-openssl.sh $SSL_TARGET $SSL_TARGET_EXTRA
elif [ "$SSL_TARGET" = "lipo" ]; then
    echo_archs
elif [ "$SSL_TARGET" = "all" ]; then
    echo_archs
    for ARCH in $SSL_ALL_ARCHS
    do
        sh tools/do-compile-openssl.sh $ARCH $SSL_TARGET_EXTRA
    done

elif [ "$SSL_TARGET" = "check" ]; then
    echo_archs
elif [ "$SSL_TARGET" = "clean" ]; then
    echo_archs
    for ARCH in $SSL_ALL_ARCHS
    do
        cd openssl-$ARCH && git clean -xdf && cd -
    done
    rm -rf openssl-*
    rm -rf build/openssl-*
    rm -rf build/universal/include
    rm -rf build/universal/lib
else
    echo "Usage:"
    echo "  compile-openssl.sh armv7|arm64|i386|x86_64"
    echo "  compile-openssl.sh armv7s (obselete)"
    echo "  compile-openssl.sh lipo"
    echo "  compile-openssl.sh all"
    echo "  compile-openssl.sh clean"
    echo "  compile-openssl.sh check"
    exit 1
fi
