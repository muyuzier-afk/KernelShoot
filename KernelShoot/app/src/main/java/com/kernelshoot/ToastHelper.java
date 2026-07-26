package com.kernelshoot;

import android.content.Context;
import android.widget.Toast;

/**
 * Toast 工具.
 *
 * 关键点: Toast 是系统窗口层, 不获取输入焦点, 不触发前台 Activity 生命周期回调,
 * 因此从 BroadcastReceiver 弹出 Toast 完全不影响当前前台应用.
 *
 * 注意: BroadcastReceiver 上下文可直接 show Toast, 但耗时操作必须切子线程.
 */
public final class ToastHelper {
    private ToastHelper() {}

    public static void show(final Context ctx, final String msg) {
        Toast.makeText(ctx.getApplicationContext(), msg, Toast.LENGTH_SHORT).show();
    }

    public static void show(final Context ctx, final int resId) {
        Toast.makeText(ctx.getApplicationContext(), resId, Toast.LENGTH_SHORT).show();
    }

    public static void showLong(final Context ctx, final String msg) {
        Toast.makeText(ctx.getApplicationContext(), msg, Toast.LENGTH_LONG).show();
    }
}
