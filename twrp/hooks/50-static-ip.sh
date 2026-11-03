#!/system/bin/sh
# 50-static-ip.sh — 配IP (轮询ip link, wifi关联后配; 修ip命令的LD_LIBRARY_PATH)
# 根因: runner的LD_LIBRARY_PATH只有32位库, ip命令是64位需要lib64, 否则segfault静默失败

export LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib

# 等wlan0 LOWER_UP (wifi关联事件, 最多180秒)
i=0
while [ $i -lt 180 ]; do
    ip link show wlan0 2>/dev/null | grep -q 'LOWER_UP' && break
    sleep 1
    i=$((i+1))
done

# 关联后再等3秒(让IPv6/DHCP稳定)
sleep 3

# 配IP+路由(一次性, 此时wifi已关联)
ip addr add 192.168.31.213/24 dev wlan0 2>/dev/null
ip route add default via 192.168.31.1 dev wlan0 2>/dev/null

exit 0
