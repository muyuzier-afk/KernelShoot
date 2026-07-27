#!/system/bin/sh
# KernelShoot - post-fs-data.sh
# 在 post-fs-data 阶段执行早期设置 (此时 /data 已挂载, SELinux 策略已加载)
#
# 职责: 仅做轻量准备, 守护进程本体在 service.sh 启动 (需 /dev/graphics 就绪)
MODDIR=${0%/*}

# 预创建输出目录, 并修正权限确保相册可读
mkdir -p /sdcard/DCIM 2>/dev/null
chmod 0777 /sdcard/DCIM 2>/dev/null

# 占位: 可在此放置设备特定兼容性探测, 例如检测 fb0 是否存在
# 实际守护进程会在运行时自动回退到 DRM
true
