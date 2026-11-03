#!/system/bin/sh
# chroot-mount (TWRP): same mounts as the system version, but no
# sys.boot_completed wait (never set in recovery). Trigger = /data/debian
# exists (TWRP mounts /data). Mounts are kernel-only, work in TWRP as-is.
MODDIR=${0%/*}
log() { echo "[chroot-mount] $1" | tee -a /data/local/tmp/chroot-mount.log; }

# Wait for the chroot root (TWRP mounts /data). Bounded so failure is visible.
i=0
while [ ! -d /data/debian ] && [ $i -lt 300 ]; do i=$((i+1)); sleep 1; done
[ -d /data/debian ] || { log "! /data/debian missing"; exit 1; }

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
log "Chroot mounts done"
