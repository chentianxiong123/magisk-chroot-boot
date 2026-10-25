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

# chroot-network one-shot policy routes, same precedence:
#   1. config/routes.conf  (local, gitignored, real CIDRs)
#   2. keep whatever is already on the device
#   3. example (inactive)
if [ -f "$ROOT/config/routes.conf" ]; then
  cp "$ROOT/config/routes.conf" "$TMP/stage/routes.conf"
  echo "==> using local config/routes.conf"
elif adb_ shell 'su -c "test -f /data/adb/chroot-routes.conf"'; then
  echo "==> existing route config detected; leaving it untouched"
else
  cp "$ROOT/modules/chroot-network/routes.conf.example" "$TMP/stage/routes.conf"
  echo "==> no route config provided; installing inactive example"
fi

# chroot-netfix config, same precedence:
#   1. config/netfix.conf  (local, gitignored, real CIDRs)
#   2. keep whatever is already on the device
#   3. example (inactive)
if [ -f "$ROOT/config/netfix.conf" ]; then
  cp "$ROOT/config/netfix.conf" "$TMP/stage/netfix.conf"
  echo "==> using local config/netfix.conf"
elif adb_ shell 'su -c "test -f /data/adb/chroot-netfix.conf"'; then
  echo "==> existing netfix config detected; leaving it untouched"
else
  cp "$ROOT/modules/chroot-netfix/netfix.conf.example" "$TMP/stage/netfix.conf"
  echo "==> no netfix config provided; installing inactive example"
fi

# static-ip config, same precedence.
if [ -f "$ROOT/config/static-ip.conf" ]; then
  cp "$ROOT/config/static-ip.conf" "$TMP/stage/static-ip.conf"
  echo "==> using local config/static-ip.conf"
elif adb_ shell 'su -c "test -f /data/adb/static-ip.conf"'; then
  echo "==> existing static-ip config detected; leaving it untouched"
else
  cp "$ROOT/modules/static-ip/static-ip.conf.example" "$TMP/stage/static-ip.conf"
  echo "==> no static-ip config provided; installing example"
fi

# postboot-tune config, same precedence.
if [ -f "$ROOT/config/postboot-tune.conf" ]; then
  cp "$ROOT/config/postboot-tune.conf" "$TMP/stage/postboot-tune.conf"
  echo "==> using local config/postboot-tune.conf"
elif adb_ shell 'su -c "test -f /data/adb/postboot-tune.conf"'; then
  echo "==> existing postboot-tune config detected; leaving it untouched"
else
  cp "$ROOT/modules/postboot-tune/postboot-tune.conf.example" "$TMP/stage/postboot-tune.conf"
  echo "==> no postboot-tune config provided; installing example"
fi

tar czf "$TMP/setup.tgz" -C "$TMP/stage" .
adb_ push "$TMP/setup.tgz" /data/local/tmp/chroot-setup.tgz >/dev/null
adb_ push "$ROOT/scripts/device-install.sh" /data/local/tmp/chroot-device-install.sh >/dev/null

echo "==> installing on device"
adb_ shell 'su -c "sh /data/local/tmp/chroot-device-install.sh /data/local/tmp/chroot-setup.tgz"'

# Reload the netfix watchdog so config/script changes apply without a reboot.
if adb_ shell 'su -c "test ! -f /data/adb/modules/chroot-netfix/disable"'; then
  echo "==> restarting netfix watchdog"
  adb_ shell 'su -c "setsid sh /data/adb/modules/chroot-netfix/service.sh >/dev/null 2>&1 < /dev/null &"' || true
else
  echo "==> chroot-netfix is disabled; skip watchdog restart"
fi

echo
echo "done. reboot to verify:"
echo "  adb reboot && sleep 80 && ./scripts/healthcheck.sh"
