#!/system/bin/sh
# 40-adbd-tcp.sh — 开启无线 adb (WiFi adb, 改造自 Magisk adb-tcp 模块)
# hybrid recovery 没 init 框架, kill adbd 后靠 recovery init 拉起(带TCP)

# 设 TCP 端口
setprop service.adb.tcp.port 5555 2>/dev/null

# 重启 adbd (recovery init 会带 TCP 拉起)
# 注意: 不用 stop/start adbd (recovery 没 init 框架), 直接 kill 让它重启
kill $(pidof adbd) 2>/dev/null

# 等 adbd 重新监听
sleep 3

exit 0
