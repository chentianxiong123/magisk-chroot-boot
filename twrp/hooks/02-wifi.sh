#!/system/bin/sh
# 02-wifi.sh — hybrid recovery WiFi (wpa_cli reassociate一次触发关联)
# wpa_cli会segfault但timeout+重定向兜住; runner会过滤Aborted

# 1. 挂 system + vendor
grep -q " /system " /proc/mounts 2>/dev/null || mount -o rw /dev/block/mmcblk0p29 /system 2>/dev/null
[ -L /vendor ] || ln -sf /system/vendor /vendor 2>/dev/null

# 2. wmt_loader (只能跑一次)
[ -e /dev/wmtWifi ] || /system/vendor/bin/wmt_loader >/dev/null 2>&1

# 3. wmt_launcher (后台)
/system/vendor/bin/wmt_launcher -p /vendor/firmware/ >/dev/null 2>&1 < /dev/null &
sleep 10

# 4. 开 wifi + wlan0 up
printf "1\n" > /dev/wmtWifi 2>/dev/null
sleep 3
ip link set wlan0 up 2>/dev/null

# 5. wpa_supplicant (64位需要lib64)
mkdir -p /data/misc/wifi/sockets 2>/dev/null
chmod 777 /data/misc/wifi/sockets 2>/dev/null
cat > /data/wpa.conf <<WCONF
ctrl_interface=/data/misc/wifi/sockets
network={
    ssid="Xiaomi_F5A3"
    psk="123456789"
    key_mgmt=WPA-PSK
}
WCONF
killall wpa_supplicant 2>/dev/null
sleep 1
LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib \
    /system/bin/wpa_supplicant -Dnl80211 -iwlan0 -c /data/wpa.conf >/dev/null 2>&1 < /dev/null &
sleep 5

# 6. reassociate一次 (timeout兜segfault; 不循环)
timeout 3 /system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 reassociate </dev/null >/dev/null 2>&1

# 7. 等关联完成
sleep 20

exit 0
