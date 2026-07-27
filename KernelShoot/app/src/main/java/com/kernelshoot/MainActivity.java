package com.kernelshoot;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

/**
 * 启动入口 (仅用于首次启动前台服务, 之后可关闭).
 *
 * 注意: 此 Activity 只在用户主动从桌面打开时出现, 用于:
 *   - 启动 SnapForegroundService (Android 8+ 后台无法直接起前台服务)
 *   - 显示一次简短说明
 * 它不是截图触发路径 (截图走通知点击 -> BroadcastReceiver).
 */
public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        TextView tv = findViewById(R.id.tv_status);
        tv.setText(getString(R.string.main_status));

        startSnapService();
    }

    private void startSnapService() {
        Intent svc = new Intent(this, SnapForegroundService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }
    }
}
