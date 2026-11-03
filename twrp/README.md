# 魅蓝 Note5 hybrid recovery 服务器方案

TWRP + chroot Debian 服务器实战系列（5 篇）。

## 系列文档（twrp/docs/）

| 篇 | 文档 | 内容 |
|---|---|---|
| 篇1 | 背景与路线选择 | 项目背景 + 4 条路线对比 + hybrid 胜出原因 |
| 篇2 | TWRP 镜像标准化修补技术 | repack_twrp_hook.py 标准化修补（5 个踩坑） |
| 篇3 | MTK 驱动与省电调度逆向 | MTK 驱动逆向 + 省电调度 + 8 核压测 |
| 篇4 | hybrid 全自动钩子实战 | 12 个踩坑 + 屏幕/U 盘/fuelgauged/TWRP UI 实验 |
| 篇5 | zram 启动链踩坑 | zram 64 位溢出 bug + 解法 |

## 核心成果

- **hybrid recovery**（recovery-normal 内核 + TWRP ramdisk）稳定运行
- **8 核全功能**（CPU 性能完整，HPS 自动开核）
- **chroot Debian 服务器**（cpa/freellmapi/batt-display/adbd-tcp/sshd）
- **WiFi + 无线 adb + SSH**（远程管理）
- **背光控制 + U 盘挂载**（外设支持）
- **5 个踩坑解法**（cpio 特殊文件名/64 位溢出/权限/wifi 配置/reboot 路径）

## 文件结构

```
twrp/
├── README.md                    # 本文件（系列索引）
├── build.sh                     # 构建脚本
├── repack_twrp_hook.py          # TWRP 镜像修补
├── twrp-hook.sh                 # hybrid 钩子模板
├── config/
│   └── twrp-wifi.conf           # WiFi 配置（gitignore，本地保留）
├── modules/                     # hybrid 钩子模块
│   ├── chroot-mount/
│   ├── chroot-network/
│   ├── chroot-services/
│   └── wifi-up/
└── docs/                        # 系列文档（5 篇）
    ├── 篇1-背景与路线选择.md
    ├── 篇2-TWRP镜像标准化修补技术.md
    ├── 篇3-MTK驱动与省电调度逆向.md
    ├── 篇4-hybrid全自动钩子实战.md
    └── 篇5-zram启动链踩坑.md
```

## 快速开始

```bash
# 1. 构建镜像
./build.sh

# 2. 修补 TWRP（插钩子）
python3 repack_twrp_hook.py recovery-normal.img hybrid-hooked.img

# 3. 刷机
fastboot boot hybrid-hooked.img

# 4. SSH 连接（120 秒后）
ssh -p 2222 root@<device-ip>
```

## License

MIT
