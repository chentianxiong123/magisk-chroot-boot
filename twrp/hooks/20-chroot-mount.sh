#!/system/bin/sh
# 20-chroot-mount.sh — 挂载 Debian chroot 文件系统 (改造自 Magisk chroot-mount 模块)
# 改造: 去掉 getprop sys.boot_completed (hybrid recovery 没框架), 直接挂

B=/data/debian/bin/busybox

# 等 /data/debian 可用 (持久化分区, 重启后就在)
i=0
while [ ! -d /data/debian/bin ] && [ $i -lt 30 ]; do sleep 1; i=$((i+1)); done
[ -d /data/debian/bin ] || exit 0

# 挂 chroot 文件系统 (busybox, 绕 nodev)
$B mount -t proc proc /data/debian/proc 2>/dev/null
$B mount -t sysfs sys /data/debian/sys 2>/dev/null
$B mount -o bind /dev /data/debian/dev 2>/dev/null
$B mount -t devpts devpts /data/debian/dev/pts 2>/dev/null

# ptmx (tty 需要)
rm -f /data/debian/dev/ptmx 2>/dev/null
$B mknod /data/debian/dev/ptmx c 5 2 2>/dev/null
chown root:root /data/debian/dev/ptmx 2>/dev/null
chmod 666 /data/debian/dev/ptmx 2>/dev/null
$B mount -o remount,gid=5,mode=620 /data/debian/dev/pts 2>/dev/null

# tmpfs (可写目录)
mkdir -p /data/debian/tmp /data/debian/var/log /data/debian/var/lib/apt/lists /data/debian/var/cache/apt /data/debian/run/sshd
$B mount -t tmpfs tmpfs /data/debian/tmp 2>/dev/null
$B mount -t tmpfs -o size=64M tmpfs /data/debian/var/log 2>/dev/null
mkdir -p /data/debian/var/log/supervisor
$B mount -t tmpfs -o size=128M tmpfs /data/debian/var/lib/apt/lists 2>/dev/null
$B mount -t tmpfs -o size=128M tmpfs /data/debian/var/cache/apt 2>/dev/null
chmod 755 /data/debian/run/sshd 2>/dev/null

exit 0
