#!/usr/bin/env python3
# listen-keys.py — 监听所有 input 设备的按键事件（后台持续）
import os
import struct
import threading
import sys

EVENT_SIZE = 24

def read_device(dev_path, dev_name):
    try:
        fd = os.open(dev_path, os.O_RDONLY)
        while True:
            data = os.read(fd, EVENT_SIZE)
            if len(data) != EVENT_SIZE:
                continue
            tv_sec, tv_usec, ev_type, code, value = struct.unpack("<qqHHi", data)
            # 只打印 EV_KEY (0x01) 按键事件
            if ev_type == 0x01:
                print(f"[+] {dev_name}: type={ev_type} code={code} value={value}", flush=True)
    except Exception as e:
        pass

def main():
    devices = []
    for i in range(20):
        dev_path = f"/dev/input/event{i}"
        name_path = f"/sys/class/input/event{i}/device/name"
        try:
            with open(name_path, "r") as f:
                name = f.read().strip()
            # 只监听按键相关设备（跳过传感器）
            if any(k in name for k in ["kpd", "keys", "keypad", "gpio-keys"]):
                devices.append((dev_path, name))
        except:
            pass

    print(f"[*] 监听 {len(devices)} 个按键设备", flush=True)
    for path, name in devices:
        print(f"  {path} = {name}", flush=True)

    threads = []
    for path, name in devices:
        t = threading.Thread(target=read_device, args=(path, name))
        t.daemon = True
        t.start()
        threads.append(t)

    # 持续监听
    while True:
        time.sleep(1)

import time

if __name__ == "__main__":
    main()
