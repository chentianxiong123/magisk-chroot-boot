#!/system/bin/sh
# Start the netfix watchdog once. The watchdog applies the rules from
# /data/adb/chroot-netfix.conf and re-applies them after network/firewall
# changes. It must survive the module's service.sh exiting, so use setsid.
MODDIR=${0%/*}
LOG=/data/local/tmp/chroot-netfix.log
PIDFILE=/data/local/tmp/chroot-netfix.pid

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [netfix] $1" >> "$LOG"; }

while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done

if [ -f "$PIDFILE" ]; then
  old=$(cat "$PIDFILE" 2>/dev/null)
  if [ -n "$old" ] && tr '\0' ' ' < "/proc/$old/cmdline" 2>/dev/null | grep -q 'chroot-netfix/watchdog.sh'; then
    kill "$old" 2>/dev/null
    sleep 1
    kill -9 "$old" 2>/dev/null
    log "stopped old watchdog pid=$old"
  fi
fi

setsid nohup sh "$MODDIR/watchdog.sh" >/dev/null 2>&1 < /dev/null &
echo $! > "$PIDFILE"
log "watchdog started pid=$!"
