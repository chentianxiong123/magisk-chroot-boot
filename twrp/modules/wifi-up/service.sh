#!/system/bin/sh
# wifi-up (TWRP): bring up WiFi from /system, no Android framework.
# Sequence: /system mount -> /vendor symlink -> wait wlan0 -> wpa_supplicant
#           -> connect -> static IP/DHCP -> write /data/adb/chroot-net.conf
# Self-sync: waits for /data/adb/twrp-wifi.conf; others wait for chroot-net.conf.
MODDIR=${0%/*}
log() { echo "[wifi-up] $1" | tee -a /data/local/tmp/wifi-up.log; }

# 1. Wait for config (TWRP mounts /data). Bounded so failure is visible.
i=0
while [ ! -f /data/adb/twrp-wifi.conf ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
if [ ! -f /data/adb/twrp-wifi.conf ]; then log "! no /data/adb/twrp-wifi.conf"; exit 1; fi
. /data/adb/twrp-wifi.conf
log "config loaded"

# 2. Mount /system (TWRP usually has it; ensure for tools/libs/firmware)
mount /system 2>/dev/null
[ -d /system/bin ] || { log "! /system not mounted"; exit 1; }

# 3. /vendor symlink (MTK: vendor content under /system/vendor). Firmware is
#    loaded on-demand by the kernel driver from here.
[ -e /vendor ] || ln -sf /system/vendor /vendor
[ -e /vendor/firmware ] || ln -sf /system/vendor/firmware /vendor/firmware 2>/dev/null

# 4. Wait for wlan0 (kernel loads firmware from /vendor/firmware on probe)
i=0
while [ ! -e /sys/class/net/wlan0 ] && [ $i -lt 60 ]; do i=$((i+1)); sleep 1; done
[ -e /sys/class/net/wlan0 ] || { log "! wlan0 not present (firmware?)"; exit 1; }
log "wlan0 present"

# 5. Generate wpa_supplicant.conf and start wpa_supplicant from /system.
#    Running a /system binary in TWRP needs /system libs + linker; /system is
#    mounted, so PATH/LD_LIBRARY_PATH point there.
cat > /data/adb/twrp-wpa.conf <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=CN
network={
    ssid="$WIFI_SSID"
    psk="$WIFI_PSK"
    key_mgmt=WPA-PSK
}
EOF
chmod 600 /data/adb/twrp-wpa.conf 2>/dev/null
PATH=/system/bin:/system/sbin:/sbin:$PATH
export LD_LIBRARY_PATH=/system/lib
ifconfig wlan0 up 2>/dev/null
wpa_supplicant -Dnl80211 -iwlan0 -c /data/adb/twrp-wpa.conf -B 2>/dev/null || \
  wpa_supplicant -iwlan0 -c /data/adb/twrp-wpa.conf -B 2>/dev/null
log "wpa_supplicant started"

# 6. Wait for connection (COMPLETED)
i=0
while [ $i -lt 60 ]; do
  st=$(wpa_cli -iwlan0 status 2>/dev/null | grep -m1 'wpa_state=')
  [ "${st#*=}" = "COMPLETED" ] && { log "wifi COMPLETED"; break; }
  i=$((i+1)); sleep 1
done

# 7. Configure IP: static if WIFI_IP set, else DHCP
if [ -n "$WIFI_IP" ]; then
  ifconfig wlan0 "$WIFI_IP" netmask "$WIFI_MASK" 2>/dev/null
  ip route add default via "$WIFI_GW" dev wlan0 2>/dev/null
else
  udhcpc -i wlan0 2>/dev/null
fi
sleep 2

# 8. Write network info for chroot-network (DNS + gateway)
GW=${WIFI_GW:-$(ip route show table 0 2>/dev/null | grep '^default' | head -1 | cut -d' ' -f3)}
{
  echo "DNS1=${WIFI_DNS1:-$GW}"
  [ -n "$WIFI_DNS2" ] && echo "DNS2=$WIFI_DNS2"
  [ -n "$GW" ] && echo "GW=$GW"
} > /data/adb/chroot-net.conf
chmod 644 /data/adb/chroot-net.conf 2>/dev/null
log "chroot-net.conf: $(cat /data/adb/chroot-net.conf 2>/dev/null | tr '\n' ' ')"
log "wlan0: $(ifconfig wlan0 2>/dev/null | grep -m1 'inet' | tr -s ' ')"
