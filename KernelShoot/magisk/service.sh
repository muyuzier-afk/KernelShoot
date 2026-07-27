#!/system/bin/sh
# KernelShoot - service.sh
# 在 late_start service 阶段启动守护进程 (此阶段 SELinux 已就绪, /dev 可访问)
#
# 保活: while 循环检测, 进程退出则拉起; 每次启动间隔 2 秒避免空转
# 守护进程自身已做双重 fork+setsid, 这里仅负责"拉起并看门"
#
# 注意: 此脚本由 Magisk 以 u:r:magisk:s0 执行, 子进程继承该上下文
MODDIR=${0%/*}
BIN="$MODDIR/KernelShoot_daemon"
LOG="$MODDIR/daemon.log"

# 等待 /data 挂载完成 (late_start 时通常已挂载, 保险起见)
while [ ! -d /data/adb ]; do
    sleep 1
done

# 确保输出目录存在
mkdir -p /sdcard/DCIM 2>/dev/null

# 确保二进制可执行
chmod 0755 "$BIN" 2>/dev/null

# 去除旧日志 (保留最近一次)
: > "$LOG"

# 看门循环: 守护进程崩溃则自动重启
while true; do
    if [ -x "$BIN" ]; then
        "$BIN" >> "$LOG" 2>&1 &
        KS_PID=$!
        wait $KS_PID
        echo "[watchdog] daemon exited (code=$?), restarting in 2s" >> "$LOG"
    else
        echo "[watchdog] binary not executable: $BIN" >> "$LOG"
    fi
    sleep 2
done
