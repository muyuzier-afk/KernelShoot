package com.kernelshoot;

/**
 * 与 native 守护进程通信的常量.
 * 抽象命名空间名字必须与 native/jni/include/common.h 的 KS_SOCKET_NAME 一致.
 * 协议: 4 字节大端长度前缀 + JSON 负载.
 */
public final class DaemonConst {
    private DaemonConst() {}

    /** 抽象命名空间 socket 名 (与 native KS_SOCKET_NAME 一致) */
    public static final String SOCKET_NAME = "kernelshoot.daemon";

    /** 请求 JSON */
    public static final String REQ_SCREENSHOT = "{\"cmd\":\"screenshot\"}";

    /** 通信超时 (ms) */
    public static final int SOCKET_TIMEOUT_MS = 5000;

    /** 倒计时秒数 */
    public static final int COUNTDOWN_SECONDS = 3;

    /** 通知渠道 */
    public static final String CHANNEL_ID = "kernelshoot_channel";

    /** 通知 ID */
    public static final int NOTIFICATION_ID = 0x4B53; // "KS"

    /** 广播 action: 通知点击触发截图 */
    public static final String ACTION_SHOOT =
            "com.kernelshoot.action.SHOOT";
}
