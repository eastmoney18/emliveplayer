#!/bin/bash
SSL_ARCH=$1
SSL_BUILD_PATH=openssl-$SSL_ARCH
echo $SSL_BUILD_PATH
if [ ! -d "$SSL_BUILD_PATH" ]; then  
    cp -rf ../openssl-src $SSL_BUILD_PATH
fi  

