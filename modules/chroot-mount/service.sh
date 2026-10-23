#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done
while [ ! -d /data/debian ]; do sleep 1; done
log() { echo "[chroot-mount] $1" | tee -a /data/local/tmp/chroot-mount.log; }
log "Mounting chroot filesystems..."
/data/debian/bin/busybox mount -t proc proc /data/debian/proc 2>/dev/null
/data/debian/bin/busybox mount -t sysfs sys /data/debian/sys 2>/dev/null
/data/debian/bin/busybox mount -o bind /dev /data/debian/dev 2>/dev/null
/data/debian/bin/busybox mount -t devpts devpts /data/debian/dev/pts 2>/dev/null
rm -f /data/debian/dev/ptmx
/data/debian/bin/busybox mknod /data/debian/dev/ptmx c 5 2 2>/dev/null
chown root:root /data/debian/dev/ptmx 2>/dev/null
chmod 666 /data/debian/dev/ptmx 2>/dev/null
/data/debian/bin/busybox mount -o remount,gid=5,mode=620 /data/debian/dev/pts 2>/dev/null
mkdir -p /data/debian/tmp /data/debian/var/log /data/debian/var/lib/apt/lists /data/debian/var/cache/apt /data/debian/run/sshd
/data/debian/bin/busybox mount -t tmpfs tmpfs /data/debian/tmp 2>/dev/null
/data/debian/bin/busybox mount -t tmpfs -o size=64M tmpfs /data/debian/var/log 2>/dev/null
mkdir -p /data/debian/var/log/supervisor
/data/debian/bin/busybox mount -t tmpfs -o size=128M tmpfs /data/debian/var/lib/apt/lists 2>/dev/null
/data/debian/bin/busybox mount -t tmpfs -o size=128M tmpfs /data/debian/var/cache/apt 2>/dev/null
chmod 755 /data/debian/run/sshd 2>/dev/null
# DNS: prefer the active network's DNS. WiFi/DHCP can finish after
# sys.boot_completed, so wait briefly, then fall back to the default
# gateway, and finally to a public DNS.
i=0
while [ -z "$(getprop net.dns1)" ] && [ "$i" -lt 30 ]; do
  i=$((i+1))
  sleep 1
done
DNS1=$(getprop net.dns1)
DNS2=$(getprop net.dns2)
GW=$(ip route show table 0 2>/dev/null | grep '^default' | head -1 | cut -d' ' -f3)
: > /data/debian/etc/resolv.conf 2>/dev/null
if [ -n "$DNS1" ]; then echo "nameserver $DNS1" >> /data/debian/etc/resolv.conf; fi
if [ -n "$DNS2" ]; then echo "nameserver $DNS2" >> /data/debian/etc/resolv.conf; fi
if ! grep -q '^nameserver' /data/debian/etc/resolv.conf 2>/dev/null; then
  if [ -n "$GW" ]; then
    echo "nameserver $GW" >> /data/debian/etc/resolv.conf
  else
    echo "nameserver 223.5.5.5" >> /data/debian/etc/resolv.conf
  fi
fi
log "Chroot mounts done"
