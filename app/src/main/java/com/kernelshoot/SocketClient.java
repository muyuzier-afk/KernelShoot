package com.kernelshoot;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.util.Log;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * LocalSocket 客户端: 连接 native 守护进程 (抽象命名空间).
 *
 * 协议: 4 字节大端长度 + JSON.
 *
 * 特点:
 *  - 全程在子线程调用 (BroadcastReceiver 的 HandlerThread 中)
 *  - 5 秒超时保护, 避免阻塞
 *  - 纯 Java LocalSocket, 不依赖 JNI
 */
public final class SocketClient {
    private static final String TAG = DaemonConst.class.getSimpleName();

    /** 截图请求结果 */
    public static final class Result {
        public final int code;          // 0 成功, 非 0 守护进程错误码
        public final String msg;
        public final String path;       // 成功时的 JPEG 路径

        public Result(int code, String msg, String path) {
            this.code = code;
            this.msg = msg;
            this.path = path;
        }

        public boolean ok() { return code == 0; }
    }

    private SocketClient() {}

    /** 发送截图请求, 阻塞直到返回或超时. 必须在子线程调用. */
    public static Result requestScreenshot() {
        LocalSocket socket = null;
        try {
            socket = new LocalSocket();
            // 注意: 必须用 connect(addr) 无参超时重载.
            // Android 的 LocalSocketImpl.connect(addr, timeout) 在部分系统上
            // 直接抛 UnsupportedOperationException ("not supported"), 而
            // connect(addr) 走 connectLocal 路径, 是稳定可用实现.
            // 读取超时仍由下方 setSoTimeout 控制.
            socket.connect(new LocalSocketAddress(
                    DaemonConst.SOCKET_NAME,
                    LocalSocketAddress.Namespace.ABSTRACT));
            socket.setSoTimeout(DaemonConst.SOCKET_TIMEOUT_MS);

            DataOutputStream out = new DataOutputStream(socket.getOutputStream());
            byte[] req = DaemonConst.REQ_SCREENSHOT.getBytes(StandardCharsets.UTF_8);
            out.writeInt(req.length);          // 4 字节大端长度
            out.write(req);
            out.flush();

            DataInputStream in = new DataInputStream(socket.getInputStream());
            int respLen = in.readInt();        // 4 字节大端长度
            if (respLen <= 0 || respLen > 64 * 1024) {
                return new Result(-1, "bad response length: " + respLen, null);
            }
            byte[] resp = new byte[respLen];
            in.readFully(resp);
            String json = new String(resp, StandardCharsets.UTF_8);
            return parseResponse(json);
        } catch (Exception e) {
            // e.getMessage() 可能为 null (如某些 ErrnoException 包装), 拼上类名便于排查
            String detail = e.getClass().getSimpleName();
            if (e.getMessage() != null) detail += ": " + e.getMessage();
            Log.e(TAG, "requestScreenshot failed: " + e, e);
            return new Result(-1, "daemon unavailable: " + detail, null);
        } finally {
            if (socket != null) {
                try { socket.close(); } catch (Exception ignored) {}
            }
        }
    }

    /** 极简 JSON 解析: {"code":0,"msg":"ok","path":"/sdcard/..."} */
    private static Result parseResponse(String json) {
        int code = readInt(json, "code", -1);
        String msg = readString(json, "msg", "");
        String path = readString(json, "path", "");
        return new Result(code, msg, path);
    }

    private static int readInt(String json, String key, int def) {
        String s = readString(json, key, null);
        if (s == null) return def;
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return def; }
    }

    private static String readString(String json, String key, String def) {
        // 查找 "key" 后第一个字符串值
        String pat = "\"" + key + "\"";
        int i = json.indexOf(pat);
        if (i < 0) return def;
        int colon = json.indexOf(':', i + pat.length());
        if (colon < 0) return def;
        int q1 = json.indexOf('"', colon + 1);
        if (q1 < 0) return def;
        int q2 = q1 + 1;
        StringBuilder sb = new StringBuilder();
        while (q2 < json.length()) {
            char c = json.charAt(q2);
            if (c == '\\' && q2 + 1 < json.length()) {
                sb.append(json.charAt(q2 + 1));
                q2 += 2;
                continue;
            }
            if (c == '"') break;
            sb.append(c);
            q2++;
        }
        return sb.toString();
    }
}
