/*
 * KernelShoot - selinux_check.c
 * SELinux 状态检测与诊断
 *
 * 用途:
 *   守护进程启动时打印 SELinux 模式; socket bind 失败时根据当前模式
 *   给出针对性诊断, 帮助排查为何 App 连不上守护进程.
 *
 * 两种模式下的预期行为:
 *   - Permissive: 不需要任何 sepolicy 规则, bind 必然成功
 *     (如果仍失败, 原因在别处: 端口冲突/权限/路径)
 *   - Enforcing: 需要 sepolicy.rule 注入的 allow 规则
 *     (bind 失败通常是规则没生效, 引导用户检查 ksud/sepolicy.rule)
 */
#include "ks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int ks_selinux_enforcing(void) {
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    if (fd < 0) {
        /* SELinux 未启用或未挂载 */
        return -1;
    }
    char buf[4] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    /* "1" = Enforcing, "0" = Permissive */
    return (buf[0] == '1') ? 1 : 0;
}

void ks_selinux_dump_diag(void) {
    int en = ks_selinux_enforcing();
    KS_LOGE("-------- SELinux diagnostic --------");
    switch (en) {
        case 1:
            KS_LOGE("SELinux: Enforcing");
            KS_LOGE("Likely cause: sepolicy.rule not applied");
            KS_LOGE("App -> daemon socket connectto rule missing");
            KS_LOGE("Suggested fixes:");
            KS_LOGE("  1. Check daemon.log in module dir");
            KS_LOGE("  2. Run: dmesg | grep denied | grep -E 'connectto|kernelshoot'");
            KS_LOGE("  3. Run: ksud sepolicy apply <module>/sepolicy.rule");
            KS_LOGE("  4. Or reboot to let KSU reload rules at boot");
            KS_LOGE("  5. Last resort: setenforce 0 (NOT recommended)");
            break;
        case 0:
            KS_LOGE("SELinux: Permissive");
            KS_LOGE("SELinux is NOT blocking this. Cause is elsewhere:");
            KS_LOGE("  - Socket name conflict (another daemon running?)");
            KS_LOGE("  - Binary permission (chmod 0755?)");
            KS_LOGE("  - Run: ps -A | grep KernelShoot_daemon");
            break;
        default:
            KS_LOGE("SELinux: not enabled / cannot read /sys/fs/selinux/enforce");
            KS_LOGE("Cause is elsewhere (socket conflict or permission)");
            break;
    }
    KS_LOGE("-------------------------------------");
}
