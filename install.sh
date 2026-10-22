#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

echo "==> checking adb device"
adb_ get-state >/dev/null

echo "==> checking root"
adb_ shell 'su -c "id"' | grep -q 'uid=0' || {
  echo "!! root not available via: su -c"
  exit 1
}

echo "==> checking /data/debian"
adb_ shell 'su -c "test -d /data/debian"' || {
  echo "!! /data/debian not found"
  exit 1
}

mkdir -p "$TMP/stage"
cp -a "$ROOT/modules" "$TMP/stage/"

# Precedence:
#   1. config/services.conf  (local, gitignored, real services)
#   2. keep whatever is already on the device
#   3. example from supervisor/chroot.conf.tpl
if [ -f "$ROOT/config/services.conf" ]; then
  mkdir -p "$TMP/stage/supervisor"
  cp "$ROOT/config/services.conf" "$TMP/stage/supervisor/chroot.conf"
  echo "==> using local config/services.conf"
elif adb_ shell 'su -c "test -f /data/debian/etc/supervisor/conf.d/chroot.conf"'; then
  echo "==> existing device config detected; leaving it untouched"
else
  mkdir -p "$TMP/stage/supervisor"
  cp "$ROOT/supervisor/chroot.conf.tpl" "$TMP/stage/supervisor/chroot.conf"
  echo "==> no config provided; installing example services"
fi

tar czf "$TMP/setup.tgz" -C "$TMP/stage" .
adb_ push "$TMP/setup.tgz" /data/local/tmp/chroot-setup.tgz >/dev/null
adb_ push "$ROOT/scripts/device-install.sh" /data/local/tmp/chroot-device-install.sh >/dev/null

echo "==> installing on device"
adb_ shell 'su -c "sh /data/local/tmp/chroot-device-install.sh /data/local/tmp/chroot-setup.tgz"'

echo
echo "done. reboot to verify:"
echo "  adb reboot && sleep 80 && ./scripts/healthcheck.sh"
