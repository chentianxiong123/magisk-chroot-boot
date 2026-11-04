#!/bin/bash
# power-key-toggle.sh — 检测 power 键（event4 gf-keys），切换背光
# 用法: power-key-toggle.sh &

BL=/sys/class/leds/lcd-backlight/brightness
EV=/dev/input/event4

# 读 event4（二进制 24 字节/事件）
# struct input_event {
#     struct timeval time;  // 16 bytes (arm64)
#     __u16 type;           // 2 bytes
#     __u16 code;           // 2 bytes
#     __s32 value;          // 4 bytes
# }
# power 键: type=0x01 (EV_KEY), code=0x74 (KEY_POWER), value=1 (按下)

dd if=$EV bs=24 count=1 2>/dev/null | od -An -tu1 | while read -r b0 b1 b2 b3 b4 b5 b6 b7 b8 b9 b10 b11 b12 b13 b14 b15 b16 b17 b18 b19 b20 b21 b22 b23; do
    # type (bytes 16-17 little-endian)
    type=$(( b16 | (b17 << 8) ))
    # code (bytes 18-19 little-endian)
    code=$(( b18 | (b19 << 8) ))
    # value (bytes 20-23 little-endian)
    value=$(( b20 | (b21 << 8) | (b22 << 16) | (b23 << 24) ))

    # EV_KEY (0x01) + KEY_POWER (0x74) + 按下 (value=1)
    if [ $type -eq 1 ] && [ $code -eq 116 ] && [ $value -eq 1 ]; then
        # 切换背光
        current=$(cat $BL 2>/dev/null)
        if [ "$current" = "0" ]; then
            echo 255 > $BL
            echo "[$(date)] Power 键按下，开屏（背光 255）"
        else
            echo 0 > $BL
            echo "[$(date)] Power 键按下，关屏（背光 0）"
        fi
    fi
done &
