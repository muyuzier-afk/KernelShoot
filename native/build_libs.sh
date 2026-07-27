#!/usr/bin/env bash
# KernelShoot - build_libs.sh
# 交叉编译 libjpeg-turbo 与 libdrm 静态库, 输出到 prebuilt/arm64-v8a/
#
# 前置: export ANDROID_NDK=/path/to/ndk
#       需 cmake (libjpeg-turbo 用 cmake), meson+ninja (libdrm 用 meson)
#
# 产出目录结构:
#   prebuilt/arm64-v8a/
#     libturbojpeg.a
#     libdrm.a
#     include/
#       turbojpeg.h
#       xf86drm.h  xf86drmMode.h  xf86drm.h
#       libdrm/*.h  drm/*.h
set -euo pipefail

NDK="${ANDROID_NDK:?请设置 ANDROID_NDK}"
ABI=arm64-v8a
API=28
ARCH=arm64
HOST_TAG=linux-x86_64
# NDK 标准布局: $NDK/toolchains/llvm/prebuilt/<host>/
# 部分老版 NDK 没有 prebuilt 层, 兼容两种布局
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
[ -d "$TOOLCHAIN" ] || TOOLCHAIN="$NDK/toolchains/llvm/$HOST_TAG"
SYSROOT="$TOOLCHAIN/sysroot"
CC="$TOOLCHAIN/bin/aarch64-linux-android$API-clang"
AR="$TOOLCHAIN/bin/llvm-ar"

ROOT="$(cd "$(dirname "$0")" && pwd)"
PREBUILT="$ROOT/prebuilt/$ABI"
WORK="$ROOT/build/_libs"
SRC="$ROOT/build/_src"

mkdir -p "$PREBUILT/include" "$WORK" "$SRC"

echo "==> [1/2] libjpeg-turbo"
LIBJPEG_VER=3.0.0
if [ ! -d "$SRC/libjpeg-turbo-$LIBJPEG_VER" ]; then
  curl -fsSL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$LIBJPEG_VER/libjpeg-turbo-$LIBJPEG_VER.tar.gz" \
    | tar -xz -C "$SRC"
fi
cmake -S "$SRC/libjpeg-turbo-$LIBJPEG_VER" -B "$WORK/libjpeg" \
  -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_SYSTEM_PROCESSOR=$ARCH \
  -DCMAKE_ANDROID_NDK="$NDK" \
  -DCMAKE_ANDROID_ARCH=$ARCH \
  -DCMAKE_ANDROID_API=$API \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_AR="$AR" \
  -DENABLE_SHARED=OFF \
  -DENABLE_STATIC=ON \
  -DWITH_TURBOJPEG=ON \
  -DCMAKE_INSTALL_PREFIX="$PREBUILT"
cmake --build "$WORK/libjpeg" -j"$(nproc)"
cmake --install "$WORK/libjpeg"
# libjpeg-turbo 安装到 lib/, 拷贝一份保证 Makefile 找到
cp -f "$PREBUILT/lib/libturbojpeg.a" "$PREBUILT/" 2>/dev/null || true
cp -f "$PREBUILT/lib/libjpeg.a" "$PREBUILT/" 2>/dev/null || true

echo "==> [2/2] libdrm"
LIBDRM_VER=2.4.115
if [ ! -d "$SRC/libdrm-$LIBDRM_VER" ]; then
  curl -fsSL "https://dri.freedesktop.org/libdrm/libdrm-$LIBDRM_VER.tar.xz" \
    | tar -xJ -C "$SRC"
fi
# 用 meson 交叉编译
cat > "$WORK/libdrm-cross.txt" <<EOF
[binaries]
c = '$CC'
ar = '$AR'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
meson setup "$SRC/libdrm-$LIBDRM_VER" "$WORK/libdrm" \
  --cross-file "$WORK/libdrm-cross.txt" \
  --default-library=static \
  --prefix="$PREBUILT" \
  -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled \
  -Dnouveau=disabled -Dvmwgfx=disabled -Domap=disabled \
  -Dexynos=disabled -Dfreedreno=disabled -Dtegra=disabled \
  -Dvc4=disabled -Detnaviv=disabled -Dsysmans=disabled
ninja -C "$WORK/libdrm" install
# 拷到 PREBUILT 根方便 -L 查找
cp -f "$PREBUILT/lib/libdrm.a" "$PREBUILT/" 2>/dev/null || true
# 头文件归位: include/libdrm/, include/drm/
mkdir -p "$PREBUILT/include/libdrm" "$PREBUILT/include/drm"
cp -f "$PREBUILT"/include/xf86drm*.h "$PREBUILT/include/" 2>/dev/null || true
cp -f "$PREBUILT"/include/libdrm/*.h "$PREBUILT/include/libdrm/" 2>/dev/null || true
cp -f "$PREBUILT"/include/drm/*.h "$PREBUILT/include/drm/" 2>/dev/null || true

echo "==> 完成. 静态库位于 $PREBUILT"
echo "    现在可执行: make -C $ROOT"
