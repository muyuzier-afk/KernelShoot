#!/usr/bin/env bash
# KernelShoot - ci_build.sh
# GitHub Actions 专用构建脚本
# 在 ubuntu runner 上完成: 静态库 -> 守护进程 -> APK -> 模块 zip
#
# 与 build.sh 区别: 不依赖宿主预装 NDK/SDK, 全部由 workflow 注入环境变量
# 输出统一放在 dist/, 供后续 release 上传
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

NDK="${ANDROID_NDK:?ANDROID_NDK not set}"
# 兼容 ANDROID_HOME / ANDROID_SDK_ROOT 两种变量名
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$SDK" ] || { echo "ANDROID_HOME or ANDROID_SDK_ROOT not set" >&2; exit 1; }
export ANDROID_HOME="$SDK" ANDROID_SDK_ROOT="$SDK"
ABI=arm64-v8a
API=28

log()  { echo -e "\033[1;34m[ci]\033[0m $*"; }
fail() { echo -e "\033[1;31m[ci-error]\033[0m $*" >&2; exit 1; }

# ---------- 0. 工具链准备 ----------
# NDK 标准布局: $NDK/toolchains/llvm/prebuilt/<host>/bin/
# host 在 Linux 上是 linux-x86_64
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
[ -d "$TOOLCHAIN" ] || TOOLCHAIN="$NDK/toolchains/llvm/linux-x86_64"
export PATH="$TOOLCHAIN/bin:$PATH"
CC="$TOOLCHAIN/bin/aarch64-linux-android$API-clang"
AR="$TOOLCHAIN/bin/llvm-ar"
[ -x "$CC" ] || fail "clang not found: $CC (searched $TOOLCHAIN/bin)"

PREBUILT="$ROOT/native/prebuilt/$ABI"
WORK="$ROOT/build/_ci"
DIST="$ROOT/dist"
mkdir -p "$PREBUILT/include/libdrm" "$PREBUILT/include/drm" "$WORK" "$DIST"

# ---------- 1. libjpeg-turbo 静态库 ----------
build_libjpeg() {
    log "building libjpeg-turbo"
    local SRC="$WORK/libjpeg-turbo-3.0.0"
    if [ ! -d "$SRC" ]; then
        curl -fsSL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.0.0/libjpeg-turbo-3.0.0.tar.gz" \
            | tar -xz -C "$WORK"
    fi
    cmake -S "$SRC" -B "$WORK/libjpeg" \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_ANDROID_NDK="$NDK" \
        -DCMAKE_ANDROID_ARCH=arm64 \
        -DCMAKE_ANDROID_API="$API" \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_AR="$AR" \
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
        -DWITH_TURBOJPEG=ON \
        -DCMAKE_INSTALL_PREFIX="$PREBUILT"
    cmake --build "$WORK/libjpeg" -j"$(nproc)"
    cmake --install "$WORK/libjpeg"
    cp -f "$PREBUILT/lib/libturbojpeg.a" "$PREBUILT/" 2>/dev/null || true
}

# ---------- 2. libdrm 静态库 ----------
build_libdrm() {
    log "building libdrm"
    local SRC="$WORK/libdrm-2.4.115"
    if [ ! -d "$SRC" ]; then
        curl -fsSL "https://dri.freedesktop.org/libdrm/libdrm-2.4.115.tar.xz" \
            | tar -xJ -C "$WORK"
    fi
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
    meson setup "$SRC" "$WORK/libdrm" \
        --cross-file "$WORK/libdrm-cross.txt" \
        --default-library=static \
        --prefix="$PREBUILT" \
        -Dauto_features=disabled \
        -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled \
        -Dnouveau=disabled -Dvmwgfx=disabled -Domap=disabled \
        -Dexynos=disabled -Dfreedreno=disabled -Dtegra=disabled \
        -Dvc4=disabled -Detnaviv=disabled \
        -Dcairo-tests=disabled -Dman-pages=disabled \
        -Dvalgrind=disabled -Dtests=false
    ninja -C "$WORK/libdrm" install
    cp -f "$PREBUILT/lib/libdrm.a" "$PREBUILT/" 2>/dev/null || true
    cp -f "$PREBUILT"/include/xf86drm*.h "$PREBUILT/include/" 2>/dev/null || true
    cp -f "$PREBUILT"/include/libdrm/*.h "$PREBUILT/include/libdrm/" 2>/dev/null || true
    cp -f "$PREBUILT"/include/drm/*.h "$PREBUILT/include/drm/" 2>/dev/null || true
}

# ---------- 3. 守护进程 ----------
build_daemon() {
    log "building KernelShoot_daemon"
    make -C "$ROOT/native" clean >/dev/null 2>&1 || true
    ANDROID_NDK="$NDK" make -C "$ROOT/native" all
    ANDROID_NDK="$NDK" make -C "$ROOT/native" strip
    local BIN="$ROOT/native/build/$ABI/KernelShoot_daemon"
    [ -f "$BIN" ] || fail "daemon build failed"
    file "$BIN"
}

# ---------- 4. APK ----------
build_apk() {
    log "building APK"
    cd "$ROOT"
    # 强制使用项目自带的 gradle wrapper (version 与 AGP 匹配, 不依赖 runner 系统 gradle)
    [ -x "./gradlew" ] || fail "gradlew not found in repo root"
    chmod +x ./gradlew
    ./gradlew :app:assembleRelease --no-daemon -x lint
    local APK="$ROOT/app/build/outputs/apk/release/app-release-unsigned.apk"
    [ -f "$APK" ] || fail "apk build failed"
    cp -f "$APK" "$WORK/KernelShoot-unsigned.apk"
}

# ---------- 4.5 给 APK 签名 ----------
# assembleRelease 默认产出的是 -unsigned.apk, 直接 pm install 会
# 报 INSTALL_PARSE_FAILED_NO_CERTIFICATES. 用本地生成的自签 keystore
# 走 zipalign + apksigner 签名一次.
sign_apk() {
    log "signing APK"
    local UNSIGNED="$WORK/KernelShoot-unsigned.apk"
    local ALIGNED="$WORK/KernelShoot-aligned.apk"
    local SIGNED="$WORK/KernelShoot.apk"
    local KEYSTORE="$WORK/keystore.jks"
    # 选最新 build-tools, 避免硬编码版本
    local BTDIR
    BTDIR="$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)"
    [ -n "$BTDIR" ] || fail "no build-tools found under $SDK/build-tools"
    local APKSIGNER="$BTDIR/apksigner"
    local ZIPALIGN="$BTDIR/zipalign"
    [ -x "$APKSIGNER" ] || fail "apksigner not found: $APKSIGNER"
    [ -x "$ZIPALIGN" ]  || fail "zipalign not found: $ZIPALIGN"

    # 首次构建生成 keystore, 后续复用 (确保模块可重复发布, 签名一致)
    if [ ! -f "$KEYSTORE" ]; then
        keytool -genkeypair -v -keystore "$KEYSTORE" -alias kernelshoot \
            -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass changeit -keypass changeit \
            -dname "CN=KernelShoot, OU=Magisk, O=KernelShoot, L=NA, ST=NA, C=CN" \
            >/dev/null 2>&1 \
            || fail "keytool genkeypair failed"
    fi

    "$ZIPALIGN"  -f -p 4 "$UNSIGNED" "$ALIGNED"
    "$APKSIGNER" sign \
        --ks "$KEYSTORE" \
        --ks-pass pass:changeit \
        --key-pass pass:changeit \
        --min-sdk-version 26 \
        --out "$SIGNED" "$ALIGNED" \
        || fail "apksigner sign failed"
    "$APKSIGNER" verify --print-certs "$SIGNED" >/dev/null \
        || fail "apksigner verify failed"
    rm -f "$UNSIGNED" "$ALIGNED"
    log "signed APK: $SIGNED"
}

# ---------- 5. 组装模块 zip ----------
build_module() {
    log "assembling module zip"
    local VER; VER="$(tr -d '[:space:]' < "$ROOT/VER")"
    [ -n "$VER" ] || fail "VER file empty"
    # 必须用全局变量: 函数返回后 local 失效, 但 trap 仍会触发,
    # 此时若 $STAGE 不可见, 在 set -u 下直接报 unbound variable 退出非 0
    STAGE="$(mktemp -d)"
    # ${STAGE:-} 防御 trap 触发时变量已 unset 的情况
    trap 'rm -rf "${STAGE:-}"' EXIT

    cp -r "$ROOT/magisk/." "$STAGE/"
    # 同步 VER 到 module.prop version
    sed -i "s/^version=.*/version=v$VER/" "$STAGE/module.prop"

    cp -f "$ROOT/native/build/$ABI/KernelShoot_daemon" "$STAGE/KernelShoot_daemon"
    chmod 0755 "$STAGE/KernelShoot_daemon"

    if [ -f "$WORK/KernelShoot.apk" ]; then
        cp -f "$WORK/KernelShoot.apk" "$STAGE/KernelShoot.apk"
    fi
    rm -rf "$STAGE/system" 2>/dev/null || true

    local ZIP="$DIST/KernelShoot-v$VER.zip"
    ( cd "$STAGE" && zip -r9 "$ZIP" ./* >/dev/null )
    log "module zip: $ZIP"
    ls -la "$ZIP"

    # 显式清理并解除 trap, 避免正常路径下也走 trap 二次清理
    rm -rf "$STAGE"
    trap - EXIT
    STAGE=
}

# ---------- main ----------
build_libjpeg
build_libdrm
build_daemon
build_apk
sign_apk
build_module

log "done. dist:"
ls -la "$DIST"
