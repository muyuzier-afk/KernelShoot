# KernelShoot proguard 规则
# 守护进程通过 LocalSocket 通信, 不依赖反射, 默认规则即可.
# 保留 SocketClient.Result (可能有反射访问, 实际不会, 但保险起见).
-keep class com.kernelshoot.SocketClient$Result { *; }
