<div align="center">

# KernelShoot · 内核级截图

**纯 ROOT 无损截图服务（KernelSU 模块）**

绕过 SurfaceFlinger，直接从内核 framebuffer 读取画面。
零前台干扰：不弹窗、不抢焦点、不截获触摸。

[**English**](./README.md) · **简体中文**

[![Build and Release](https://github.com/muyuzier-afk/KernelShoot/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/muyuzier-afk/KernelShoot/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/muyuzier-afk/KernelShoot?label=Release&color=blue)](https://github.com/muyuzier-afk/KernelShoot/releases)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)
[![Platform](https://img.shields.io/badge/Platform-arm64--v8a-orange.svg)](#系统要求)
[![SELinux](https://img.shields.io/badge/SELinux-Enforcing%20%26%20Permissive-brightgreen.svg)](#selinux-兼容性)

</div>

---

## 简介

KernelShoot 是一款基于 KernelSU 的 Android 截图工具。它不经过 SurfaceFlinger，也不调用 `screencap` 命令，而是**直接读取内核 framebuffer 设备（`/dev/graphics/fb0`）或 DRM 设备（`/dev/dri/card0`）**来获取屏幕画面。

整条采集链路运行在一个常驻的 root 守护进程中（PPID = 1），空闲时阻塞在 `epoll_wait`，**CPU 占用为 0%**。

截图触发方式：点击一个低优先级常驻通知。通知点击事件由静态注册的 `BroadcastReceiver` 接收——**永远不启动 Activity**，因此前台应用不会收到 `onPause`，不会创建窗口，输入焦点也不会变化。

### 为什么选择 KernelShoot？

| 关注点 | `screencap` | KernelShoot |
|---|---|---|
| 是否依赖 SurfaceFlinger | 是 | 否 |
| 触发方式 | Shell 命令 | 点击通知 |
| 每次截图的冷启动开销 | Fork + 连接 SF | 守护进程已常驻 |
| 典型截图延迟 | 200–500 ms | 30–80 ms |
| 输出格式 | PNG | JPEG（NEON 加速） |
| 对前台的影响 | 无 | 无 |
| SELinux Enforcing 支持 | N/A | 支持（自动规则自愈） |

## 架构

```
┌─────────────────────────── 用户交互层 ────────────────────────────┐
│                                                                  │
│   前台服务                  静态广播接收器 (BroadcastReceiver)     │
│   ┌─────────────────────┐  ┌──────────────────────────────┐      │
│   │ 常驻通知             │─▶│ Toast "3 秒后截图…"          │      │
│   │ IMPORTANCE_LOW       │  │ HandlerThread.postDelayed    │      │
│   │ PendingIntent→BCAST  │  │ LocalSocket → 守护进程        │      │
│   └─────────────────────┘  └──────────────────────────────┘      │
│                                       │ 无 Activity / Window     │
└───────────────────────────────────────┼──────────────────────────┘
                                        │ Abstract namespace socket
┌───────────────────────────────────────▼──────────────────────────┐
│             Root 守护进程 (PPID=1, epoll_wait 0% CPU)            │
│                                                                  │
│   ┌─────────────┐ ┌──────────────┐ ┌──────────────┐ ┌─────────┐ │
│   │ /proc 扫描  │▶│ fb0 mmap     │▶│ libjpeg-turbo│▶│ 文件写入 │ │
│   │ oom_score=0 │ │ 或 DRM dumb  │ │ tjCompress2  │ │ DCIM/*. │ │
│   │ → 包名      │ │ 后备路径     │ │ NEON, q=95   │ │ jpg     │ │
│   └─────────────┘ └──────────────┘ └──────────────┘ └─────────┘ │
└───────────────────────────────────────┬──────────────────────────┘
                                        │ open / ioctl / mmap
┌───────────────────────────────────────▼──────────────────────────┐
│                       内核空间                                    │
│       /dev/graphics/fb0       或       /dev/dri/card0            │
└──────────────────────────────────────────────────────────────────┘
```

### 设计原则

- **无 Activity** — 通知点击通过 `PendingIntent.getBroadcast` 路由到 `BroadcastReceiver`，接收器在 10 ms 内返回，永不阻塞广播派发。
- **无 Window / 无 Surface** — 不创建任何可能截获触摸事件或改变焦点栈的对象。
- **常驻守护进程，init 收养** — 双重 `fork()` + `setsid()`，守护进程最终成为 init（PID 1）的子进程。`service.sh` 内的看门狗循环会在崩溃时自动重启。
- **纯内核读取路径** — 对 fb0 执行 `open()` / `ioctl(FBIOGET_VSCREENINFO)` / `mmap()`，失败时回退到 DRM dumb buffer，全程不走 SurfaceFlinger。

## 目录结构

```
.
├── VER                          # 发布版本的唯一真实来源
├── native/                      # C 守护进程（NDK 交叉编译）
│   ├── jni/
│   │   ├── include/
│   │   │   ├── common.h         # 公共常量、协议、日志宏
│   │   │   └── ks_internal.h    # 模块间 API 声明
│   │   ├── main.c               # 入口：daemonize → epoll 循环 → 编排
│   │   ├── daemon_init.c        # 双重 fork + setsid
│   │   ├── socket_server.c      # Abstract-namespace LocalSocket + epoll
│   │   ├── foreground_detector.c# /proc/*/oom_score_adj → 包名
│   │   ├── fb_screenshot.c      # fb0 ioctl + mmap 零拷贝
│   │   ├── drm_screenshot.c     # DRM dumb buffer 后备路径
│   │   ├── jpeg_encoder.c       # libjpeg-turbo tjCompress2 (FASTDCT, q=95)
│   │   ├── file_writer.c        # {ts}_{pkg}.jpg → /sdcard/DCIM, chmod 0644
│   │   ├── selinux_check.c      # SELinux 状态检测 + 诊断
│   │   ├── Android.mk
│   │   └── Application.mk
│   ├── Makefile                 # 独立 NDK 交叉编译 (aarch64)
│   └── build_libs.sh            # 构建 libturbojpeg.a / libdrm.a
├── app/                         # Android Java 交互层
│   ├── src/main/java/com/kernelshoot/
│   │   ├── SnapForegroundService.java  # 常驻低优先级通知
│   │   ├── SnapReceiver.java           # 通知点击 → 3s → 截图
│   │   ├── SocketClient.java           # LocalSocket 客户端 + JSON 解析
│   │   ├── ToastHelper.java            # 无焦点 Toast 辅助
│   │   ├── BootReceiver.java           # 开机自启服务
│   │   ├── MainActivity.java
│   │   └── DaemonConst.java            # 协议常量（与 native 对齐）
│   ├── src/main/AndroidManifest.xml
│   ├── src/main/res/
│   └── build.gradle
├── magisk/                      # KernelSU 模块
│   ├── module.prop
│   ├── service.sh               # 启动 + SELinux 自愈 + 看门狗
│   ├── post-fs-data.sh
│   ├── sepolicy.rule            # KSU `su` 域 + 跨域 connectto
│   └── customize.sh             # 安装器：chmod 二进制 + pm install APK
├── ci_build.sh                  # CI 流水线：libs → daemon → APK → module.zip
├── build.sh                     # 本地构建入口
├── gradlew                      # Gradle wrapper 8.7（已 vendored）
├── gradle/wrapper/
├── .github/workflows/build.yml  # 构建 + 发布（同 tag 覆盖）
├── .gitattributes
├── settings.gradle / build.gradle / gradle.properties
├── README.md                    # 英文文档
└── README.zh-CN.md              # 中文文档（本文件）
```

## 安装

### 方式 A — 预编译版本（推荐）

1. 从 [Releases](https://github.com/muyuzier-afk/KernelShoot/releases) 页面下载最新的 `KernelShoot-v{VER}.zip`。
2. 打开 **KernelSU 管理器** → **模块** → **从存储安装** → 选择该 zip。
3. 重启设备。
4. 从启动器打开一次 **KernelShoot** 应用，以启动前台通知。
5. 下拉通知栏，点击 **KernelShoot** → 3 秒倒计时结束后自动截图。

### 方式 B — 从源码构建

依赖：Android NDK r26+ 以及包含 platform 34 的 Android SDK。

```bash
export ANDROID_NDK=/path/to/ndk
export ANDROID_HOME=/path/to/sdk

./build.sh all          # 全流程：libs → daemon → APK → module.zip
# 也可以分步执行：
# ./build.sh libs      # 仅构建 libturbojpeg.a / libdrm.a
# ./build.sh daemon    # 仅构建 native 守护进程
# ./build.sh apk       # 仅构建 APK
# ./build.sh module    # 仅打包模块 zip
```

产物：`dist/KernelShoot-v{VER}.zip`

## 使用方法

1. **触发** — 下拉通知栏，点击 **KernelShoot** 通知。
2. **倒计时** — 弹出 Toast：`3 秒后截图…`（非阻塞，不改变焦点）。
3. **采集** — 3 秒后，守护进程读取 framebuffer 并压缩为 JPEG。
4. **结果** — Toast 提示：`截图成功: /sdcard/DCIM/20260726_153012_com.example.app.jpg`。

截图文件命名规则：

```
/sdcard/DCIM/{时间戳}_{包名}.jpg
```

文件权限为 `0644`，系统相册会通过 DCIM MediaStore 扫描自动收录。

## SELinux 兼容性

KernelShoot 在 **两种 SELinux 模式下都能正常工作**——无需切换为 Permissive。

| 模式 | 行为 |
|---|---|
| **Permissive** | 无需 sepolicy 规则，守护进程直接运行。 |
| **Enforcing** | [`magisk/sepolicy.rule`](./magisk/sepolicy.rule) 会在开机时由 `ksud` 注入到内核策略中。关键规则包括设备访问（`fb0`、`dri`）、DCIM 写入，以及允许 `untrusted_app` → `su` 跨域 `connectto` 的规则。 |

### 自愈机制

[`magisk/service.sh`](./magisk/service.sh) 在 Enforcing 模式下启动时会校验关键 `connectto` 规则。如果该规则缺失（例如 KSU 更新后被清空），脚本会自动通过 `ksud sepolicy apply` 重新应用。守护进程在启动时也会打印当前 SELinux 模式，并在服务端 socket 绑定失败时输出针对性诊断。

```bash
# 在设备上校验
getenforce                                                       # → Enforcing
ksud sepolicy query 'allow untrusted_app su unix_stream_socket connectto'
dmesg | grep denied | grep -E "fb0|dri|connectto"                # → (空)
```

## CI / 发布

本仓库自带完整的 [GitHub Actions](./.github/workflows/build.yml) 流水线。

| 项目 | 说明 |
|---|---|
| **版本来源** | 仓库根目录的 [`VER`](./VER) 文件（单行，例如 `1.0.0`） |
| **触发条件** | 推送至 `main` 且改动 `native/`、`app/`、`magisk/`、`ci_build.sh`、`VER` 或工作流文件；也可在 Actions 页面手动触发 |
| **流水线** | libjpeg-turbo → libdrm → daemon → APK → module.zip（针对 arm64-v8a、API 28 交叉编译） |
| **产物** | `KernelShoot-v{VER}.zip`，作为 Release 附件挂载在 `v{VER}` tag 下 |
| **同 tag 覆盖** | 若 `v{VER}` 已存在，会先删除旧的 Release **和** tag，再用新构建重新创建；否则创建新 Release |
| **Latest 标记** | 每个发布的 Release 都会被标记为 `--latest` |

### 发布新版本

修改 [`VER`](./VER) 文件的内容（例如 `1.0.0` → `1.0.1`）并推送至 `main`。工作流会自动构建并发布 `v1.0.1`。

### 重新发布同一版本

保持 `VER` 不变，推送任意源码改动。工作流会检测到已存在的 `v{VER}` Release，删除它和对应 tag，然后用新构建重新创建。

## 故障排查

守护进程的所有诊断日志都写入同一个文件：

```bash
cat /data/adb/modules/kernelshoot/daemon.log
```

| 现象 | 可能原因 | 解决方法 |
|---|---|---|
| `截图失败 [-1]: daemon unavailable` | 应用无法连接守护进程 socket | 检查 `ps -A \| grep KernelShoot_daemon`；查看 `dmesg \| grep denied` 中是否有 `connectto` 拒绝 |
| `截图失败 [5]: no framebuffer device available` | fb0 和 DRM 都不可访问 | 执行 `ls -l /dev/graphics/fb0 /dev/dri/card0`；确认守护进程以 root 身份运行 |
| `截图失败 [8]: file write failed` | 无法写入 `/sdcard/DCIM/` | 在 root shell 中执行：`mkdir -p /sdcard/DCIM && chmod 777 /sdcard/DCIM` |
| 通知不可见 | 前台服务未启动 | 打开一次 KernelShoot 应用；Android 13+ 需授予通知权限 |
| 重启后守护进程未运行 | `service.sh` 未执行 | 确认 KSU 模块已启用；检查 `daemon.log` |

## 卸载

通过 KernelSU 管理器：**模块** → **KernelShoot** → **卸载** → 重启。

或者在 root shell 中：

```bash
rm -rf /data/adb/modules/kernelshoot
pm uninstall com.kernelshoot
reboot
```

## 系统要求

- **KernelSU** — 内核模块 + `ksud` + 管理器应用
- **架构** — `arm64-v8a`
- **Android** — 8.0（API 26）及以上
- **SELinux** — Enforcing 与 Permissive 两种模式均支持

## 对比

| 指标 | `screencap` | KernelShoot |
|---|---|---|
| 是否依赖 SurfaceFlinger | 是 | 否 |
| 执行模型 | 每次 fork | 常驻守护进程 |
| 对前台的影响 | 无 | 无 |
| 截图延迟（1080p） | 200–500 ms | 30–80 ms |
| 输出格式 | PNG | JPEG（体积更小，NEON 加速） |
| 触发方式 | CLI / API | 点击通知 |
| 权限要求 | root | root |

## 许可证

基于 [MIT 许可证](./LICENSE) 发布。

<div align="center">

[English](./README.md) · [简体中文](./README.zh-CN.md)

<sub>Built with Android NDK · libjpeg-turbo · libdrm · Gradle 8.7 · AGP 8.5.0</sub>

</div>
