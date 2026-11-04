#!/system/bin/sh
# 03-show-image.sh — 把任意图片显示到屏幕（经 bootanimation → SF → hwcomposer）
# 用法：03-show-image.sh /path/to/image.png
#   图片会被预先旋转180°抵消屏幕倒转；若已在别处处理好可传 0
set -x
IMG=${1:-/data/local/tmp/frame0.png}
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64

# 生成无限循环 bootanimation
WORK=/data/local/tmp/my_bootanim
rm -rf $WORK && mkdir -p $WORK/part0
cp "$IMG" $WORK/part0/000.png
printf '1080 1920 10\np 0 0 part0\n' > $WORK/desc.txt
cd $WORK && zip -q -0 -r /data/local/tmp/my_bootanimation.zip .

# 替换 + 重启 bootanimation
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  killall bootanimation 2>/dev/null; sleep 1
  cp /data/local/tmp/my_bootanimation.zip /system/media/bootanimation.zip
  setsid /system/bin/bootanimation >/data/local/tmp/ba.log 2>&1 &
'

# 验证 layer 合成
sleep 3
chroot /data/chroot-system /system/bin/sh -c 'dumpsys SurfaceFlinger 2>&1 | grep -E "Visible layers|numLayers|flips" | head -4'
