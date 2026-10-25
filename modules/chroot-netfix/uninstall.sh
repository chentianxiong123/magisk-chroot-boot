#!/system/bin/sh
# Best-effort cleanup when the module is removed. Runtime rules would also
# disappear on reboot; this removes them immediately.
CONF=/data/adb/chroot-netfix.conf
PIDFILE=/data/local/tmp/chroot-netfix.pid

if [ -f "$PIDFILE" ]; then
  pid=$(cat "$PIDFILE" 2>/dev/null)
  if [ -n "$pid" ] && tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q 'chroot-netfix/watchdog.sh'; then
    kill "$pid" 2>/dev/null
    sleep 1
  fi
  rm -f "$PIDFILE"
fi

RULE_CIDRS=""
RULE_PREF=9000
IPTABLES_CHAIN=fw_OUTPUT
[ -f "$CONF" ] && . "$CONF"

for cidr in $RULE_CIDRS; do
  while ip rule del to "$cidr" lookup main pref "$RULE_PREF" 2>/dev/null; do :; done
done

while iptables -D "$IPTABLES_CHAIN" -p icmp -m icmp --icmp-type 0 -j RETURN 2>/dev/null; do :; done
while iptables -D "$IPTABLES_CHAIN" -p icmp --icmp-type echo-reply -j RETURN 2>/dev/null; do :; done
