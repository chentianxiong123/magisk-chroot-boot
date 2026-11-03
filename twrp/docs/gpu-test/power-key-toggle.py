#!/usr/bin/env python3
# power-key-toggle.py — 检测 power 键（event4 gf-keys），切换背光
# 用法: python3 power-key-toggle.py &

import os
import struct
import sys

BL = "/sys/class/leds/lcd-backlight/brightness"
EV = "/dev/input/event3"

# arm64 input_event: timeval(16) + type(2) + code(2) + value(4) = 24 bytes
EVENT_SIZE = 24

def read_event(fd):
    data = os.read(fd, EVENT_SIZE)
    if len(data) != EVENT_SIZE:
        return None
    # struct: tv_sec(8) tv_usec(8) type(2) code(2) value(4) = 24
    tv_sec, tv_usec, ev_type, code, value = struct.unpack("<qqHHi", data)
    return ev_type, code, value

def toggle_backlight():
    try:
        with open(BL, "r") as f:
            current = int(f.read().strip())
    except:
        current = 255

    new = 0 if current > 0 else 255
    try:
        with open(BL, "w") as f:
            f.write(str(new))
        print(f"[+] backlight: {current} -> {new}", flush=True)
    except Exception as e:
        print(f"[!] 写背光失败: {e}", flush=True)

def main():
    fd = os.open(EV, os.O_RDONLY)
    print(f"[*] 监听 {EV} (power 键)", flush=True)
    while True:
        ev = read_event(fd)
        if not ev:
            continue
        ev_type, code, value = ev
        # EV_KEY=0x01, KEY_POWER=116 (0x74), value=1 (按下)
        if ev_type == 0x01 and code == 116 and value == 1:
            print(f"[+] Power 键按下 (type={ev_type} code={code} value={value})", flush=True)
            toggle_backlight()

if __name__ == "__main__":
    main()
