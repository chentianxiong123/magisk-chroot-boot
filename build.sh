#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
OUT="$ROOT/dist"
mkdir -p "$OUT"

command -v zip >/dev/null || { echo "!! zip not installed"; exit 1; }

for m in "$ROOT"/modules/*/; do
  id=$(basename "$m")
  ver=$(sed -n 's/^version=//p' "$m/module.prop")
  work=$(mktemp -d)
  cp -a "$m." "$work/"
  cp -a "$ROOT/meta/META-INF" "$work/"
  chmod 755 "$work/service.sh"
  (cd "$work" && zip -qr "$OUT/${id}-${ver}.zip" .)
  rm -rf "$work"
  echo "built dist/${id}-${ver}.zip"
done
