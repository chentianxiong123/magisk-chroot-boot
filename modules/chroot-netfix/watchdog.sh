#!/system/bin/sh
# Re-apply Android policy routing and ICMP echo-reply rules every 10s.
# Android's netd/Flyme may flush rules on Wi-Fi/mobile/power-state changes.
MODDIR=${0%/*}
LOG=/data/local/tmp/chroot-netfix.log
CONF=/data/adb/chroot-netfix.conf

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [netfix] $1" >> "$LOG"; }

ensure_rule() {
  cidr="$1"
  pref="$2"
  if ip rule show 2>/dev/null | grep -q "^${pref}:.*to ${cidr}.*lookup main"; then
    return 0
  fi
  if ip rule add to "$cidr" lookup main pref "$pref" 2>>"$LOG"; then
    log "added ip rule: to $cidr lookup main pref $pref"
  else
    log "failed to add ip rule: to $cidr lookup main pref $pref"
  fi
}

remove_rule() {
  cidr="$1"
  pref="$2"
  while ip rule del to "$cidr" lookup main pref "$pref" 2>/dev/null; do :; done
}

ensure_icmp() {
  chain="$1"
  if ! iptables -S "$chain" >/dev/null 2>&1; then
    [ "$chain_warned" = "0" ] && log "waiting for iptables chain $chain"
    chain_warned=1
    return 1
  fi
  chain_warned=0

  first=$(iptables -S "$chain" 2>/dev/null | grep '^-A ' | head -1)
  case "$first" in
    *"-p icmp"*"--icmp-type 0"*"-j RETURN"*) return 0 ;;
    *"-p icmp"*"--icmp-type echo-reply"*"-j RETURN"*) return 0 ;;
  esac

  while iptables -D "$chain" -p icmp -m icmp --icmp-type 0 -j RETURN 2>/dev/null; do :; done
  while iptables -D "$chain" -p icmp --icmp-type echo-reply -j RETURN 2>/dev/null; do :; done
  if iptables -I "$chain" 1 -p icmp -m icmp --icmp-type 0 -j RETURN 2>/dev/null; then
    log "inserted ICMP echo-reply RETURN at top of $chain"
  else
    log "failed to insert ICMP echo-reply rule in $chain"
  fi
}

missing_logged=0
chain_warned=0
while true; do
  RULE_CIDRS=""
  RULE_PREF=9000
  ICMP_ECHO_REPLY=0
  IPTABLES_CHAIN=fw_OUTPUT

  if [ -f "$CONF" ]; then
    missing_logged=0
    . "$CONF"
  else
    [ "$missing_logged" = "0" ] && log "no config at $CONF; idle"
    missing_logged=1
  fi

  for cidr in $RULE_CIDRS; do
    case "$cidr" in
      */*) ensure_rule "$cidr" "$RULE_PREF" ;;
      *) log "skip invalid CIDR: $cidr" ;;
    esac
  done

  [ "$ICMP_ECHO_REPLY" = "1" ] && ensure_icmp "$IPTABLES_CHAIN"

  sleep 10
done
