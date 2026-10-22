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

restorecon -R /data/adb/modules 2>/dev/null || true
rm -rf "$W" "$T"

echo "installed modules:"
ls -1 /data/adb/modules
