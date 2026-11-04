#!/usr/bin/env python3
# fb0-flip-daemon.py — 读 fb0 → 翻转 180 度 → 写回 fb0（每 100ms）
# 用法: python3 fb0-flip-daemon.py &

import os
import numpy as np
from PIL import Image
import time
import sys

FB0 = "/dev/graphics/fb0"
W, H = 1080, 1920
STRIDE = 4352  # 1088*4 (double buffer, 1080 可视 + 8 padding)
PIXEL = 4      # 32bpp BGRA

def flip_fb0():
    # 读 fb0（可视区 1080x1920）
    with open(FB0, "rb") as f:
        data = f.read(STRIDE * H)

    # 转 numpy（H x STRIDE）
    arr = np.frombuffer(data, dtype=np.uint8)
    arr = arr.reshape(H, STRIDE)[:, :W * PIXEL].reshape(H, W, PIXEL)

    # 翻转 180 度（上下 + 左右翻转）
    arr_flipped = np.flipud(np.fliplr(arr))

    # 写回 fb0（重新加 padding）
    output = np.zeros((H, STRIDE), dtype=np.uint8)
    output[:, :W * PIXEL] = arr_flipped.reshape(H, W * PIXEL)

    with open(FB0, "wb") as f:
        f.write(output.tobytes())

def main():
    print(f"[*] fb0 翻转守护启动（每 100ms 翻转 {W}x{H}）", flush=True)
    while True:
        try:
            flip_fb0()
        except Exception as e:
            print(f"[!] 翻转失败: {e}", flush=True)
        time.sleep(0.1)

if __name__ == "__main__":
    main()
