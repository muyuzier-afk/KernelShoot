#!/system/bin/sh
# KernelShoot - service.sh
# 在 late_start service 阶段启动守护进程 (此阶段 SELinux 已就绪, /dev 可访问)
#
# 保活: while 循环检测, 进程退出则拉起; 每次启动间隔 2 秒避免空转
# 守护进程自身已做双重 fork+setsid, 这里仅负责"拉起并看门"
#
# SELinux 自愈:
#   - Permissive 模式: 无需任何规则, 直接启动
#   - Enforcing 模式: 验证关键规则 (connectto) 是否生效, 不生效则用
#     ksud sepolicy 重新应用本模块的 sepolicy.rule
#   - 确保两种模式下守护进程都能 bind socket 并被 App 连接
#
# 此脚本由 KernelSU 以 u:r:su:s0 执行

MODDIR=${0%/*}
BIN="$MODDIR/KernelShoot_daemon"
RULES="$MODDIR/sepolicy.rule"
LOG="$MODDIR/daemon.log"

log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOG"; }

# ---------- 等待 /data 挂载 ----------
while [ ! -d /data/adb ]; do
    sleep 1
done

# 确保输出目录存在
mkdir -p /sdcard/DCIM 2>/dev/null

# 确保二进制可执行
chmod 0755 "$BIN" 2>/dev/null

# 清空旧日志
: > "$LOG"
log "=== KernelShoot service.sh start ==="
log "MODDIR=$MODDIR"

# ---------- SELinux 状态检测 ----------
SELINUX_ENFORCE=0
if [ -f /sys/fs/selinux/enforce ]; then
    SELINUX_ENFORCE=$(cat /sys/fs/selinux/enforce 2>/dev/null || echo 0)
fi
SESTATUS=$(getenforce 2>/dev/null || echo "unknown")
log "SELinux state: $SESTATUS (enforce=$SELINUX_ENFORCE)"

# ---------- Enforcing 下验证并自愈规则 ----------
if [ "$SELINUX_ENFORCE" = "1" ]; then
    log "SELinux is Enforcing, verifying sepolicy rules..."

    # 关键规则: App -> 守护进程 socket connectto
    # 这条规则不生效则 Java 端 connect() 会 EACCES
    RULE_OK=0

    # 方式1: 用 ksud sepolicy query (KSU v0.7+)
    if [ -x /data/adb/ksu/bin/ksud ]; then
        if /data/adb/ksu/bin/ksud sepolicy query \
            "allow untrusted_app su unix_stream_socket connectto" >/dev/null 2>&1; then
            RULE_OK=1
            log "  [ksud] connectto rule OK"
        fi
    fi

    # 方式2: 如果 ksud 不支持 query, 用 magiskpolicy 兼容 (部分 KSU 分支带)
    if [ "$RULE_OK" = "0" ] && [ -x /data/adb/magisk/magiskpolicy ]; then
        if /data/adb/magisk/magiskpolicy --query \
            "allow untrusted_app su unix_stream_socket connectto" 2>&1 | grep -q "allow"; then
            RULE_OK=1
            log "  [magiskpolicy] connectto rule OK"
        fi
    fi

    # 规则未生效, 尝试重新应用
    if [ "$RULE_OK" = "0" ]; then
        log "  connectto rule MISSING, attempting to apply $RULES"

        APPLIED=0
        # 尝试 ksud sepolicy apply
        if [ -x /data/adb/ksu/bin/ksud ] && [ -f "$RULES" ]; then
            if /data/adb/ksu/bin/ksud sepolicy apply "$RULES" >>"$LOG" 2>&1; then
                APPLIED=1
                log "  [ksud] rules re-applied"
            fi
        fi
        # 尝试 magiskpolicy --apply
        if [ "$APPLIED" = "0" ] && [ -x /data/adb/magisk/magiskpolicy ] && [ -f "$RULES" ]; then
            if /data/adb/magisk/magiskpolicy --apply "$RULES" >>"$LOG" 2>&1; then
                APPLIED=1
                log "  [magiskpolicy] rules re-applied"
            fi
        fi

        if [ "$APPLIED" = "1" ]; then
            log "  rules recovered, please retry screenshot if still failing"
        else
            log "  WARNING: could not apply sepolicy.rules"
            log "  App -> daemon socket connect may fail under Enforcing"
            log "  Workaround: setenforce 0 (NOT recommended, breaks system security)"
            log "  Or reboot to let KSU reload rules at boot"
        fi
    fi
else
    log "SELinux is Permissive or disabled, no rule verification needed"
fi

# ---------- 看门循环: 启动并保活守护进程 ----------
while true; do
    if [ -x "$BIN" ]; then
        "$BIN" >> "$LOG" 2>&1 &
        KS_PID=$!
        wait $KS_PID
        RC=$?
        log "[watchdog] daemon exited (code=$RC), restarting in 2s"
        # 非正常退出码记录, 便于排查
        if [ "$RC" != "0" ]; then
            log "[watchdog] last 20 lines of dmesg avc denied:"
            dmesg | grep -i "avc.*denied" | tail -20 >> "$LOG" 2>/dev/null
        fi
    else
        log "[watchdog] binary not executable: $BIN"
    fi
    sleep 2
done
