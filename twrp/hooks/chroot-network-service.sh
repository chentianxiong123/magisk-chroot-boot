#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done
while [ ! -d /data/debian/etc ]; do sleep 1; done
log() { echo "[chroot-network] $1" | tee -a /data/local/tmp/chroot-network.log; }

# WiFi/DHCP can finish after sys.boot_completed, so wait briefly for the
# active network's DNS, then fall back to the default gateway / public DNS.
i=0
while [ -z "$(getprop net.dns1)" ] && [ "$i" -lt 30 ]; do
  i=$((i+1))
  sleep 1
done
DNS1=$(getprop net.dns1)
DNS2=$(getprop net.dns2)
GW=$(ip route show table 0 2>/dev/null | grep '^default' | head -1 | cut -d' ' -f3)
R=/data/debian/etc/resolv.conf
: > "$R" 2>/dev/null
[ -n "$DNS1" ] && echo "nameserver $DNS1" >> "$R"
[ -n "$DNS2" ] && echo "nameserver $DNS2" >> "$R"
if ! grep -q '^nameserver' "$R" 2>/dev/null; then
  if [ -n "$GW" ]; then
    echo "nameserver $GW" >> "$R"
  else
    echo "nameserver 223.5.5.5" >> "$R"
  fi
fi
chmod 644 "$R" 2>/dev/null
log "DNS1=${DNS1:-none} DNS2=${DNS2:-none} GW=${GW:-none}"

# One-shot policy routing for raw TUNs. The TUN route lives in main, but
# Android netd policy routing sends ordinary traffic to wlan0/rmnet first.
# This is applied at boot only (no watchdog); use chroot-netfix if you need
# automatic re-apply on network/power changes.
apply_routes() {
  ROUTE_CONF=/data/adb/chroot-routes.conf
  [ -f "$ROUTE_CONF" ] || return 0
  RULE_CIDRS=""
  RULE_PREF=9000
  . "$ROUTE_CONF"
  for cidr in $RULE_CIDRS; do
    case "$cidr" in
      */*)
        if ip rule show 2>/dev/null | grep -q "^${RULE_PREF}:.*to ${cidr}.*lookup main"; then
          :
        elif ip rule add to "$cidr" lookup main pref "$RULE_PREF" 2>/dev/null; then
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
# netd may still be settling right after DHCP; one retry is enough for a
# fixed network and much cheaper than a permanent watchdog.
sleep 5
apply_routes
