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
