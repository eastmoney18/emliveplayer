#!/bin/bash
CONFIGURATION_NAME=$1
if [[ $CONFIGURATION_NAME != "Debug" ]] && [[ $CONFIGURATION_NAME != "Release" ]];then
    echo "must specify one configuration, Debug or Release!"
    exit 1
fi

BUILD_PATH=`pwd`/build
mkdir -p $BUILD_PATH
PRODUCT_NAME=$2
if test -z $PRODUCT_NAME;then
    echo "not net product name, default build EMLiveSDKIOS"
    PRODUCT_NAME=EMLiveSDKIOS
fi

PRODUCT_PATH_IOS=${BUILD_PATH}/${CONFIGURATION_NAME}-iphoneos/${PRODUCT_NAME}.framework
PRODUCT_PATH_SIMULATOR=${BUILD_PATH}/${CONFIGURATION_NAME}-iphonesimulator/${PRODUCT_NAME}.framework

echo iphoneos simulator version
echo ======================================
echo build EMLiveSDKIOS
xcodebuild -configuration $CONFIGURATION_NAME -scheme ${PRODUCT_NAME} -quiet SYMROOT=$BUILD_PATH -sdk iphonesimulator

echo iphoneos version
echo =====================================
xcodebuild -configuration $CONFIGURATION_NAME -scheme ${PRODUCT_NAME} -quiet SYMROOT=$BUILD_PATH -sdk iphoneos

echo =====================================
echo build end=============

echo lipo iphoneos and iphoneSimulator============================

cp -rf ${PRODUCT_PATH_IOS} ${BUILD_PATH}/
cp -rf ${MEDIALIVELIB_PATH_IOS} ${BUILD_PATH}/
cp -rf ${MEDIALIVEIMAGE_PATH_IOS} ${BUILD_PATH}/

lipo -create ${PRODUCT_PATH_IOS}/${PRODUCT_NAME} ${PRODUCT_PATH_SIMULATOR}/${PRODUCT_NAME} -output ${BUILD_PATH}/${PRODUCT_NAME}.framework/${PRODUCT_NAME}

#zip -9 -r ${BUILD_PATH}/${PRODUCT_NAME}.framework/${PRODUCT_NAME}.zip ${BUILD_PATH}/${PRODUCT_NAME}.framework/${PRODUCT_NAME}

echo build finished================================
