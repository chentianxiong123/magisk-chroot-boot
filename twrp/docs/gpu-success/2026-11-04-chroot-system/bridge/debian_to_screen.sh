#!/bin/bash
# debian_to_screen.sh — Debian 应用把任意图片/画面显示到物理屏幕
# 桥：Debian → 帧PNG → bootanimation → SF → hwcomposer → 屏幕（已验证链路）
#
# 用法：
#   debian_to_screen.sh <image.png> [pre_rotate180=1]
#   - image.png: 要显示的图片（1080x1920）
#   - pre_rotate180: 默认 1（图片预旋转180°抵消屏幕倒转）；传 0 则不旋转
#
# 依赖：chroot /system 的 SF + servicemanager 已跑（01+02 脚本）
IMG="${1:-/root/frame.png}"
ROT="${2:-1}"
CHROOT=/data/chroot-system
OUT=/data/local/tmp/bridge_frame.png

# 1. 预处理（预旋转180抵消屏幕倒转）
if [ "$ROT" = "1" ]; then
  # 用 Python PIL 旋转（Debian 有）
  python3 -c "
from PIL import Image
img=Image.open('$IMG').convert('RGB').resize((1080,1920))
img.rotate(180).save('$OUT')
print('rotated:', '$OUT')
" 2>&1
else
  python3 -c "
from PIL import Image
Image.open('$IMG').convert('RGB').resize((1080,1920)).save('$OUT')
print('no-rotate:', '$OUT')
" 2>&1
fi

# 2. 生成无限循环 bootanimation zip
WORK=/data/local/tmp/ba_bridge
rm -rf "$WORK" && mkdir -p "$WORK/part0"
cp "$OUT" "$WORK/part0/000.png"
printf '1080 1920 10\np 0 0 part0\n' > "$WORK/desc.txt"
cd "$WORK" && zip -q -0 -r /data/local/tmp/ba_bridge.zip .

# 3. 经 chroot /system 的 bootanimation 显示（SF 已跑）
$CHROOT/system/bin/chroot $CHROOT /system/bin/sh -c "
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  killall bootanimation 2>/dev/null; sleep 1
  cp /data/local/tmp/ba_bridge.zip /system/media/bootanimation.zip
  setsid /system/bin/bootanimation >/data/local/tmp/ba.log 2>&1 &
"
sleep 3
echo "--- 显示状态 ---"
$CHROOT/system/bin/chroot $CHROOT /system/bin/sh -c "dumpsys SurfaceFlinger 2>&1 | grep -E 'Visible layers|numLayers|flips' | head -4"
