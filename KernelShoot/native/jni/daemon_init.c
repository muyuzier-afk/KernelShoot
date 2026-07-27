/*
 * KernelShoot - daemon_init.c
 * 守护进程化：双重 fork + setsid, 关闭标准流, 重设 umask
 *
 * 流程：
 *   parent fork -> child1 fork -> grandchild (被 init 收养, PPID=1)
 *   每代父进程 exit(0)
 *   孙进程 setsid() 成为新会话组长, 脱离控制终端
 *
 * 这样守护进程:
 *   - 与启动 shell 无关联 (无控制终端, 不会收到 SIGHUP)
 *   - 不再是进程组组长, 不会被信号广播误伤
 *   - 被 init 收养, 存活优先级最高
 */
#include "common.h"

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

/* 重定向 stdin/stdout/stderr 到 /dev/null, 避免干扰 */
static void redirect_std_fd(void) {
    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) return;
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > STDERR_FILENO) close(devnull);
}

int ks_daemonize(void) {
    /* 第一次 fork */
    pid_t pid = fork();
    if (pid < 0) {
        KS_LOGE("first fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        /* 父进程退出 */
        _exit(0);
    }

    /* 子进程: 成为新会话组长, 脱离控制终端 */
    if (setsid() < 0) {
        KS_LOGE("setsid failed: %s", strerror(errno));
        return -1;
    }

    /* 第二次 fork: 确保未来无法重新获取控制终端 */
    pid = fork();
    if (pid < 0) {
        KS_LOGE("second fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    /* 孙进程 (守护进程本体) */
    /* 清除文件创建掩码, 完全控制权限 */
    umask(0);

    /* 切换到根目录, 避免占用挂载点 */
    if (chdir("/") < 0) {
        /* 非致命 */
    }

    /* 忽略常见的终止信号 (实际保活由 service.sh while 循环兜底) */
    signal(SIGHUP,  SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    redirect_std_fd();

    KS_LOGI("daemonized: pid=%d ppid=%d sid=%d",
            (int)getpid(), (int)getppid(), (int)getsid(0));
    return 0;
}
