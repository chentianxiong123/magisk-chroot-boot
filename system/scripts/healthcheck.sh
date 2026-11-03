#!/usr/bin/env bash
set -uo pipefail

ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
PORT_NAMES=${PORT_NAMES:-}
PORTS=${PORTS:-}

adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

echo "==> supervisor"
STATE=$(adb_ shell 'su -c "/data/debian/bin/busybox chroot /data/debian /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf status"' 2>&1 || true)
echo "$STATE"

fail=0

if [ -n "$PORTS" ]; then
  echo
  echo "==> listening ports"
  NET=$(adb_ shell 'su -c "netstat -ln 2>/dev/null"' 2>&1 || true)
  set -- $PORT_NAMES
  for p in $PORTS; do
    name=${1:-}
    [ $# -gt 0 ] && shift
    if printf '%s\n' "$NET" | grep -q ":$p "; then
      printf '  [ OK ] %-6s %s\n' "$p" "$name"
    else
      printf '  [FAIL] %-6s %s\n' "$p" "$name"
      fail=1
    fi
  done
fi

echo
if printf '%s\n' "$STATE" | grep -qE 'FATAL|BACKOFF|STOPPED'; then
  echo "!! supervisor reports unhealthy programs"
  fail=1
fi

[ "$fail" -eq 0 ] && echo "healthcheck: OK" || echo "healthcheck: FAILED"
exit "$fail"
