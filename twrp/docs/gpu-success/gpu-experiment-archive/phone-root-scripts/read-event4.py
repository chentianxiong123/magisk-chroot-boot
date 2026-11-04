#!/usr/bin/env python3
# read-event4.py — 读 event4 原始事件（测试 power 键）
import os
import struct
import sys

EV = "/dev/input/event4"
EVENT_SIZE = 24

def main():
    fd = os.open(EV, os.O_RDONLY)
    print(f"[*] 读 {EV}（按 power 键测试，Ctrl+C 退出）", flush=True)
    count = 0
    while count < 20:
        data = os.read(fd, EVENT_SIZE)
        if len(data) != EVENT_SIZE:
            continue
        tv_sec, tv_usec, ev_type, code, value = struct.unpack("<qqHHi", data)
        # 只打印 EV_KEY (0x01) 事件
        if ev_type == 0x01:
            print(f"[+] type={ev_type} code={code} value={value}", flush=True)
            count += 1

if __name__ == "__main__":
    main()
