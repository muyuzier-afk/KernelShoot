# KernelShoot

KernelShoot is a screenshot tool that uses PID0, which gives root access.

## Features

- **Pure ROOT screenshot**: reads directly from kernel framebuffer (`/dev/graphics/fb0`) or DRM (`/dev/dri/card0`), bypassing SurfaceFlinger
- **Zero foreground impact**: no Activity / Window / Surface, no focus steal, no touch interception, no `onPause` callback on the foreground app
- **SELinux Enforcing compatible**: works under both Enforcing and Permissive modes; auto-recovers missing sepolicy rules at boot
- **KernelSU module**: ships as a flashable zip, daemon watchdog auto-restart
- **Fast**: libjpeg-turbo NEON-accelerated JPEG compression (~15–30ms per 1080p frame)

## Architecture

```
User tap → notification → BroadcastReceiver → 3s countdown → LocalSocket
                                                                ↓
                          daemon (PPID=1, epoll_wait 0% CPU)
                          ├─ /proc foreground package detection
                          ├─ fb0 mmap → fallback DRM dumb buffer
                          ├─ libjpeg-turbo compress
                          └─ write /sdcard/DCIM/{ts}_{pkg}.jpg
```

## CI / Release

This repository is built and released via GitHub Actions.

- Version source: [`VER`](./VER) file at repo root (single line, e.g. `1.0.0`)
- Trigger: any push to `main` that touches `native/`, `app/`, `magisk/`, `ci_build.sh`, `VER`, or the workflow itself; also manual `workflow_dispatch`
- Output: `KernelShoot-v{VER}.zip` attached to GitHub Release tagged `v{VER}`
- **If a release with the same tag already exists, it is overwritten** (deleted and recreated); otherwise a new one is created

### Bump version

Edit `VER` and push — the workflow will build and re-publish the release automatically.

## Build locally

Requires Android NDK r26+ and Android SDK with platform 34.

```bash
export ANDROID_NDK=/path/to/ndk
export ANDROID_HOME=/path/to/sdk
./build.sh all     # or: libs | daemon | apk | module
```

Output: `dist/KernelShoot-v{VER}.zip`

## Install

1. Flash `KernelShoot-v{VER}.zip` via KernelSU Manager → Modules → Install from storage
2. Reboot
3. Open KernelShoot app once to start the foreground notification
4. Tap the notification → 3s countdown → screenshot saved to `/sdcard/DCIM/`

## Requirements

- KernelSU (kernel module + ksud + manager)
- arm64-v8a device
- SELinux Enforcing is supported (no need to set permissive)

## License

MIT
