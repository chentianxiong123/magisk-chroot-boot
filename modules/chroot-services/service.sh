#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done
while [ ! -f /data/debian/proc/version ]; do sleep 1; done
D=/data/debian
log() { echo "[services] $1" | tee -a /data/local/tmp/services.log; }
log "Starting supervisord..."
n=0
while [ $n -lt 3 ]; do
  n=$((n+1))
  setsid $D/bin/busybox chroot $D /usr/bin/supervisord -c /etc/supervisor/supervisord.conf
  sleep 4
  if $D/bin/busybox chroot $D /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf pid >/dev/null 2>&1; then
    log "Supervisord up (attempt $n)"
    break
  fi
  log "Supervisord attempt $n failed"
done
log "Starting adbd 5555..."
setprop service.adb.tcp.port 5555
stop adbd
start adbd
log "All done"
