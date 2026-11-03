#!/system/bin/sh
# One-shot post-boot tune. No watchdog.
#   1) wait boot_completed + WAIT_BOOT
#   2) wait for STATIC_IP on IFACE (so WiFi is really up before freezing it)
#   3) force-stop STOP_PKGS
#   4) SIGSTOP FREEZE_PROCS (skip FREEZE_NEEDS_NET when there is no network)

MODDIR=${0%/*}
LOG=/data/local/tmp/postboot-tune.log
CONF=/data/adb/postboot-tune.conf

WAIT_BOOT=40
IFACE=wlan0
STATIC_IP=192.168.31.213
WAIT_STATIC=150
STOP_PKGS="com.android.settings com.android.providers.downloads com.android.packageinstaller com.android.providers.calendar"
FREEZE_PROCS="com.mediatek.wfo.impl com.mediatek.gba com.meizu.experiencedatasync com.mediatek.atci.service com.mediatek.engineermode com.meizu.mzbasestationsafe com.meizu.wifiadmin"
FREEZE_NEEDS_NET="com.meizu.wifiadmin"

[ -f "$CONF" ] && . "$CONF"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [tune] $1" >> "$LOG"; }

i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 150 ]; do
  sleep 2
  i=$((i+1))
done
[ "$(getprop sys.boot_completed)" = "1" ] || { log "boot timeout, exit"; exit 0; }

sleep "$WAIT_BOOT"

i=0
while [ $i -lt $((WAIT_STATIC / 2)) ]; do
  ip -4 addr show "$IFACE" 2>/dev/null | grep -q "inet $STATIC_IP/" && break
  sleep 2
  i=$((i+1))
done
net_ok=0
ip -4 addr show "$IFACE" 2>/dev/null | grep -q "inet $STATIC_IP/" && net_ok=1
[ $net_ok = 1 ] || log "warn: $STATIC_IP not on $IFACE, network-dependent freezes will be skipped"

for p in $STOP_PKGS; do
  am force-stop "$p" >/dev/null 2>&1 && log "stopped $p"
done

for p in $FREEZE_PROCS; do
  case " $FREEZE_NEEDS_NET " in
    *" $p "*) [ $net_ok = 1 ] || { log "skip $p (no net)"; continue; } ;;
  esac
  pid=$(pidof "$p" 2>/dev/null)
  if [ -n "$pid" ]; then
    kill -STOP $pid 2>/dev/null && log "froze $p ($pid)"
  else
    log "no proc $p"
  fi
done

log "done, system_server=$(pidof system_server)"
exit 0
