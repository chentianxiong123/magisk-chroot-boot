#!/usr/bin/env bash
set -euo pipefail

SVC=${1:-supervisord}
LINES=${2:-100}
ADB=${ADB:-adb}
SERIAL=${SERIAL:-}
adb_() { if [ -n "$SERIAL" ]; then "$ADB" -s "$SERIAL" "$@"; else "$ADB" "$@"; fi; }

LOG="/data/debian/var/log/supervisor/${SVC}.log"
adb_ shell "su -c 'tail -n ${LINES} ${LOG}'"
