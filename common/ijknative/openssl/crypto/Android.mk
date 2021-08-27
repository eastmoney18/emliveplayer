LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := crypto
LOCAL_SRC_FILES := $(MY_APP_OPENSSL_OUTPUT_PATH)/libcrypto.a
include $(PREBUILT_STATIC_LIBRARY)
