#!/system/bin/sh
# chroot-services (TWRP): no sys.boot_completed (never set in recovery).
# Trigger = network info present (wifi-up done) + chroot mounted.
# Keeps su - root: the kernel enforces CONFIG_ANDROID_PARANOID_NETWORK, so
# supervisord/services need inet(3003)/net_raw(3004) from the chroot's
# /etc/group. That is a kernel restriction, not an Android framework one.
MODDIR=${0%/*}
D=/data/debian
log() { echo "[services] $1" | tee -a /data/local/tmp/services.log; }

# Wait for wifi-up's net info + chroot mounts. Bounded.
i=0
while [ ! -f /data/adb/chroot-net.conf ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
i=0
while [ ! -f "$D/proc/version" ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
[ -f "$D/proc/version" ] || { log "! chroot not mounted (/data/debian/proc/version)"; exit 1; }

unset LD_PRELOAD LD_LIBRARY_PATH
log "Starting supervisord..."
n=0
while [ $n -lt 3 ]; do
  n=$((n+1))
  # su - root applies the chroot's /etc/group (inet 3003 / net_raw 3004) so
  # supervisord and every service inherit network permission. A plain chroot
  # would keep empty supplementary groups.
  setsid $D/bin/busybox chroot $D /bin/su -s /bin/sh - root -c "exec /usr/bin/supervisord -c /etc/supervisor/supervisord.conf"
  sleep 4
  if $D/bin/busybox chroot $D /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf pid >/dev/null 2>&1; then
    log "Supervisord up (attempt $n)"
    break
  fi
  log "Supervisord attempt $n failed"
done
log "All done"
