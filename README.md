# KernelShoot

> 纯 ROOT 无损截图服务 · 从内核 framebuffer 直接读取画面 · 不影响前台应用

KernelShoot 是一个基于 KernelSU 的 Android 截图工具。它通过一个常驻后台的 root 守护进程直接读取内核 framebuffer（`/dev/graphics/fb0`）或 DRM 设备（`/dev/dri/card0`），完全绕过 SurfaceFlinger 和 `screencap`，实现真正意义上的「无损截图」——不创建窗口、不抢焦点、不拦截触摸、不触发前台 Activity 生命周期回调。

## 核心特性

| 特性 | 实现 |
|---|---|
| 零前台干扰 | 通知点击走 `BroadcastReceiver`，无 Activity / Window / Surface |
| 纯内核读取 | 直接 `open` / `ioctl` / `mmap` fb0，DRM 回退 |
| SELinux 双模式兼容 | Enforcing 与 Permissive 均可用，启动时自检规则并自愈 |
| 守护进程保活 | 双重 fork + setsid，PPID=1，`service.sh` 看门循环 |
| 待机零开销 | 非截图期 `epoll_wait` 阻塞，CPU 占用 0% |
| 高速压缩 | libjpeg-turbo NEON 加速，1080p 约 15–30ms |
| KernelSU 模块 | 单 zip 刷入，重启即用 |

## 工作流程

```
用户点击通知
   │
   ▼
BroadcastReceiver.onReceive()        ← 立即返回, 不阻塞
   ├─ Toast: "3 秒后截图"
   └─ HandlerThread.postDelayed(3000ms)
         │
         ▼
   LocalSocket → 守护进程
         │
         ▼
   守护进程 (PPID=1, epoll 阻塞中被唤醒)
   ├─ 读取 /proc/[pid]/oom_score_adj → 前台包名
   ├─ open /dev/graphics/fb0 → ioctl → mmap → memcpy
   │  └─ 失败回退: /dev/dri/card0 → drmModeGetFB → MapDumb
   ├─ libjpeg-turbo tjCompress2 (quality=95, FASTDCT)
   └─ 写 /sdcard/DCIM/{timestamp}_{package}.jpg
         │
         ▼
   Socket 返回路径 → Toast: "截图成功"
```

## 仓库结构

```
.
├── VER                      # 版本号 (CI 发版依据)
├── native/                  # C 守护进程
│   ├── jni/
│   │   ├── include/         # common.h, ks_internal.h
│   │   ├── main.c           # 入口 + 主编排
│   │   ├── daemon_init.c    # 双重 fork + setsid
│   │   ├── socket_server.c  # 抽象命名空间 LocalSocket + epoll
│   │   ├── foreground_detector.c  # /proc 前台包名检测
│   │   ├── fb_screenshot.c  # framebuffer 截图引擎
│   │   ├── drm_screenshot.c # DRM 回退引擎
│   │   ├── jpeg_encoder.c   # libjpeg-turbo 压缩
│   │   ├── file_writer.c    # 文件命名与写入
│   │   ├── selinux_check.c  # SELinux 状态检测与诊断
│   │   └── Android.mk / Application.mk
│   ├── Makefile             # 独立 NDK 交叉编译
│   └── build_libs.sh        # 编译静态库 (libturbojpeg / libdrm)
├── app/                     # Android Java 交互层
│   ├── src/main/java/com/kernelshoot/
│   │   ├── SnapForegroundService.java   # 持久通知前台服务
│   │   ├── SnapReceiver.java            # 通知点击 → 3s 倒计时 → 截图
│   │   ├── SocketClient.java            # LocalSocket 客户端
│   │   ├── ToastHelper.java
│   │   ├── BootReceiver.java            # 开机自启
│   │   ├── MainActivity.java
│   │   └── DaemonConst.java             # 协议常量 (与 native 对齐)
│   ├── src/main/AndroidManifest.xml
│   ├── src/main/res/
│   └── build.gradle
├── magisk/                  # KernelSU 模块
│   ├── module.prop
│   ├── service.sh           # 启动 + SELinux 自愈 + 看门循环
│   ├── post-fs-data.sh
│   ├── sepolicy.rule        # KSU su 域规则 + connectto 跨域放行
│   └── customize.sh         # 安装脚本
├── ci_build.sh              # CI 全流程构建
├── build.sh                 # 本地构建
├── .github/workflows/build.yml  # GitHub Actions
└── settings.gradle / build.gradle / gradle.properties
```

## 安装

### 方式一：下载预编译版本（推荐）

1. 前往 [Releases](https://github.com/muyuzier-afk/KernelShoot/releases) 下载最新 `KernelShoot-v{VER}.zip`
2. 打开 KernelSU 管理器 → 模块 → 从存储安装 → 选择该 zip
3. 重启设备
4. 从桌面打开 KernelShoot 应用一次（启动持久通知）
5. 下拉通知栏，点击「KernelShoot 运行中」→ 3 秒后截图完成

### 方式二：本地构建

需要 Android NDK r26+ 与 Android SDK（platform 34）：

```bash
export ANDROID_NDK=/path/to/ndk
export ANDROID_HOME=/path/to/sdk

./build.sh all        # 全流程: libs → daemon → apk → module.zip
# 或分步:
# ./build.sh libs    # 仅编译 libturbojpeg.a / libdrm.a
# ./build.sh daemon  # 仅编译守护进程
# ./build.sh apk     # 仅构建 APK
# ./build.sh module  # 仅打包模块 zip
```

产物：`dist/KernelShoot-v{VER}.zip`

## 使用

1. **触发截图**：下拉通知栏 → 点击「KernelShoot 运行中 · 点击截图」
2. **倒计时**：Toast 提示「3 秒后截图…」
3. **完成**：Toast 提示「截图成功: /sdcard/DCIM/20260726_153012_com.example.app.jpg」

截图保存路径：

```
/sdcard/DCIM/{timestamp}_{package}.jpg
```

文件权限 `0644`，系统相册自动扫描可见。

## CI 与发版

本项目通过 GitHub Actions 自动构建并发布 Release。

| 项 | 说明 |
|---|---|
| 版本来源 | 仓库根 [`VER`](./VER) 文件，单行（如 `1.0.0`） |
| 触发条件 | 推送到 `main` 且触及 `native/` / `app/` / `magisk/` / `ci_build.sh` / `VER` / workflow 自身；也支持 Actions 页面手动触发 |
| 产物 | `KernelShoot-v{VER}.zip`，附加到 tag 为 `v{VER}` 的 Release |
| **同版本覆盖** | 若 `v{VER}` 已存在，先删除旧 Release 与 tag，再重新创建；否则直接创建新 Release |

### 发新版本

编辑 `VER` 文件改成新版本号（如 `1.0.1`），提交推送即可。CI 会自动构建并发布 `v1.0.1`。

### 重发同版本

保持 `VER` 不变，修改任意源码并推送。CI 检测到 `v1.0.0` 已存在 → 删除旧 Release → 用新构建覆盖。

## SELinux 兼容性

KernelShoot 在两种 SELinux 模式下均可工作：

- **Permissive 模式**：无需任何 sepolicy 规则，直接运行
- **Enforcing 模式**：通过 [`magisk/sepolicy.rule`](./magisk/sepolicy.rule) 注入规则，关键规则包括：
  - 守护进程访问 `/dev/graphics/fb0`、`/dev/dri/card0`
  - 写入 `/sdcard/DCIM`
  - **App → 守护进程 socket 的 `connectto`**（不可省略，跨域规则）
- **自愈机制**：`service.sh` 启动时检测 SELinux 模式，Enforcing 下用 `ksud sepolicy query` 验证关键规则，未生效则自动 `ksud sepolicy apply` 重新应用

守护进程启动时也会打印 SELinux 状态，bind 失败时输出基于当前模式的诊断建议。

## 排查

日志统一在：

```bash
cat /data/adb/modules/kernelshoot/daemon.log
```

| 症状 | 原因 | 解决 |
|---|---|---|
| `截图失败 [-1]: daemon unavailable` | App 连不上守护进程 socket | 检查守护进程是否运行；查 `dmesg \| grep denied` 是否有 `connectto` 拦截 |
| `截图失败 [5]: no framebuffer device available` | fb0 与 DRM 都打不开 | `ls -l /dev/graphics/fb0 /dev/dri/card0` 检查设备节点与权限 |
| `截图失败 [8]: file write failed` | 无法写 `/sdcard/DCIM/` | root shell 执行 `mkdir -p /sdcard/DCIM && chmod 777 /sdcard/DCIM` |
| 无通知 | 前台服务未启动 | 打开一次 KernelShoot 应用；Android 13+ 检查通知权限 |
| 重启后守护进程未运行 | `service.sh` 未执行 | 检查 KSU 模块是否启用；查看 `daemon.log` |

## 卸载

KernelSU 管理器 → 模块 → KernelShoot → 移除 → 重启。

或 root shell：

```bash
rm -rf /data/adb/modules/kernelshoot
pm uninstall com.kernelshoot
reboot
```

## 系统要求

- KernelSU（内核模块 + ksud + 管理器）
- arm64-v8a 设备
- Android 8.0 (API 26) 及以上
- SELinux Enforcing 与 Permissive 均支持

## 技术对比

| 指标 | `screencap` | KernelShoot |
|---|---|---|
| 依赖服务 | SurfaceFlinger | 无 |
| 执行方式 | fork 子进程 | 守护进程常驻 |
| 前台影响 | 无 | 无 |
| 截图速度 | 200–500ms | 30–80ms |
| 保存格式 | PNG | JPEG（更小） |
| 权限需求 | root | root |
| 触发方式 | 命令行 | 通知点击 |

## 许可证

MIT
