# Meizu M5 Note — "摆脱安卓" 无头服务器：全盘讨论记录与成果

> 记录时间：2026-11-03 → 11-04 夜
> 设备：Meizu M5 Note（M5_Note）· Android 7.0 (API 24) · kernel 3.18.35 · MT6755 (Helio P10) · Magisk root · 3GB RAM
> 目标：**摆脱安卓框架**——boot 进 TWRP recovery，跑 Debian chroot + 用户态 supervisor（**不要 systemd**），低功耗无头服务器；电池监控必须；显示/GPU 为可选项（有最好没也可以）。

---

## 1. 已跑通（成果清单）

| 项目 | 状态 | 说明 |
|---|---|---|
| **TWRP recovery** | ✅ | omni_M5Note，root shell（`u:r:su:s0`） |
| **服务栈** | ✅ | 在 TWRP 内：`supervisord` + `sshd`(22) + `wpa_supplicant` + `fuelgauged` + `cli-proxy-api` + `MainThread`，合计 ~200MB / 3GB |
| **电池监控** | ✅ | 无框架，走内核 sysfs：`/sys/class/power_supply/battery`（capacity/voltage/current），`fuelgauged` 在跑 |
| **Debian chroot** | ✅ | `/data/debian`（glibc），supervisord 管理服务 |
| **zram** | ✅ | **新增**：~1.36GiB swap，priority 100（内建 major 254），等效 ~4GB+ |
| **TWRP 钩子系统** | ✅ | `/data/local/tmp/twrp-hooks/`：`01-fuelgauged` `02-wifi` `03-zram` `20-chroot-mount` `30-chroot-services` `40-adbd-tcp` `50-static-ip`——TWRP 启动自动按序执行，全链路通 |
| **内核** | ✅ | **零模块**，所有驱动内建；无 DRM/KMS（仅 fbdev `mtkfb`）；无 namespaces/LXC |
| **负载** | ✅ | loadavg~10 = **MTK D 态内核线程假象**（display/CCCI/battery 线程阻塞），实际 CPU 空闲，非真实负载 |

**存储**：`/data`=mmcblk0p31、`/system`=mmcblk0p29（均 ext4）。**分区 by-name**：`/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/`。

---

## 2. 显示/GPU 调研（核心话题）

### 2.1 安卓如何输送图像（原版 Android 实测，`dumpsys SurfaceFlinger`）

```
App/UI → Mali-T860 GLES 3.2 (EGL Midgard r12p0, eglSwapBuffers 25µs)
      → ion/dma-buf gralloc 缓冲（/dev/ion）
      → EGL_ANDROID_image_native_buffer + fence sync 交接
      → SurfaceFlinger → HWC(hwcomposer.mt6755.so, gles_count=3 可GPU合成)
      → mtk_disp_mgr + mtk_cmdq → 显示控制器**直接扫出 ion 缓冲**
      → DSI → 屏
```

**全程零拷贝，无 CPU memcpy。** 关键节点：`/dev/ion`、`/dev/graphics/fb0`（mtkfb 1080x1920 32bpp 三缓冲）、`/dev/mali0`、`/dev/mtk_disp_mgr`、`/dev/mtk_cmdq`、`/dev/MTK_SMI`。

### 2.2 卡点（用户态无法触达零拷贝扫出路）

- 内核**只给 fbdev（mtkfb），无 DRM/KMS**（`/dev/dri` 不存在）→ 用户态无 plane/overlay 接口；
- 唯一能驱动显示控制器的用户态接口 = **安卓 hwcomposer HAL**（C 接口 HAL，可被 libhybris 加载）；
- 裸 chroot **无安卓显示初始化环境**（无 property 服务、无 display init 序列、无 SurfaceFlinger）→ 只能 **CPU memcpy 进 fb0**（这就是"最后还得 CPU 输送"）。

### 2.3 关键区分（此前被混淆）

| | 是什么 | Flick/libhybris 需要吗 |
|---|---|---|
| **Google 安卓框架** | SurfaceFlinger / system_server / zygote / binder | ❌ 不需要 |
| **安卓 vendor 显示 HAL** | `hwcomposer.mt6755.so` + `gralloc.mt6755.so` + `libGLES_mali.so`（C 接口 HAL） | ✅ 就用这个 |

**Mobian/Droidian/Flick = 第二行**：Linux 合成器经 libhybris **直接驱动 vendor 显示 HAL**，无需 SurfaceFlinger。HWC 是 C 接口，谁都能驱动。

### 2.4 chroot 里的工作（`/data/debian/root`）

- **`Flick/`**（Mobian shell）：`drm-hwcomposer-shim/`（Rust）+ `hwc-shim/`（C，`flick_hwc.c`）+ `start_drm.sh`/`start_flick.sh` + `install-droidian/mobian.sh`；
- **`drm-hwcomposer-shim`**：Rust 写的 "DRM/KMS shim over Android hwcomposer for Linux phones"——**build 成功**（`target/release/libdrm_hwcomposer_shim.so` 1.1MB + `test_hwc`/`test_gbm`/`test_c_api`），`build.rs` 链 libhybris 全家桶（`libhwc2.so` `libgralloc.so` `libhybris-hwcomposerwindow.so` `libhardware.so`）；
- **原链路**：`libhybris` + `HYBRIS_EGLPLATFORM=null`（离屏）+ `pbo-bench`（PBO 读回）+ `fb0-flip-daemon.py`（CPU 翻转 fb0）——**卡在 CPU**；
- 其它件：`libGL-gl4es.so.1`（GL4ES）、`probe21-ion-alloc`（ion 探测）、`GLES2/`、`Flick` 测试脚本（`test_shim.sh`/`test_weston.sh`）。

### 2.5 为什么失败：视角错位（不是方案错）

- **`drm-hwcomposer-shim` 编译成功，但从未跑起来**——测试脚本假设 **Halium/Droidian** 环境：`systemctl stop phosh`、`/usr/lib/halium-wrappers/android-service.sh`、`android-service@hwcomposer.service`、`pkill graphics.composer`；
- 用户实际环境：**TWRP + Debian chroot + 无 systemd + 无 halium-wrappers + Android 7（HWC 1.x，非 HIDL）**；
- **结论：核心件（shim/libhybris）通用没错，但接线/测试全是 Halium 世界的**——上一任以"Halium/Droidian/Mobian 手机"为视角，而非"TWRP+chroot"这台机的视角。错位的是视角，不是硬件。

---

## 3. 环境分析（三环境 + 库墙）

### 3.1 三个用户态环境，平行独立，各自一套库

| 环境 | rootfs/运行时 | libc | 位数 |
|---|---|---|---|
| **Android system** | boot ramdisk + `/system` + `/vendor` | **bionic** | **64 位**（arm64-v8a，`linker64`） |
| **TWRP 内存盘** | recovery ramdisk | bionic（TWRP 自己构建那份） | **32 位**（runner 只有 32 位库） |
| **Debian chroot** | `/data/debian` | **glibc** | 64 位 |

**墙 = bionic vs glibc → libhybris**（glibc 进程加载 bionic 安卓库的唯一桥）。Android/TWRP 虽同 bionic，但是各自独立副本，跨借需显式 `LD_LIBRARY_PATH`。

### 3.2 "修库"苦活的根源：TWRP(32位) 启动 Android(64位) 二进制

每跑一个 64 位安卓二进制都要手动补 `lib64`，且防污染：
- `01-fuelgauged.sh`：`LD_LIBRARY_PATH` 只给该进程、**不 export**（防毒 64 位工具如 pidof）；
- `02-wifi.sh`：`wpa_supplicant` **64 位要 lib64**；
- `50-static-ip.sh`：`ip` 是 64 位，runner 只有 32 位库 → **segfault 静默失败**，需 export `lib64` 路径；
- `30-chroot-services.sh`：进 chroot 前 `unset LD_PRELOAD LD_LIBRARY_PATH`（防污染 glibc），用 **busybox chroot**（宿主 chroot 是 bionic 的）。

### 3.3 systemd = 只是 PID-1 的 supervisor

Halium 的 `android-service@hwcomposer.service` 实质是 `android-service.sh`（设安卓环境 + 拉起 HAL），**systemd 只是壳**。可用 **supervisord**（已有）替代：启动 + priority 依赖 + autorestart 崩溃重启 + 自写 launcher（设安卓环境拉起 HAL）= 完整覆盖 systemd 角色。

---

## 4. 方案（明天的计划）

### 4.1 思路：从 TWRP 视角换成 **chroot /system 视角**

- **不再跨 32位→64位、bionic→glibc 边界**——在 `/system` 里二进制找自己的 `lib64`/`vendor/lib64` 是原生的，"修库"苦活消失；
- 抄 `init.mt6755*.rc` **精准按需启动**（不是硬跑整份 init.rc）：
  1. **property 服务**（HAL 读 props，裸 chroot 没有）；
  2. **servicemanager**（binder 服务注册）；
  3. **display/HAL 的 service 定义**（init.mt6755*.rc 里那几段）。
- **supervisord 直接管理驱动启动**（守护 + 崩溃重启 + 按需拉起）。

### 4.2 如何给安卓图像服务发任务

安卓图像服务 = **binder 服务（ISurfaceComposer）**，交互走安卓窗口/缓冲队列协议：

```
任务 = 创建 surface → dequeueBuffer → 渲染进缓冲 → queueBuffer → SF 合成 → HWC → 屏
```

两条路：
- **路线 B（直驱 hwcomposer，无需 SurfaceFlinger）**：`EGL_PLATFORM=hwcomposer` + `libhybris-hwcomposerwindow.so`（ANativeWindow = hwcomposer 输出窗）→ Mali 渲染 → `eglSwapBuffers` → hwcomposer 扫出。**件已全有**（shim build.rs 已链）；
- **路线 A（走 SurfaceFlinger，binder）**：libhybris + `libgui`/`libui`（`SurfaceComposerClient`→`createSurface`→`IGraphicBufferProducer`→`dequeueBuffer`→渲染→`queueBuffer`→SF 合成）。

### 4.3 X11 应用零拷贝上屏

```
X11 应用(glibc) → Xwayland → Weston(合成器)
  → libhybris EGL on hwcomposer window（EGL_PLATFORM=hwcomposer + libhybris-hwcomposerwindow.so）
  → Mali 渲进 gralloc/ion 缓冲（零拷贝: GPU→缓冲）
  → eglSwapBuffers → hwcomposer 扫出该缓冲（零拷贝: 缓冲→屏）
```

**全程无 CPU memcpy**（Droidian/Mobian 量产做法）。替代：`drm-hwcomposer-shim`（LD_PRELOAD）把 hwcomposer 伪装成 DRM 设备，Weston/Xorg modesetting 直接用。
**make-or-break 唯一一点**：`hwcomposer`+`gralloc`+`libGLES_mali` 经 libhybris 能否正常初始化+扫出——4.1 的"chroot /system + init 按需启动"正是解决它。

---

## 5. 发现的问题（待修）

1. **`50-static-ip.sh` 死代码 bug**：`exit 0` **后面**还有"冻结 recovery（`kill -STOP $(pidof recovery)`）+ 电源键切背光（`power-key-toggle.py`）"——**永不执行**。若电源键亮屏不好使，就是它。
2. **`02-wifi.sh` 硬编码 WiFi 凭据**（ssid=`Xiaomi_F5A3` psk=`123456789`）——个人环境可用，注意别外泄。

---

## 6. 下一步行动

1. **明天**：supervisord 直接管理 HAL 启动；抄 `init.mt6755*.rc` 精准按需启动（property 服务 + servicemanager + display HAL service）；chroot `/system` 视角。
2. 先打通**路线 B**（hwcomposer 直驱，`EGL_PLATFORM=hwcomposer`），再上路线 A（SurfaceFlinger）。
3. X11 零拷贝链路（Xwayland→Weston→hybris EGL→hwcomposer→屏）。
4. 可选：修 `50-static-ip.sh` 死代码。

---

*记录来源：2026-11-04 与 AI 助手（商量/SensenNova）的全盘讨论，设备 192.168.31.213（Meizu M5 Note）。*
