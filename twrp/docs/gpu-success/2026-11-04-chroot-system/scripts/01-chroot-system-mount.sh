#!/system/bin/sh
# 01-chroot-system-mount.sh — 建立 chroot /system 的挂载环境
# 用法：在 TWRP 里跑（/data /system 已挂载）
set -x

# 1. 清残留
umount -l /data/chroot-system/system 2>/dev/null
umount -l /data/chroot-system/dev    2>/dev/null
umount -l /data/chroot-system/proc   2>/dev/null
umount -l /data/chroot-system/sys    2>/dev/null
umount -l /data/chroot-system/data   2>/dev/null
rm -rf /data/chroot-system

# 2. 建 chroot 根 + bind
mkdir -p /data/chroot-system/{dev,proc,sys,tmp,system,vendor,data}
mount --bind /system /data/chroot-system/system
mount --bind /dev    /data/chroot-system/dev
mount --bind /proc   /data/chroot-system/proc
mount --bind /sys    /data/chroot-system/sys
mount --bind /data   /data/chroot-system/data

# 3. 补 SELinux 服务上下文
cp /service_contexts /data/chroot-system/service_contexts 2>/dev/null

# 4. 修 /vendor symlink（chroot 内）
chroot /data/chroot-system /system/bin/sh -c 'rmdir /vendor 2>/dev/null; ln -sf /system/vendor /vendor'

# 5. 修 hwcomposer 模块 symlink
chroot /data/chroot-system /system/bin/sh -c '
  cd /system/vendor/lib64/hw 2>/dev/null
  ln -sf hwcomposer.mt6755.so hwcomposer.default.so 2>/dev/null
  ln -sf gralloc.mt6755.so     gralloc.default.so    2>/dev/null
'

# 6. ★ 挂载 selinuxfs（最关键）
mount -t selinuxfs selinuxfs /sys/fs/selinux 2>/dev/null

echo "== chroot-system 环境就绪 =="
chroot /data/chroot-system /system/bin/sh -c 'ls /system/bin/surfaceflinger /system/bin/servicemanager' 2>&1
