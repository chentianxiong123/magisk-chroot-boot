#!/system/bin/sh
# chroot-network (TWRP): no net.dns1 (Android framework never writes it in
# recovery). DNS/gateway come from /data/adb/chroot-net.conf, written by
# wifi-up after the link is up. Route tools: prefer /system/bin/ip (TWRP may
# lack iproute2), fall back to toybox route.
MODDIR=${0%/*}
log() { echo "[chroot-network] $1" | tee -a /data/local/tmp/chroot-network.log; }
IP=/system/bin/ip
[ -x "$IP" ] || IP=ip

# Wait for wifi-up's network info + the chroot's /etc. Bounded.
i=0
while [ ! -f /data/adb/chroot-net.conf ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
[ -f /data/adb/chroot-net.conf ] || { log "! no /data/adb/chroot-net.conf (wifi-up failed?)"; exit 1; }
i=0
while [ ! -d /data/debian/etc ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
[ -d /data/debian/etc ] || { log "! /data/debian/etc missing"; exit 1; }

. /data/adb/chroot-net.conf
GW=${GW:-$($IP route show table 0 2>/dev/null | grep '^default' | head -1 | cut -d' ' -f3)}
R=/data/debian/etc/resolv.conf
: > "$R" 2>/dev/null
[ -n "$DNS1" ] && echo "nameserver $DNS1" >> "$R"
[ -n "$DNS2" ] && echo "nameserver $DNS2" >> "$R"
if ! grep -q '^nameserver' "$R" 2>/dev/null; then
  if [ -n "$GW" ]; then echo "nameserver $GW" >> "$R"; else echo "nameserver 223.5.5.5" >> "$R"; fi
fi
chmod 644 "$R" 2>/dev/null
log "DNS1=${DNS1:-none} DNS2=${DNS2:-none} GW=${GW:-none}"

# One-shot policy routing (same as system version, but uses $IP).
apply_routes() {
  ROUTE_CONF=/data/adb/chroot-routes.conf
  [ -f "$ROUTE_CONF" ] || return 0
  RULE_CIDRS=""
  RULE_PREF=9000
  . "$ROUTE_CONF"
  for cidr in $RULE_CIDRS; do
    case "$cidr" in
      */*)
        if $IP rule show 2>/dev/null | grep -q "^${RULE_PREF}:.*to ${cidr}.*lookup main"; then
          :
        elif $IP rule add to "$cidr" lookup main pref "$RULE_PREF" 2>/dev/null; then
          log "route rule added: $cidr -> main (pref $RULE_PREF)"
        else
          log "route rule add failed: $cidr"
        fi
        ;;
      *) log "invalid route CIDR: $cidr" ;;
    esac
  done
}
apply_routes
sleep 5
apply_routes
log "done"
