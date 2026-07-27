<div align="center">

# KernelShoot

**Pure-ROOT lossless screenshot service for Android**

Read frames directly from the kernel framebuffer — bypass SurfaceFlinger entirely.  
Zero foreground interference. No window, no focus steal, no touch interception.

[![Build and Release](https://github.com/muyuzier-afk/KernelShoot/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/muyuzier-afk/KernelShoot/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/muyuzier-afk/KernelShoot?label=Release&color=blue)](https://github.com/muyuzier-afk/KernelShoot/releases)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)
[![Platform](https://img.shields.io/badge/Platform-arm64--v8a-orange.svg)](#system-requirements)
[![SELinux](https://img.shields.io/badge/SELinux-Enforcing%20%26%20Permissive-brightgreen.svg)](#selinux-compatibility)

</div>

---

## Overview

KernelShoot is a KernelSU-based Android screenshot tool that captures the screen by reading the kernel framebuffer device (`/dev/graphics/fb0`) or DRM device (`/dev/dri/card0`) directly, instead of going through SurfaceFlinger or the `screencap` command. The capture pipeline runs inside a resident root daemon (PPID = 1) that blocks on `epoll_wait` while idle, ensuring **0% CPU usage** when not capturing.

A screenshot is triggered by tapping a low-priority persistent notification. The notification click is delivered to a statically registered `BroadcastReceiver` — **no Activity is ever started**, so the foreground app never receives `onPause`, no window is created, and no input focus is changed.

### Why KernelShoot?

| Concern | `screencap` | KernelShoot |
|---|---|---|
| Depends on SurfaceFlinger | Yes | No |
| Trigger mechanism | Shell command | Notification tap |
| Cold-start cost per capture | Fork + connect to SF | Already-resident daemon |
| Typical capture latency | 200–500 ms | 30–80 ms |
| Output format | PNG | JPEG (NEON accelerated) |
| Foreground impact | None | None |
| SELinux Enforcing support | N/A | Yes (auto rule recovery) |

## Architecture

```
┌─────────────────────────── User Interaction ───────────────────────────┐
│                                                                        │
│   Foreground Service          BroadcastReceiver (static)               │
│   ┌─────────────────────┐     ┌──────────────────────────────┐        │
│   │ Persistent notif    │────▶│ Toast "3s countdown"         │        │
│   │ IMPORTANCE_LOW      │     │ HandlerThread.postDelayed    │        │
│   │ PendingIntent→BCAST │     │ LocalSocket → daemon         │        │
│   └─────────────────────┘     └──────────────────────────────┘        │
│                                          │ no Activity / Window        │
└──────────────────────────────────────────┼─────────────────────────────┘
                                           │ Abstract namespace socket
┌──────────────────────────────────────────▼─────────────────────────────┐
│                  Root Daemon (PPID=1, epoll_wait 0% CPU)               │
│                                                                        │
│   ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │
│   │ /proc scan  │─▶│ fb0 mmap     │─▶│ libjpeg-turbo│─▶│ file write │ │
│   │ oom_score=0 │  │  or DRM dumb │  │ tjCompress2  │  │ DCIM/*.jpg │ │
│   │ → pkg name  │  │  fallback    │  │ NEON, q=95   │  │ chmod 0644 │ │
│   └─────────────┘  └──────────────┘  └──────────────┘  └────────────┘ │
└──────────────────────────────────────────┬─────────────────────────────┘
                                           │ open / ioctl / mmap
┌──────────────────────────────────────────▼─────────────────────────────┐
│                    Kernel space                                        │
│       /dev/graphics/fb0       or       /dev/dri/card0                  │
└────────────────────────────────────────────────────────────────────────┘
```

### Design Principles

- **No Activity** — Notification tap routes through `PendingIntent.getBroadcast` → `BroadcastReceiver`. The receiver returns in <10 ms and never blocks broadcast dispatch.
- **No Window / No Surface** — Nothing is created that could intercept touch events or alter the focus stack.
- **Resident daemon, init-reparented** — Double `fork()` + `setsid()` leaves the daemon as a child of init (PID 1). A `service.sh` watchdog loop restarts it on crash.
- **Pure kernel read path** — `open()` / `ioctl(FBIOGET_VSCREENINFO)` / `mmap()` on fb0, with DRM dumb buffer fallback. No SurfaceFlinger round-trip.

## Repository Layout

```
.
├── VER                          # Single source of truth for release version
├── native/                      # C daemon (cross-compiled with NDK)
│   ├── jni/
│   │   ├── include/
│   │   │   ├── common.h         # Shared constants, protocol, log macros
│   │   │   └── ks_internal.h    # Inter-module API declarations
│   │   ├── main.c               # Entry: daemonize → epoll loop → orchestration
│   │   ├── daemon_init.c        # Double fork + setsid
│   │   ├── socket_server.c      # Abstract-namespace LocalSocket + epoll
│   │   ├── foreground_detector.c# /proc/*/oom_score_adj → package name
│   │   ├── fb_screenshot.c      # fb0 ioctl + mmap zero-copy
│   │   ├── drm_screenshot.c     # DRM dumb-buffer fallback
│   │   ├── jpeg_encoder.c       # libjpeg-turbo tjCompress2 (FASTDCT, q=95)
│   │   ├── file_writer.c        # {ts}_{pkg}.jpg → /sdcard/DCIM, chmod 0644
│   │   ├── selinux_check.c      # SELinux state detection + diagnostics
│   │   ├── Android.mk
│   │   └── Application.mk
│   ├── Makefile                 # Standalone NDK cross-compile (aarch64)
│   └── build_libs.sh            # Build libturbojpeg.a / libdrm.a
├── app/                         # Android Java interaction layer
│   ├── src/main/java/com/kernelshoot/
│   │   ├── SnapForegroundService.java  # Persistent low-priority notification
│   │   ├── SnapReceiver.java           # Notification tap → 3s → screenshot
│   │   ├── SocketClient.java           # LocalSocket client + JSON parse
│   │   ├── ToastHelper.java            # Focus-free Toast helper
│   │   ├── BootReceiver.java           # Start service on boot
│   │   ├── MainActivity.java
│   │   └── DaemonConst.java            # Protocol constants (mirrors native)
│   ├── src/main/AndroidManifest.xml
│   ├── src/main/res/
│   └── build.gradle
├── magisk/                      # KernelSU module
│   ├── module.prop
│   ├── service.sh               # Launch + SELinux self-heal + watchdog
│   ├── post-fs-data.sh
│   ├── sepolicy.rule            # KSU `su` domain + cross-domain connectto
│   └── customize.sh             # Installer: chmod binary + pm install APK
├── ci_build.sh                  # CI pipeline: libs → daemon → APK → module.zip
├── build.sh                     # Local build entrypoint
├── gradlew                      # Gradle wrapper 8.7 (vendored)
├── gradle/wrapper/
├── .github/workflows/build.yml  # Build + Release (same-tag overwrite)
├── .gitattributes
├── settings.gradle / build.gradle / gradle.properties
└── README.md
```

## Installation

### Option A — Pre-built release (recommended)

1. Download the latest `KernelShoot-v{VER}.zip` from the [Releases](https://github.com/muyuzier-afk/KernelShoot/releases) page.
2. Open **KernelSU Manager** → **Modules** → **Install from storage** → select the zip.
3. Reboot the device.
4. Launch the **KernelShoot** app once from the launcher to start the foreground notification.
5. Pull down the notification shade and tap **KernelShoot** → screenshot is captured after a 3-second countdown.

### Option B — Build from source

Requires Android NDK r26+ and Android SDK with platform 34.

```bash
export ANDROID_NDK=/path/to/ndk
export ANDROID_HOME=/path/to/sdk

./build.sh all          # Full pipeline: libs → daemon → APK → module.zip
# Or stage-by-stage:
# ./build.sh libs      # Build libturbojpeg.a / libdrm.a
# ./build.sh daemon    # Build native daemon only
# ./build.sh apk       # Build APK only
# ./build.sh module    # Assemble module zip only
```

Output: `dist/KernelShoot-v{VER}.zip`

## Usage

1. **Trigger** — Pull down the notification shade and tap the **KernelShoot** notification.
2. **Countdown** — A Toast appears: `3 秒后截图…` (non-blocking, no focus change).
3. **Capture** — After 3 seconds, the daemon reads the framebuffer and compresses to JPEG.
4. **Result** — A Toast confirms: `截图成功: /sdcard/DCIM/20260726_153012_com.example.app.jpg`.

Screenshots are saved as:

```
/sdcard/DCIM/{timestamp}_{package}.jpg
```

Files are written with mode `0644`, so the system gallery picks them up automatically via DCIM MediaStore scanning.

## SELinux Compatibility

KernelShoot operates correctly under **both** SELinux modes — no need to set permissive.

| Mode | Behavior |
|---|---|
| **Permissive** | No sepolicy rules required. Daemon runs directly. |
| **Enforcing** | [`magisk/sepolicy.rule`](./magisk/sepolicy.rule) is injected into the live kernel policy by `ksud` at boot. Critical rules include device access (`fb0`, `dri`), DCIM write, and the cross-domain `connectto` allowing `untrusted_app` → `su` socket connections. |

### Self-healing mechanism

[`magisk/service.sh`](./magisk/service.sh) verifies the critical `connectto` rule at startup when running in Enforcing mode. If the rule is missing (e.g. a KSU update cleared it), the script automatically re-applies it via `ksud sepolicy apply`. The daemon also prints the current SELinux mode on boot and emits targeted diagnostics if the server socket fails to bind.

```bash
# Verify on-device
getenforce                                                       # → Enforcing
ksud sepolicy query 'allow untrusted_app su unix_stream_socket connectto'
dmesg | grep denied | grep -E "fb0|dri|connectto"                # → (empty)
```

## CI / Release

This repository ships with a fully automated [GitHub Actions](./.github/workflows/build.yml) pipeline.

| Aspect | Detail |
|---|---|
| **Version source** | [`VER`](./VER) file at repo root (single line, e.g. `1.0.0`) |
| **Triggers** | Push to `main` touching `native/`, `app/`, `magisk/`, `ci_build.sh`, `VER`, or the workflow; manual dispatch from the Actions tab |
| **Pipeline** | libjpeg-turbo → libdrm → daemon → APK → module.zip (cross-compiled for arm64-v8a, API 28) |
| **Artifact** | `KernelShoot-v{VER}.zip` attached to a Release tagged `v{VER}` |
| **Same-tag overwrite** | If `v{VER}` already exists, the old Release **and** its tag are deleted, then a fresh Release is created with the new build. Otherwise a new Release is created. |
| **Latest marker** | Each published Release is flagged `--latest`. |

### Releasing a new version

Bump the contents of [`VER`](./VER) (e.g. `1.0.0` → `1.0.1`) and push to `main`. The workflow will build and publish `v1.0.1` automatically.

### Re-publishing the same version

Keep `VER` unchanged, push any source change. The workflow detects the existing `v{VER}` Release, deletes it along with the tag, and recreates it with the new build.

## Troubleshooting

All daemon diagnostics are written to a single log file:

```bash
cat /data/adb/modules/kernelshoot/daemon.log
```

| Symptom | Likely Cause | Resolution |
|---|---|---|
| `截图失败 [-1]: daemon unavailable` | App cannot connect to daemon socket | Check `ps -A \| grep KernelShoot_daemon`; inspect `dmesg \| grep denied` for `connectto` denials |
| `截图失败 [5]: no framebuffer device available` | Neither fb0 nor DRM is accessible | Run `ls -l /dev/graphics/fb0 /dev/dri/card0`; confirm daemon is running as root |
| `截图失败 [8]: file write failed` | Cannot write to `/sdcard/DCIM/` | From a root shell: `mkdir -p /sdcard/DCIM && chmod 777 /sdcard/DCIM` |
| No notification visible | Foreground service not started | Open the KernelShoot app once; on Android 13+, grant notification permission |
| Daemon not running after reboot | `service.sh` did not execute | Verify the KSU module is enabled; inspect `daemon.log` |

## Uninstallation

Via KernelSU Manager: **Modules** → **KernelShoot** → **Remove** → reboot.

Or from a root shell:

```bash
rm -rf /data/adb/modules/kernelshoot
pm uninstall com.kernelshoot
reboot
```

## System Requirements

- **KernelSU** — kernel module + `ksud` + manager app
- **Architecture** — `arm64-v8a`
- **Android** — 8.0 (API 26) or above
- **SELinux** — Enforcing and Permissive both supported

## Comparison

| Metric | `screencap` | KernelShoot |
|---|---|---|
| SurfaceFlinger dependency | Yes | No |
| Execution model | Fork per capture | Resident daemon |
| Foreground impact | None | None |
| Capture latency (1080p) | 200–500 ms | 30–80 ms |
| Output format | PNG | JPEG (smaller, NEON accelerated) |
| Trigger | CLI / API | Notification tap |
| Permission | root | root |

## License

Released under the [MIT License](./LICENSE).

<div align="center">

<sub>Built with Android NDK · libjpeg-turbo · libdrm · Gradle 8.7 · AGP 8.5.0</sub>

</div>
