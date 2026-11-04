# magisk-chroot-boot 项目交接文档（TWRP 版 chroot）

> 记录日期：2026-11-01
> 目的：手机 chroot 服务器断网期间，委托他人继续。本文档完整记录项目状态、设计、验证、下一步。

---

## 1. 项目是什么

**一个 Magisk 模块项目，让 Android 设备上的 Debian chroot 在 TWRP（recovery）里跑起来，不依赖安卓框架。**

- **system 版本**（已工作）：安卓开机后，Magisk 模块挂载 chroot、配网络、拉 supervisord，Debian 服务（sshd、web 等）在 `/data/debian` 里跑。
- **twrp 版本**（本项目重点，已写好未实测）：同一个 Debian chroot，但在 **TWRP/recovery** 里跑 —— 不依赖安卓框架（不用 `sys.boot_completed`、不用 `net.dns1`、不靠安卓管理 WiFi）。

**核心思路（通解）**：`ramdisk 的 init 改动 + 挂载 + chroot`，**不碰内核源码、不碰 .ko、不需要安卓框架**。内核驱动全 built-in 在原厂内核里，recovery 引导即生效。

---

## 2. 目录结构（Z:\my-project\magisk-chroot-boot\）

```
magisk-chroot-boot/
├── .git  .gitignore
├── system/                 ★ SYSTEM 版本（安卓开机，已工作）
│   ├── modules/            7 模块：chroot-mount / chroot-network / chroot-services /
│   │                       chroot-netfix / static-ip / adb-tcp / postboot-tune
│   ├── scripts/  meta/  config/  docs/  supervisor/  templates/
│   ├── releases/           7 个模块 zip（system 产物）
│   ├── dist/  backups/  legacy/
│   └── build.sh  install.sh  uninstall.sh  README.md  LICENSE
└── twrp/                   ★ TWRP 版本（recovery，本项目重点）
    ├── modules/
    │   ├── wifi-up/          ★ 新增：WiFi 拉起（无安卓框架）
    │   ├── chroot-mount/     ★ 改触发（无 boot_completed）
    │   ├── chroot-network/   ★ DNS 从配置读（无 net.dns1）
    │   └── chroot-services/  ★ 改触发，保留 su - root
    ├── config/twrp-wifi.conf   ← WiFi SSID/PSK/静态IP/DNS 配置
    ├── releases/twrp-magisk.img ← ★ 带面具的 TWRP（18,908,736 B ≈ 18.2MB）
    ├── dist/  meta/  build.sh  LICENSE
```

- `build.sh` 打包 `modules/*` + `meta/META-INF` → `releases/<id>-<ver>.zip`（每个模块一个 zip，Magisk 可刷）。
- `releases/`、`dist/`、`config/`、`backups/` 均 gitignore（产物不入库，发 GitHub Releases）。

---

## 3. 设备信息

| 项 | 值 |
|---|---|
| 型号 | M5 Note（mt6755，**不是** MT6750） |
| Android | 7.0，arm64-v8a |
| GPU | Mali（Midgard），`/dev/mali0`，OpenGL ES 3.0 |
| 显示 | **`/dev/fb0`（framebuffer），无 `/dev/dri`（无 DRM/KMS）** |
| 音频 | `/dev/snd/controlC0` + 多路 pcm（ALSA 有） |
| WiFi | `mt-wifi` 驱动（built-in）→ `wlan0`、`p2p0` |
| modem | `ccmni0–17`、`cc3mni0–7`（蜂窝数据） |
| 内核模块 | **无 .ko，全 built-in**（`/proc/modules` 空、全盘无 .ko） |
| Magisk | v23.0（23000），工具在 `/data/adb/magisk/` |
| chroot | `/data/debian`（活），原备份 `/data/debian.tgz`（**2026-11-01 已删**） |

**adb 连接**：
- USB：`621QECQ83QBVM`（当前可用，就是这台设备，有 `/data/debian`）
- 无线：`192.168.31.213:5555`（**2026-11-01 已 offline**，需 `adb connect` 重连或重启 adbd）
- 另有一台 `Punguin_Uz901`（recovery 状态，无 getprop，非本项目目标）

---

## 4. 已完成的工作

1. **解包分析** recovery.img（MTKclient images + 设备分区）—— Android bootimg，kernel+ramdisk。
2. **面具修补 TWRP**（正确目标 = MTKclient 的 18MB `images/recovery.img`，**不是**设备 32MB 原厂 recovery 分区）：
   - 用设备 `/data/adb/magisk/` 的 `boot_patch.sh` + `magiskboot`（v23.0，从 Magisk APK `com.topjohnwu.magisk-1/base.apk` 提取，md5 已验证一致）；
   - 产出 **`twrp/releases/twrp-magisk.img`**（19,087,360 B），ramdisk 已注入 magiskinit（`skip_initramfs`→`want_initramfs` 已打）。
3. **TWRP 版 4 个模块写好**（wifi-up / chroot-mount / chroot-network / chroot-services）+ `config/twrp-wifi.conf`。
4. **GPU 路径②确认可行**（安卓内核 + vendor Mali blob + libhybris）：
   - `/dev/mali0`（mali_kbase 内核驱动）✅
   - `/vendor/lib/egl/libGLES_mali.so`（24.5MB 用户态 blob）✅
   - `/vendor/lib/hw/gralloc.mt6755.so`、`hwcomposer.mt6755.so` ✅
5. **项目重构**：`magisk-chroot-boot` 一个目录，`system/` + `twrp/` 两个版本；产物挪 `releases/`（gitignore）。
6. **设备清理**：删 `/data/debian.tgz`（245MB）+ dalvik 缓存/崩溃日志等，释放约 600MB（/data 4.0G→3.4G）。活 chroot 未动。

---

## 5. TWRP 版设计（无安卓框架通解）

### 触发：自同步，等真实条件（非模块执行顺序）
| 模块 | 触发条件 | 逻辑 |
|---|---|---|
| `wifi-up` | `/data/adb/twrp-wifi.conf` 存在 | 挂 `/system` → `/vendor`→`/system/vendor` → 等 wlan0（内核按需加载固件）→ `/system/bin/wpa_supplicant`（带 /system 库）→ 静态IP/DHCP → 写 `/data/adb/chroot-net.conf`（DNS+GW） |
| `chroot-mount` | `/data/debian` 存在 | 挂 proc/sys/dev/devpts + tmpfs（内核操作，TWRP 可用） |
| `chroot-network` | `/data/adb/chroot-net.conf` 存在 | 从该文件读 DNS（**无 getprop net.dns1**）→ 写 resolv.conf；`ip rule` 用 `/system/bin/ip` |
| `chroot-services` | `chroot-net.conf` + `/data/debian/proc/version` | **保留 `su - root`**（内核 PARANOID_NETWORK 组限制，非安卓框架）→ supervisord |

- 所有等待**有超时（300s）有日志**，失败不静默卡死；日志 `/data/local/tmp/{wifi-up,chroot-mount,chroot-network,services}.log`。
- 配置改 `twrp/config/twrp-wifi.conf`（部署到 `/data/adb/twrp-wifi.conf`），不改脚本。

### 安卓框架依赖 → TWRP 替代
| 安卓框架依赖 | TWRP 替代 |
|---|---|
| `sys.boot_completed=1` | 等真实条件（配置文件/目录存在） |
| `net.dns1/2`（getprop） | wifi-up 写 `/data/adb/chroot-net.conf`，chroot-network 读 |
| 安卓管 WiFi | wifi-up 自己：mount /system + /vendor 链接 + wpa_supplicant |
| `ip` 工具 | 优先 `/system/bin/ip`，回退 `ip` |
| `su - root` | **保留**（内核组限制，不是框架） |

---

## 6. 关键验证结果

- **内核驱动全 built-in**：`mt-wifi`（wlan0/p2p0）、`mali_kbase`（/dev/mali0）、显示（fb0）、存储、USB、ALSA、modem —— **无 .ko、无源码需求**。这就是通解的基础。
- **显示是 fbdev 无 DRM** → 桌面只能 **软件渲染（fbdev/Xorg）**，轻量可用，无 GPU 硬件加速（除非走 GPU 路径②）。
- **GPU 路径②**（安卓内核 + blob + libhybris）成立：mali0 + libGLES_mali.so + gralloc/hwcomposer 全在，只差 libhybris 桥。
- **TWRP 卡 = 软件渲染（fbdev）**，印证无 GPU 加速。

---

## 7. 下一步待办（按顺序）

1. **实测 wlan0**：刷 `twrp-magisk.img` 进 TWRP → 手动 `mount /system; ln -s /system/vendor /vendor; ifconfig wlan0 up` 确认固件加载、wlan0 出现。
2. **实测 Magisk 在 recovery 执行 service.sh 的时机**（`sys.boot_completed` 不可用，看触发）。
3. **实测 `/system/bin/wpa_supplicant` 在 TWRP 用户态能不能跑**（需 `/system/lib`）。
4. 通过后 `twrp/build.sh` 打包 4 模块 → `twrp/releases/*.zip`，连同 `twrp-wifi.conf`、`twrp-magisk.img` 部署。
5. **GPU 路径②**：chroot 里装 `libhybris`，桥 `libGLES_mali.so`/`gralloc.mt6755.so`/`hwcomposer.mt6755.so`，桌面走 EGL→/dev/mali0。
6. **双启动**（用户要求：双启动 + 不先进安卓）：
   - ① TWRP 持久 misc 标志（默认 recovery，安卓按需）—— 现成，加"重写 misc 标志"；
   - ② boot.img init 选择器（`/data/adb/bootmode` → chroot 直接 switch_root / android 正常）—— 更彻底，要写 init。
7. **Linux 桌面**：fbdev 路线（Xorg-fbdev + libinput + ALSA + 轻量 DE）软件渲染；要 GPU 走②。

---

## 8. 交接要点（命令 / 路径）

```bash
# 面具修补 recovery（已做过，流程记录）
adb push vendor/magisk/* /data/local/tmp/mp/   # 或从 /data/adb/magisk 拷
sh /data/local/tmp/mp/boot_patch.sh /data/local/tmp/twrp.img   # 产出 new-boot.img
adb pull /data/local/tmp/mp/new-boot.img twrp/releases/twrp-magisk.img

# 打包模块
cd twrp && ./build.sh    # → twrp/releases/<id>-<ver>.zip

# 部署 WiFi 配置
adb push twrp/config/twrp-wifi.conf /data/adb/twrp-wifi.conf

# adb 连接（无线掉线时）
adb connect 192.168.31.213:5555
adb -s 621QECQ83QBVM shell   # USB 直连（当前可用）

# 关键路径
# 项目: Z:\my-project\magisk-chroot-boot\
# 带面具 TWRP: twrp/releases/twrp-magisk.img (18.2MB)
# WiFi 配置: twrp/config/twrp-wifi.conf
# 设备 chroot: /data/debian   Magisk: /data/adb/magisk/
# GPU blob: /vendor/lib/egl/libGLES_mali.so (24.5MB)
```

---

## 9. 风险 & 注意

- **别刷错镜像**：`twrp-magisk.img` 是从 **18MB MTKclient `images/recovery.img`**（真 TWRP）patch 的；设备 32MB recovery 分区是**原厂恢复镜像**，不是 TWRP。刷前确认来源。
- **wpa_supplicant 在 TWRP 跑不跑是最大未知点**（需实测，见待办 1–3）。
- **Mali blob 是 bionic 编译**，必须 libhybris 桥，不能直接 dlopen。
- **设备无线 adb 易掉**（adbd 不稳定），优先 USB。
- **`su - root` 别去掉**：内核 `PARANOID_NETWORK` 需要 inet(3003)/net_raw(3004) 组，TWRP 同样生效。
- **`/data/debian.tgz` 已删**（用户决定），chroot 还原点没了 —— 若 chroot 出事，别再指望它，靠 `/data/debian` 本身。
- **产物不进 git**（releases/dist/config/backups gitignore），发 GitHub Releases。

---

## 10. 一句话总结

**通解 = 原厂内核（驱动 built-in）+ 改 ramdisk 的 init（magiskinit/TWRP）+ 挂载（/system、/vendor）+ chroot（/data/debian），不碰内核源码、不需要安卓框架。** TWRP 版 4 模块已写好，带面具 TWRP 已产出，GPU 路径②确认可行；剩下的是刷机实测（wlan0 → wpa_supplicant → Magisk recovery 触发）→ libhybris → 双启动。
