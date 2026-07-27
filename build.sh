#!/usr/bin/env bash
# KernelShoot - 顶层构建脚本
#
# 流程:
#   1. (可选) 构建预编译静态库 libturbojpeg.a / libdrm.a
#   2. 构建 native 守护进程 KernelShoot_daemon (arm64-v8a)
#   3. 构建 Android APK (release)
#   4. 组装 Magisk 模块 zip (含 daemon + apk + 脚本)
#
# 用法:
#   ./build.sh all        # 全流程
#   ./build.sh libs       # 仅构建静态库
#   ./build.sh daemon     # 仅构建守护进程
#   ./build.sh apk        # 仅构建 APK
#   ./build.sh module     # 仅打包 Magisk zip (依赖前几步产物)
#
# 环境变量:
#   ANDROID_NDK   NDK 根目录
#   ANDROID_HOME  Android SDK 根目录 (用于 gradle)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$ROOT/native"
APP="$ROOT/app"
MAGISK="$ROOT/magisk"
DIST="$ROOT/dist"

ABI=arm64-v8a
DAEMON_BIN="$NATIVE/build/$ABI/KernelShoot_daemon"
APK_RELEASE="$APP/build/outputs/apk/release/app-release-unsigned.apk"
MODULE_ZIP="$DIST/KernelShoot-$(awk -F= '/^version=/{gsub(/"/,"");print $2}' "$MAGISK/module.prop").zip"

log() { echo -e "\033[1;34m[build]\033[0m $*"; }
err() { echo -e "\033[1;31m[error]\033[0m $*" >&2; }

step_libs() {
    log "构建预编译静态库 (libjpeg-turbo + libdrm)"
    : "${ANDROID_NDK:?请设置 ANDROID_NDK}"
    bash "$NATIVE/build_libs.sh"
}

step_daemon() {
    log "构建守护进程 KernelShoot_daemon ($ABI)"
    : "${ANDROID_NDK:?请设置 ANDROID_NDK}"
    make -C "$NATIVE" all
    make -C "$NATIVE" strip
    if [ ! -f "$DAEMON_BIN" ]; then
        err "守护进程构建失败: $DAEMON_BIN 不存在"
        exit 1
    fi
    file "$DAEMON_BIN" 2>/dev/null || true
    log "守护进程: $DAEMON_BIN"
}

step_apk() {
    log "构建 APK (release)"
    if [ ! -x "$ROOT/gradlew" ]; then
        err "未找到 gradlew, 请先执行: gradle wrapper --gradle-version 8.7"
        exit 1
    fi
    cd "$ROOT"
    ./gradlew :app:assembleRelease --no-daemon
    if [ ! -f "$APK_RELEASE" ]; then
        err "APK 构建失败: $APK_RELEASE 不存在"
        exit 1
    fi
    log "APK (未签名): $APK_RELEASE"
}

step_sign() {
    # 给 -unsigned.apk 走 zipalign + apksigner, 产出 -signed.apk
    # 否则 customize.sh 中 pm install 会报 INSTALL_PARSE_FAILED_NO_CERTIFICATES
    log "签名 APK"
    : "${ANDROID_HOME:?请设置 ANDROID_HOME}"
    local BTDIR
    BTDIR="$(ls -d "$ANDROID_HOME"/build-tools/* 2>/dev/null | sort -V | tail -1)"
    [ -n "$BTDIR" ] || { err "未找到 build-tools"; exit 1; }
    local APKSIGNER="$BTDIR/apksigner"
    local ZIPALIGN="$BTDIR/zipalign"
    [ -x "$APKSIGNER" ] || { err "缺少 apksigner: $APKSIGNER"; exit 1; }
    [ -x "$ZIPALIGN"  ] || { err "缺少 zipalign: $ZIPALIGN";   exit 1; }

    local UNSIGNED="$APP/build/outputs/apk/release/app-release-unsigned.apk"
    local ALIGNED="$APP/build/outputs/apk/release/app-release-aligned.apk"
    local SIGNED="$APP/build/outputs/apk/release/app-release-signed.apk"
    local KEYSTORE="$NATIVE/prebuilt/$ABI/keystore.jks"
    if [ ! -f "$KEYSTORE" ]; then
        keytool -genkeypair -keystore "$KEYSTORE" -alias kernelshoot \
            -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass changeit -keypass changeit \
            -dname "CN=KernelShoot, OU=Magisk, O=KernelShoot, L=NA, ST=NA, C=CN" \
            >/dev/null 2>&1
    fi
    "$ZIPALIGN"  -f -p 4 "$UNSIGNED" "$ALIGNED"
    "$APKSIGNER" sign --ks "$KEYSTORE" --ks-pass pass:changeit --key-pass pass:changeit \
        --min-sdk-version 26 \
        --out "$SIGNED" "$ALIGNED"
    "$APKSIGNER" verify --print-certs "$SIGNED" >/dev/null
    APK_RELEASE="$SIGNED"
    log "APK (已签名): $APK_RELEASE"
}

step_module() {
    log "组装 Magisk 模块 zip"
    mkdir -p "$DIST"
    local STAGE; STAGE="$(mktemp -d)"
    trap 'rm -rf "$STAGE"' EXIT

    # 拷贝模块骨架
    cp -r "$MAGISK/." "$STAGE/"

    # 拷贝守护进程二进制
    if [ ! -f "$DAEMON_BIN" ]; then
        err "守护进程未构建, 请先执行: $0 daemon"
        exit 1
    fi
    cp -f "$DAEMON_BIN" "$STAGE/KernelShoot_daemon"
    chmod 0755 "$STAGE/KernelShoot_daemon"

    # 拷贝 APK (可选)
    if [ -f "$APK_RELEASE" ]; then
        cp -f "$APK_RELEASE" "$STAGE/KernelShoot.apk"
    else
        log "提示: APK 未构建, 模块不含交互层 APK"
    fi

    # 清理无用空目录
    rm -rf "$STAGE/system" 2>/dev/null || true

    # 打包 (Magisk 要求 zip 不含顶层目录)
    ( cd "$STAGE" && zip -r9 "$MODULE_ZIP" ./* >/dev/null )

    log "模块 zip: $MODULE_ZIP"
    log "刷入方法: Magisk → 模块 → 从存储安装 → 选择此 zip → 重启"
}

case "${1:-all}" in
    libs)   step_libs ;;
    daemon) step_daemon ;;
    apk)    step_apk; step_sign ;;
    sign)   step_sign ;;
    module) step_module ;;
    all)
        step_libs
        step_daemon
        step_apk
        step_sign
        step_module
        ;;
    *)
        echo "用法: $0 {all|libs|daemon|apk|sign|module}"
        exit 1
        ;;
esac

log "完成."
