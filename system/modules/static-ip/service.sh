#!/system/bin/sh
# One-shot static IP: after boot, add STATIC_IP and drop the DHCP lease.
# Config: /data/adb/static-ip.conf (falls back to the values below).

MODDIR=${0%/*}
LOG=/data/local/tmp/static-ip.log
CONF=/data/adb/static-ip.conf

STATIC_IP=192.168.31.213
PREFIX=24
IFACE=wlan0
SUBNET=192.168.31.

[ -f "$CONF" ] && . "$CONF"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [static-ip] $1" >> "$LOG"; }

addrs() {
  ip -4 -o addr show dev "$IFACE" 2>/dev/null | sed -n 's/.*inet \([0-9.]*\/[0-9]*\).*/\1/p'
}

i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 150 ]; do
  sleep 2
  i=$((i+1))
done
[ "$(getprop sys.boot_completed)" = "1" ] || { log "boot timeout, exit"; exit 0; }

# Wait for the DHCP lease; without it there is nothing to replace.
i=0
while [ $i -lt 90 ]; do
  addrs | grep -q "^${SUBNET}" && break
  sleep 2
  i=$((i+1))
done
addrs | grep -q "^${SUBNET}" || { log "no ${SUBNET}x address on $IFACE, skip"; exit 0; }

sleep 2

# Keep the secondary address alive when the primary (DHCP) one is deleted.
echo 1 > /proc/sys/net/ipv4/conf/$IFACE/promote_secondaries 2>/dev/null
echo 1 > /proc/sys/net/ipv4/conf/all/promote_secondaries 2>/dev/null

ip addr add "$STATIC_IP/$PREFIX" dev "$IFACE" 2>/dev/null && log "added $STATIC_IP/$PREFIX"

for a in $(addrs); do
  [ "$a" = "$STATIC_IP/$PREFIX" ] && continue
  ip addr del "$a" dev "$IFACE" 2>/dev/null && log "removed $a"
done

log "final: $(addrs | tr '\n' ' ')"
exit 0
