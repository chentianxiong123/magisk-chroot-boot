#!/system/bin/sh
# 02-chroot-system-start.sh — 启动 servicemanager + surfaceflinger
# 前提：01-chroot-system-mount.sh 已跑
set -x

export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
export ANDROID_ROOT=/system
export ANDROID_DATA=/data

# 1. 杀旧
chroot /data/chroot-system /system/bin/sh -c 'killall servicemanager surfaceflinger 2>/dev/null'
sleep 2

# 2. 起 servicemanager
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  setsid /system/bin/servicemanager >/data/local/tmp/sm.log 2>&1 &
'
sleep 3
echo "-- servicemanager 日志 --"
cat /data/local/tmp/sm.log 2>/dev/null

# 3. 起 surfaceflinger
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  export ANDROID_ROOT=/system
  export ANDROID_DATA=/data
  setsid /system/bin/surfaceflinger >/data/local/tmp/sf.log 2>&1 &
'
sleep 6
echo "-- SF 状态 --"
chroot /data/chroot-system /system/bin/sh -c '
  for p in $(pgrep -f surfaceflinger); do echo pid=$p 线程=$(ls /proc/$p/task 2>/dev/null|wc -l); done
'

# 4. 验证服务注册
echo "-- service list --"
chroot /data/chroot-system /system/bin/sh -c 'service list 2>&1 | head -8'
