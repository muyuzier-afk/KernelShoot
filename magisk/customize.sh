#!/system/bin/sh
# KernelShoot - customize.sh (Magisk 安装脚本)
# 在模块刷入时执行
#
# 期望的 zip 结构:
#   module.prop
#   service.sh
#   post-fs-data.sh
#   sepolicy.rule
#   customize.sh        (本文件)
#   KernelShoot_daemon   (arm64 原生二进制)
#   KernelShoot.apk      (可选, 交互层 app, 由 build.sh 打入)
SKIPUNZIP=0

ui_print "==================================="
ui_print " KernelShoot 安装中..."
ui_print "==================================="

# 1. 守护进程二进制: 安装到模块根目录, 设可执行
ui_print "- 安装守护进程二进制"
set_perm_recursive "$MODPATH" 0 0 0755 0644
if [ -f "$MODPATH/KernelShoot_daemon" ]; then
    set_perm "$MODPATH/KernelShoot_daemon" 0 0 0755 u:object_r:system_file:s0
    ui_print "  守护进程: $MODPATH/KernelShoot_daemon"
else
    ui_print "! 警告: 未找到 KernelShoot_daemon, 请用 build.sh 重新打包"
fi

# 2. 脚本权限
set_perm "$MODPATH/service.sh"        0 0 0755 u:object_r:system_file:s0
set_perm "$MODPATH/post-fs-data.sh"   0 0 0755 u:object_r:system_file:s0

# 3. (可选) 安装交互层 APK
if [ -f "$MODPATH/KernelShoot.apk" ]; then
    ui_print "- 安装交互层 APK"
    pm install -r "$MODPATH/KernelShoot.apk" 2>/dev/null \
        && ui_print "  APK 安装成功" \
        || ui_print "! APK 安装失败, 可手动 pm install"
else
    ui_print "- 未捆绑 APK (如需交互层请自行安装)"
fi

ui_print ""
ui_print " 安装完成. 重启后生效:"
ui_print "   1. 守护进程由 service.sh 自启"
ui_print "   2. 打开 KernelShoot app 启动通知"
ui_print "   3. 点击通知 → 3 秒后截图"
ui_print "   4. 截图保存于 /sdcard/DCIM/"
ui_print "==================================="
