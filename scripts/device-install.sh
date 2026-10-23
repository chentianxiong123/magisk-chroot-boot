#!/system/bin/sh
set -e

T="$1"
W=/data/local/tmp/chroot-setup
rm -rf "$W"
mkdir -p "$W"
/data/adb/magisk/busybox tar xzf "$T" -C "$W"

mkdir -p /data/adb/modules
for d in "$W"/modules/*/; do
  id=$(basename "$d")
  mkdir -p "/data/adb/modules/$id"
  cp -af "$d." "/data/adb/modules/$id/"
  chmod 755 "/data/adb/modules/$id/service.sh"
  chown -R root:root "/data/adb/modules/$id"
done

if [ -f "$W/supervisor/chroot.conf" ]; then
  mkdir -p /data/debian/etc/supervisor/conf.d
  cp -f "$W/supervisor/chroot.conf" /data/debian/etc/supervisor/conf.d/chroot.conf
  chown root:root /data/debian/etc/supervisor/conf.d/chroot.conf
fi

# Android 内核 CONFIG_ANDROID_PARANOID_NETWORK 只放行 GID 3003(inet) /
# 3004(net_raw) 的进程；只靠 root 不够，ping 等丢 cap 的 raw socket 会被拒。
G=/data/debian/etc/group
if [ -f "$G" ]; then
  cp -f "$G" "$G.bak.$(date +%s)" 2>/dev/null || true
  /data/adb/magisk/busybox awk -F: -v OFS=: '
    $1 == "inet" {
      if ($4 !~ /(^|,)root(,|$)/) $4 = ($4 == "" ? "root" : $4 ",root")
      ok_inet = 1
    }
    $1 == "net_raw" {
      if ($4 !~ /(^|,)root(,|$)/) $4 = ($4 == "" ? "root" : $4 ",root")
      ok_raw = 1
    }
    { print }
    END {
      if (!ok_inet) print "inet:x:3003:root"
      if (!ok_raw) print "net_raw:x:3004:root"
    }
  ' "$G" > "$G.tmp" && mv -f "$G.tmp" "$G"
  chown root:root "$G"
  chmod 644 "$G"
  echo "android network groups:"
  grep -E '^(inet|net_raw):' "$G"
fi

restorecon -R /data/adb/modules 2>/dev/null || true
rm -rf "$W" "$T"

echo "installed modules:"
ls -1 /data/adb/modules
