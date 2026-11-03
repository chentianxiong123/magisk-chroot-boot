#!/usr/bin/env bash
set -euo pipefail

ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

adb_ shell 'su -c "
  echo -n \"magisk: \"; /sbin/magisk64 -V
  echo -n \"kernel: \"; uname -r
  echo
  /data/debian/bin/busybox chroot /data/debian \
    /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf status
"'
