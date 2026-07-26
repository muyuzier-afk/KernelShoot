package com.kernelshoot;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

import androidx.core.app.NotificationCompat;

/**
 * 前台服务: 持有一个持久低打扰通知.
 *
 *  - 通知渠道 IMPORTANCE_LOW: 无声音、无振动、不弹横幅
 *  - 通知点击 -> PendingIntent.getBroadcast -> SnapReceiver (不启动 Activity)
 *  - 服务常驻以保持通知存在; 真正的截图由 BroadcastReceiver + 守护进程完成
 *
 * 不在此服务内做任何窗口/Surface 操作, 因此不影响前台应用.
 */
public class SnapForegroundService extends Service {

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();
        startForeground(DaemonConst.NOTIFICATION_ID, buildNotification());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // 常驻直到被显式停止
        return START_STICKY;
    }

    private void createChannel() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm == null) return;
        NotificationChannel ch = new NotificationChannel(
                DaemonConst.CHANNEL_ID,
                getString(R.string.channel_name),
                NotificationManager.IMPORTANCE_LOW);
        ch.setDescription(getString(R.string.channel_desc));
        ch.setShowBadge(false);
        ch.enableLights(false);
        ch.enableVibration(false);
        nm.createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        Intent tap = new Intent(DaemonConst.ACTION_SHOOT);
        tap.setPackage(getPackageName());

        // FLAG_IMMUTABLE (Android 12+ 强制); FLAG_UPDATE_CURRENT 保证每次可点击
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pi = PendingIntent.getBroadcast(this, 0, tap, flags);

        return new NotificationCompat.Builder(this, DaemonConst.CHANNEL_ID)
                .setContentTitle(getString(R.string.notif_title))
                .setContentText(getString(R.string.notif_text))
                .setSmallIcon(R.drawable.ic_notification)
                .setOngoing(true)
                .setContentIntent(pi)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build();
    }
}
