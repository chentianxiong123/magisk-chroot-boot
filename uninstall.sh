#!/usr/bin/env bash
set -euo pipefail

ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

echo "==> stopping supervisord"
adb_ shell 'su -c "/data/debian/bin/busybox chroot /data/debian /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf shutdown"' || true

echo "==> removing modules"
adb_ shell 'su -c "rm -rf /data/adb/modules/chroot-mount /data/adb/modules/chroot-network /data/adb/modules/chroot-services /data/adb/modules/adb-tcp"'

if [ "${1:-}" = "--purge" ]; then
  echo "==> purging supervisor config"
  adb_ shell 'su -c "rm -f /data/debian/etc/supervisor/conf.d/chroot.conf"'
fi

echo "done."
