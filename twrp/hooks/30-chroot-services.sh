#!/system/bin/sh
# 30-chroot-services.sh — 照面具原文, 只去掉等框架的循环(getprop sys.boot_completed)

while [ ! -f /data/debian/proc/version ]; do sleep 1; done
D=/data/debian
log() { echo "[services] $1" | tee -a /data/local/tmp/services.log; }
unset LD_PRELOAD LD_LIBRARY_PATH
log "Starting supervisord..."
n=0
while [ $n -lt 3 ]; do
  n=$((n+1))
  setsid $D/bin/busybox chroot $D /bin/su -s /bin/sh - root -c "exec /usr/bin/supervisord -c /etc/supervisor/supervisord.conf"
  sleep 4
  if $D/bin/busybox chroot $D /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf pid >/dev/null 2>&1; then
    log "Supervisord up (attempt $n)"
    break
  fi
  log "Supervisord attempt $n failed"
done
log "All done"
