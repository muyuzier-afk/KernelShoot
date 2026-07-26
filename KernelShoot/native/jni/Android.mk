# KernelShoot - Android.mk
# 构建原生守护进程可执行文件 KernelShoot_daemon
#
# 目录布局 (LOCAL_PATH = native/jni):
#   native/jni/Android.mk        (本文件)
#   native/jni/Application.mk
#   native/jni/*.c               (源码)
#   native/jni/include/          (头文件)
#   native/prebuilt/arm64-v8a/   (预编译静态库)
#      libturbojpeg.a  libdrm.a  include/
#
# 依赖预编译静态库:
#   libjpeg-turbo -> libturbojpeg.a  + turbojpeg.h
#   libdrm        -> libdrm.a        + xf86drm*.h / libdrm/*.h
#
# 用法: cd native && ndk-build

LOCAL_PATH := $(call my-dir)
PREBUILT_DIR := $(LOCAL_PATH)/../prebuilt/$(TARGET_ARCH_ABI)

# ---------- libjpeg-turbo (静态) ----------
include $(CLEAR_VARS)
LOCAL_MODULE := turbojpeg
LOCAL_SRC_FILES := $(PREBUILT_DIR)/libturbojpeg.a
LOCAL_EXPORT_C_INCLUDES := $(PREBUILT_DIR)/include
include $(PREBUILT_STATIC_LIBRARY)

# ---------- libdrm (静态) ----------
include $(CLEAR_VARS)
LOCAL_MODULE := drm_static
LOCAL_SRC_FILES := $(PREBUILT_DIR)/libdrm.a
LOCAL_EXPORT_C_INCLUDES := \
    $(PREBUILT_DIR)/include \
    $(PREBUILT_DIR)/include/libdrm \
    $(PREBUILT_DIR)/include/drm
include $(PREBUILT_STATIC_LIBRARY)

# ---------- KernelShoot_daemon 可执行文件 ----------
include $(CLEAR_VARS)
LOCAL_MODULE            := KernelShoot_daemon
LOCAL_C_INCLUDES        := $(LOCAL_PATH)/include
LOCAL_SRC_FILES         := \
    main.c \
    util.c \
    daemon_init.c \
    socket_server.c \
    foreground_detector.c \
    fb_screenshot.c \
    drm_screenshot.c \
    jpeg_encoder.c \
    file_writer.c

LOCAL_CFLAGS            := -Wall -Wextra -O2 -fPIE -DANDROID
LOCAL_LDFLAGS           := -fPIE -pie -Wl,--gc-sections
LOCAL_LDLIBS            := -llog
LOCAL_STATIC_LIBRARIES  := turbojpeg drm_static

include $(BUILD_EXECUTABLE)
