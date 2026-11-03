#!/usr/bin/env bash
set -euo pipefail

ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

echo "==> stopping supervisord"
adb_ shell 'su -c "/data/debian/bin/busybox chroot /data/debian /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf shutdown"' || true

echo "==> cleaning chroot-netfix"
adb_ shell 'su -c "sh /data/adb/modules/chroot-netfix/uninstall.sh"' || true

echo "==> removing modules"
adb_ shell 'su -c "rm -rf /data/adb/modules/chroot-mount /data/adb/modules/chroot-network /data/adb/modules/chroot-services /data/adb/modules/chroot-netfix /data/adb/modules/adb-tcp /data/adb/modules/static-ip /data/adb/modules/postboot-tune"'

if [ "${1:-}" = "--purge" ]; then
  echo "==> purging supervisor + netfix/static-ip/postboot config"
  adb_ shell 'su -c "rm -f /data/debian/etc/supervisor/conf.d/chroot.conf /data/adb/chroot-netfix.conf /data/adb/chroot-routes.conf /data/adb/static-ip.conf /data/adb/postboot-tune.conf"'
fi

echo "done."
