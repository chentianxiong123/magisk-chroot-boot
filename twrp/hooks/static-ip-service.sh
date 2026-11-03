#!/system/bin/sh
# one-shot: after boot, replace the DHCP address with the static one. No loop.

IP=192.168.31.213
PREFIX=24
IFACE=wlan0
LOG=/data/adb/static-ip.log

log() { echo "[static-ip] $(date '+%m-%d %H:%M:%S') $1" >> "$LOG"; }

addrs() {
  ip -4 -o addr show dev "$IFACE" 2>/dev/null | sed -n 's/.*inet \([0-9.]*\/[0-9]*\).*/\1/p'
}

i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 150 ]; do
  sleep 2
  i=$((i+1))
done

i=0
while [ $i -lt 90 ]; do
  i=$((i+1))
  addrs | grep -q '^192\.168\.31\.' && break
  sleep 2
done
addrs | grep -q '^192\.168\.31\.' || { log "no 192.168.31.x on $IFACE, skip"; exit 0; }

sleep 2
echo 1 > /proc/sys/net/ipv4/conf/$IFACE/promote_secondaries 2>/dev/null
echo 1 > /proc/sys/net/ipv4/conf/all/promote_secondaries 2>/dev/null

ip addr add "$IP/$PREFIX" dev "$IFACE" 2>/dev/null && log "added $IP/$PREFIX"

for a in $(addrs); do
  [ "$a" = "$IP/$PREFIX" ] && continue
  ip addr del "$a" dev "$IFACE" 2>/dev/null && log "removed $a"
done

log "final: $(addrs | tr '\n' ' ')"
exit 0
