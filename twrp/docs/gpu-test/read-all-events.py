#!/usr/bin/env python3
# read-all-events.py — 读所有 input 设备，找 power 键
import os
import struct
import threading
import time

EVENT_SIZE = 24

def read_device(dev_path, dev_name, results):
    try:
        fd = os.open(dev_path, os.O_RDONLY)
        while not results["stop"]:
            data = os.read(fd, EVENT_SIZE)
            if len(data) != EVENT_SIZE:
                continue
            tv_sec, tv_usec, ev_type, code, value = struct.unpack("<qqHHi", data)
            # 只打印 EV_KEY (0x01) 按键事件
            if ev_type == 0x01 and value == 1:  # 按下
                print(f"[+] {dev_name} ({dev_path}): type={ev_type} code={code} value={value}", flush=True)
    except Exception as e:
        print(f"[!] {dev_name} 读取失败: {e}", flush=True)

def main():
    # 找所有 input 设备
    devices = []
    for i in range(20):
        dev_path = f"/dev/input/event{i}"
        name_path = f"/sys/class/input/event{i}/device/name"
        try:
            with open(name_path, "r") as f:
                name = f.read().strip()
            devices.append((dev_path, name))
        except:
            pass

    print(f"[*] 找到 {len(devices)} 个 input 设备:", flush=True)
    for i, (path, name) in enumerate(devices):
        print(f"  {i}: {path} = {name}", flush=True)

    print("\n[*] 开始监听所有设备（按 power 键/home/back 测试，10 秒后退出）\n", flush=True)

    results = {"stop": False}
    threads = []
    for path, name in devices:
        t = threading.Thread(target=read_device, args=(path, name, results))
        t.daemon = True
        t.start()
        threads.append(t)

    # 运行 10 秒
    time.sleep(10)
    results["stop"] = True
    print("\n[*] 完成", flush=True)

if __name__ == "__main__":
    main()
