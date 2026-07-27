package com.kernelshoot;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.HandlerThread;
import android.text.TextUtils;
import android.util.Log;

/**
 * 静态注册的 BroadcastReceiver: 通知点击后触发截图.
 *
 * 设计要点 (针对"不影响前台应用"):
 *  1. 不创建 Activity / Window / Surface -> 不抢焦点, 不触发前台 onPause
 *  2. onReceive() 只弹一个 Toast (系统窗口层, 无焦点) 并安排延时任务,
 *     随即在 ~10ms 内返回, 不会触发 ANR (10s 限制)
 *  3. 耗时的 socket 通信在独立 HandlerThread 子线程进行
 *
 * 触发链:
 *   通知点击 -> PendingIntent.getBroadcast -> 本 Receiver -> 3s 倒计时 ->
 *   LocalSocket -> 守护进程截图 -> 返回路径 -> Toast
 */
public class SnapReceiver extends BroadcastReceiver {
    private static final String TAG = "SnapReceiver";

    /** 用一个后台 HandlerThread 跑延时与网络, 避免占用主线程 */
    private static HandlerThread sWorker;
    private static Handler sWorkerHandler;

    private static synchronized Handler workerHandler() {
        if (sWorker == null) {
            sWorker = new HandlerThread("KernelShoot-Worker");
            sWorker.start();
            sWorkerHandler = new Handler(sWorker.getLooper());
        }
        return sWorkerHandler;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !DaemonConst.ACTION_SHOOT.equals(intent.getAction())) {
            return;
        }

        final Context appCtx = context.getApplicationContext();

        // 1. 立即提示倒计时 (Toast 不抢焦点)
        ToastHelper.show(appCtx,
                appCtx.getString(R.string.toast_countdown, DaemonConst.COUNTDOWN_SECONDS));

        // 2. 安排延时截图任务 (子线程), onReceive 立即返回
        workerHandler().postDelayed(new Runnable() {
            @Override
            public void run() {
                doShoot(appCtx);
            }
        }, DaemonConst.COUNTDOWN_SECONDS * 1000L);
    }

    /** 子线程: 连接守护进程, 发截图指令, 显示结果 */
    private void doShoot(final Context ctx) {
        SocketClient.Result r = SocketClient.requestScreenshot();
        if (r.ok() && !TextUtils.isEmpty(r.path)) {
            ToastHelper.showLong(ctx, ctx.getString(R.string.toast_success, r.path));
            Log.i(TAG, "screenshot ok: " + r.path);
        } else {
            ToastHelper.showLong(ctx,
                    ctx.getString(R.string.toast_failed, r.code, r.msg));
            Log.e(TAG, "screenshot failed code=" + r.code + " msg=" + r.msg);
        }
    }
}
