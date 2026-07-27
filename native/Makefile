# KernelShoot - Makefile (独立交叉编译, 不依赖 ndk-build)
# 目标: arm64-v8a 静态可执行 KernelShoot_daemon
#
# 前置:
#   1. 设置 ANDROID_NDK 环境变量指向 NDK 根目录
#   2. 在 prebuilt/arm64-v8a/ 放好 libturbojpeg.a / libdrm.a / include/
#      (见 build_libs.sh 脚本说明)
#
# 用法:
#   export ANDROID_NDK=/path/to/ndk
#   make            # 构建
#   make strip      # 去符号
#   make clean

NDK           ?= $(ANDROID_NDK)
ifeq ($(NDK),)
$(error ANDROID_NDK 未设置, 例如: export ANDROID_NDK=$$HOME/Android/Sdk/ndk/26.1.10909125)
endif

API           := 28
ARCH          := arm64
ABI           := arm64-v8a
HOST_TAG      := linux-x86_64

TOOLCHAIN     := $(NDK)/toolchains/llvm/$(HOST_TAG)
SYSROOT       := $(TOOLCHAIN)/sysroot

CC            := $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang
STRIP         := $(TOOLCHAIN)/bin/llvm-strip

SRC_DIR       := jni
INC_DIR       := jni/include
PREBUILT      := prebuilt/$(ABI)
BUILD_DIR     := build/$(ABI)
TARGET        := $(BUILD_DIR)/KernelShoot_daemon

CFLAGS        := -Wall -Wextra -O2 -fPIE -DANDROID \
                 -I$(INC_DIR) \
                 -I$(PREBUILT)/include \
                 -I$(PREBUILT)/include/libdrm \
                 -I$(PREBUILT)/include/drm
LDFLAGS       := -fPIE -pie -Wl,--gc-sections \
                 -L$(PREBUILT)
LDLIBS        := -lturbojpeg -ldrm -llog

SRCS          := $(wildcard $(SRC_DIR)/*.c)
# selinux_check.c 已被 wildcard 自动包含
OBJS          := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all strip clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	@echo "==> built $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	@mkdir -p $@

strip: $(TARGET)
	$(STRIP) --strip-all $(TARGET)
	@echo "==> stripped $(TARGET)"

clean:
	rm -rf build
