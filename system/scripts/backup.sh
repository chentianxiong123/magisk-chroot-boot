#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="$ROOT/backups"
mkdir -p "$OUT"

adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

adb_ shell "su -c 'cd / && F=\"data/adb/modules data/debian/etc/supervisor\"; for f in data/adb/chroot-routes.conf data/adb/chroot-netfix.conf data/adb/static-ip.conf data/adb/postboot-tune.conf; do [ -f \"\$f\" ] && F=\"\$F \$f\"; done; /data/debian/bin/busybox tar czf /data/local/tmp/chroot-backup.tgz \$F'"
adb_ pull /data/local/tmp/chroot-backup.tgz "$OUT/chroot-backup-$STAMP.tgz" >/dev/null
adb_ shell 'su -c "rm -f /data/local/tmp/chroot-backup.tgz"'

ls -lh "$OUT/chroot-backup-$STAMP.tgz"
