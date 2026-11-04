# 魅蓝 Note5 TWRP chroot /system 跑通 SurfaceFlinger —— GPU 零拷贝显示到屏幕

**日期**：2026-11-04
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，Android 7.0 / Flyme 7.0.1.0A，kernel 3.18.35，4 核，3GB RAM）
**环境**：TWRP recovery + chroot（非正常启动 Android，未刷 Halium，未换内核）
**结论**：**成功**。SurfaceFlinger 在 chroot /system 里完整初始化并注册服务，Mali-T860 GPU 加速，hwcomposer.mt6755.so 合成，bootanimation 画面显示到物理屏幕（唯一遗留：画面 180° 倒转，根因已定位，有解）

---

## 一、成果一句话

**在 TWRP recovery 里 `chroot /system`，把 Android 原生显示栈（servicemanager + surfaceflinger + hwcomposer.mt6755 + Mali GPU）完整跑起来，应用经 binder → SF → hwcomposer → 屏幕，GPU 零拷贝出画面。**

这是昨天 libhybris 单环境反复撞墙（`no session 196610`、Mali kmap=0、window 函数表 NULL）走不通后，改用「原生 bionic 环境跑原生栈」的思路，一次跑通。

---

## 二、路线对比（为什么之前不行）

| 路线 | 做法 | 结果 | 卡点 |
|------|------|------|------|
| libhybris 单环境 | glibc 进程经 libhybris 桥接 bionic 的 EGL/gralloc/hwcomposer | ❌ | Mali kernel import gralloc buffer 失败（kmap=0）；hwcomposer window 缺 BufferQueue；`no session 196610` |
| panfork（Tensor G1 方案） | Panfrost+kbase 用户态驱动直连 /dev/mali0 | ❌ | 需配套内核改动，MT6755 3.18 无 |
| drm-hwcomposer-shim | 用户态 DRM 模拟 | ❌ | 只是 shader 编译模拟，无真实渲染 |
| **chroot /system 原生** | **chroot 进 /system，原样跑 Android 的 servicemanager + surfaceflinger + hwcomposer HAL** | ✅ **成功** | 只需修若干环境细节（见第四节） |

**核心判断**：Android 显示栈在正常 Android 里本来就工作（`dumpsys SurfaceFlinger` 证明）。chroot /system 是把它**原样复现**，环境一致（原生 bionic + 原生库 + 原生 HAL），不借不桥不缝。

---

## 三、最终可复现步骤（完整，按顺序）

### 3.1 准备 chroot 根（/data 下，可写）

```bash
# /system 分区是 ro，不能直接当 chroot 根；在 /data 建可写根 + bind
mkdir -p /data/chroot-system/{dev,proc,sys,tmp,system,vendor,data}
mount --bind /system /data/chroot-system/system
mount --bind /dev    /data/chroot-system/dev
mount --bind /proc   /data/chroot-system/proc
mount --bind /sys    /data/chroot-system/sys
mount --bind /data   /data/chroot-system/data      # ★ 必须，否则 SF SIGABRT
```

### 3.2 补 SELinux 服务上下文

```bash
# servicemanager 启动时需要 /service_contexts（缺则 selinux_android_service_context_handle 失败）
cp /service_contexts /data/chroot-system/service_contexts
```

### 3.3 修 /vendor symlink

```bash
# chroot 里 /vendor 必须是 symlink → /system/vendor（若是空目录，SF 卡死）
chroot /data/chroot-system /system/bin/sh -c 'rmdir /vendor 2>/dev/null; ln -sf /system/vendor /vendor'
```

### 3.4 修 hwcomposer 模块名（property 读不到时的兜底）

```bash
# chroot 里 Android property 区域不可用 → SF 的 ro.hardware 为空 → hw_get_module 找 hwcomposer.default.so
# 系统只有 hwcomposer.mt6755.so，建 symlink 命中
chroot /data/chroot-system /system/bin/sh -c '
  cd /system/vendor/lib64/hw
  ln -sf hwcomposer.mt6755.so hwcomposer.default.so
  ln -sf gralloc.mt6755.so     gralloc.default.so
'
```

### 3.5 ★★★ 挂载 selinuxfs（最关键的坑）

```bash
# TWRP 里 /sys/fs/selinux 是空目录，SELinux status 不可读
# → servicemanager 的 svc_can_register 拒绝所有服务注册 → SF 注册失败
mount -t selinuxfs selinuxfs /sys/fs/selinux
getenforce     # 应为 Permissive
```

### 3.6 启动 servicemanager

```bash
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  setsid /system/bin/servicemanager >/data/local/tmp/sm.log 2>&1 &
'
# 验证：日志应输出 "SELinux: Loaded service_contexts from /service_contexts."
#       进程应有 binder fd（3 -> /dev/binder）
```

### 3.7 启动 surfaceflinger

```bash
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  export ANDROID_ROOT=/system
  export ANDROID_DATA=/data
  setsid /system/bin/surfaceflinger >/data/local/tmp/sf.log 2>&1 &
'
# 验证：线程数 ~30（正常），有 binder + /dev/mali0 + /dev/mtk_disp_mgr + /dev/ion fd
#       service list 应出现 SurfaceFlinger
```

### 3.8 验证（决定性）

```bash
service list
#   0  GuiExtService: [GuiExtService]
#   1  gpu: [android.ui.IGpuService]
#   2  SurfaceFlinger: [android.ui.ISurfaceComposer]

dumpsys SurfaceFlinger | head -40
#   GLES: ARM, Mali-T860, OpenGL ES 3.2 v1.r12p0
#   DisplayDevice: Built-in Screen (1080x1920), powerMode=2, isDisplayOn=1
#   refresh-rate: 60.84 fps

# 送一个 layer 验证显示（bootanimation 是现成的 SF 客户端）
chroot /data/chroot-system /system/bin/sh -c '
  export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64
  setsid /system/bin/bootanimation >/data/local/tmp/ba.log 2>&1 &
'
dumpsys SurfaceFlinger | grep -A2 "Visible layers"
#   Visible layers (count = 1)
#   + Layer (BootAnimation)
#   HWC | ... | BootAnimation    ← hwcomposer 正在合成
```

**屏幕出现 Flyme 开机动画 = 全链路打通**（应用 → binder → SF → hwcomposer → Mali GPU → 屏幕）。

---

## 四、踩坑全记录（每个坑的根因与修复）

### 坑1：/system 只读，不能直接 chroot

- **现象**：`mount -o remount,rw /system` 失败（`Device or resource busy`），无法在 /system 内 mkdir /dev /proc /sys
- **根因**：/system 是 ro 挂载的 ext4
- **修复**：在 /data 建可写 chroot 根，把 /system 等 bind 进去（3.1）

### 坑2：chroot 里 sh 找不到 vendor 库

- **现象**：`libdirect-coredump.so not found`；显式调 linker64 反而 Segmentation fault
- **根因**：linker64 默认库路径不含 /vendor/lib64
- **修复**：`export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64`；**直接跑 ELF，不要显式调 linker64**

### 坑3：SF 启动即 SIGABRT（退出码 134）

- **现象**：chroot 里 SF 一启动就 `Aborted`，chroot 外同一二进制能跑
- **根因**：chroot 里没有 /data（chroot 根的 /data 不是真 /data），SF 访问 /data/system 等失败
- **修复**：`mount --bind /data /data/chroot-system/data`

### 坑4：servicemanager 启动失败（SELinux 服务上下文）

- **现象**：`selinux_android_service_context_handle: Error getting service context handle (No such file or directory)`，进程无 binder fd
- **根因**：servicemanager 需要 `/service_contexts`，chroot 根里没有
- **修复**：`cp /service_contexts /data/chroot-system/service_contexts`
- **验证**：日志出现 `SELinux: Loaded service_contexts from /service_contexts.`，进程有 binder fd

### 坑5：SF 卡在 sigsuspend（线程数=1，无 binder fd）

- **现象**：SF 进程状态 S，wchan=sigsuspend，fd 只有 0/1/2 socket，线程数 1（正常应 30）
- **根因**：chroot 里 `/vendor` 是**空目录**而非 symlink → /vendor/lib64 库路径断 → SF 初始化早期卡住
- **修复**：`ln -sf /system/vendor /vendor`（在 chroot 内）

### 坑6：SF 不加载 hwcomposer.mt6755.so

- **现象**：SF 起来了（30 线程 + binder + mali0 + fb0），但 maps 里只有 `gralloc.default.so`，没有 `hwcomposer.mt6755.so`
- **根因**：chroot 里 Android property 区域不可用 → `ro.hardware` 读为空 → `hw_get_module("hwcomposer")` 找 `hwcomposer.default.so`（不存在）→ 跳过
- **修复**：`ln -sf hwcomposer.mt6755.so hwcomposer.default.so`（+ gralloc 同理）
- **验证**：maps 里出现 `/system/vendor/lib64/hw/hwcomposer.mt6755.so` + `/system/vendor/lib64/hw/gralloc.mt6755.so`

### 坑7：★★★ SF 不注册 SurfaceFlinger 服务（最隐蔽）

- **现象**：SF 稳定运行（30 线程、epoll_wait、GPU 线程 kbase_read、LCD ON），但 `service list` 返回 `Found 0 services`，`dumpsys SurfaceFlinger` 报 `Can't find service: SurfaceFlinger`，屏幕黑（有背光）
- **根因**：**TWRP 里 `/sys/fs/selinux` 不存在**（getenforce 报 `No such file or directory`）→ servicemanager 的 `selinux_status_getenforce()` 拿不到状态 → `svc_can_register` 拒绝所有服务注册 → SF 的 `addService("SurfaceFlinger")` 失败
- **修复**：`mount -t selinuxfs selinuxfs /sys/fs/selinux` → `getenforce` 返回 `Permissive` → 重启 servicemanager + SF
- **验证**：`service list` 出现 `SurfaceFlinger: [android.ui.ISurfaceComposer]`、`GuiExtService`、`gpu` 三个服务
- **这个坑是"黑屏有背光"的真正原因，不是 hwcomposer 的锅**

### 坑8：BINDER_VERSION ioctl 误判（自己吓自己）

- **现象**：自写测试程序 `ioctl(BINDER_VERSION)` 返回 EINVAL，一度误以为 TWRP 内核 binder 驱动坏了
- **根因**：ioctl 号写错（用了 `_IOWR('b', 3, int)`，Android 实际是 `_IOWR('b', 9, struct binder_version)`）
- **修复**：改用 9 号 → `BINDER_VERSION: r=0 protocol=8`，binder 驱动完全正常
- **教训**：binder 驱动没问题；且 `BINDER_SET_CONTEXT_MGR` 一直返回成功，可作为交叉验证

---

## 五、验证证据（实测数据）

```
service list:
  0  GuiExtService: [GuiExtService]
  1  gpu: [android.ui.IGpuService]
  2  SurfaceFlinger: [android.ui.ISurfaceComposer]

dumpsys SurfaceFlinger:
  Build configuration: [sf] [libui] [libgui]
  Sync configuration: [using: EGL_ANDROID_native_fence_sync EGL_KHR_wait_sync]
  DispSync configuration: refresh 16436554 ns
  EGL implementation : 1.4 Midgard-"r12p0-04rel0"
  GLES: ARM, Mali-T860, OpenGL ES 3.2 v1.r12p0-04rel0
  DisplayDevice: Built-in Screen, type=0, hwcId=0, (1080x1920), powerMode=2, activeConfig=0
  orientation=0, isDisplayOn=1
  refresh-rate: 60.840003 fps
  last eglSwapBuffers() time: 46.847 us
  flips=25009            ← 持续合成 25009 帧
  numLayers=1            ← bootanimation 已送来

HWC 合成行:
  HWC | ... | RGBx_8888 | 0,0,1080,1920 | BootAnimation
```

SF 打开的硬件 fd（关键证据）：
```
 4 -> /dev/binder
 7 -> /dev/mali0                    ← Mali GPU
11/12 -> /dev/graphics/fb0
12 -> /dev/mtk_disp_mgr             ← MTK 显示管理器
14 -> /dev/sw_sync
15 -> /proc/mtk_cmdq
18 -> /dev/ion                      ← ION 内存（明日 libhybris 卡 kmap=0 的地方，这里正常）
```

SF 加载的 MTK 库：
```
/system/vendor/lib64/egl/libGLES_mali.so
/system/vendor/lib64/libgralloc_extra.so
/system/vendor/lib64/libion_mtk.so
/system/vendor/lib64/libdpframework.so
/system/vendor/lib64/libged.so      ← MTK GPU 调度
/system/vendor/lib64/libpq_*.so     ← PQ 图像质量
/system/vendor/lib64/hw/hwcomposer.mt6755.so
/system/vendor/lib64/hw/gralloc.mt6755.so
```

GPU 线程活跃：`tid wchan=kbase_read`（Mali kernel driver 在读）。

---

## 六、资源占用（实测）

| 进程 | RSS | 线程 |
|------|-----|------|
| surfaceflinger | **~25 MB** | 30 |
| servicemanager | **~1.6 MB** | 1 |
| 合计 | **~27 MB** | |

| 系统项 | 数值 |
|--------|------|
| MemTotal | 2.8 GB |
| 显示栈占物理内存 | 27 MB ≈ **1%** |
| zram swap | 1.46 GB |
| 刷新率 | 60.84 fps |

**结论**：显示栈开销极小（<1% 内存），完全可接受。

---

## 七、遗留问题：画面 180° 倒转（根因已定位）

### 现象
bootanimation 显示在屏幕上，但**上下颠倒**（Flyme 的屏幕像素方向需要 180° 旋转）。

### 根因
- Flyme 的 `/system/build.prop` 里配置 `ro.sf.hwrotation=180`
- `hwcomposer.mt6755.so` / `libgui_ext.so` / `libsurfaceflinger.so` 都读这个 property 决定旋转
- **但 chroot 里 Android property 区域不可用**：
  - TWRP 的 `/dev/__properties__` 是 recovery 最小版，`properties_serial` 只登记 recovery 用的 context
  - `system_prop` / `default_prop` 没被登记 → Android libc 的 `__system_property_get` 不会 mmap 它们
  - 结果 SF 读 `ro.sf.hwrotation` = 空 → `hworient=0` → 不旋转 → 画面倒转
- 证据：`dumpsys SurfaceFlinger` 里 `hworient=0`、`transform=[[1,0,0][0,1,0][0,0,1]]`（identity）；SF 的 maps 里 `__properties__` 计数 = 0（根本没 mmap）

### 三个解法（按推荐度）

**解法A（最实用，推荐）**：Debian 侧抵消
- 屏幕 180° 倒转只在**开发/调试直观性**上有影响
- 真正要用的是 Debian 桌面（经 libhybris → SF 显示）。Debian 侧 Xorg 直接做 `Rotate 180` 抵消即可
- 项目里已有现成配置：`/tmp/xorg.conf.rotate180`
- **不需要动 Android 侧任何东西**

**解法B**：让 chroot 里的 Android property 区域完整初始化
- 在 chroot 里跑 Android init 的 property 初始化部分（`__system_property_area_init()`），会登记所有 context 并加载 build.prop
- 风险：init 会尝试启动 zygote/服务，需要最小化 init.rc 或只跑 property 阶段
- 或写 bionic 小程序调 `__system_property_area_init()` + 重新登记 → 需要 Android 编译环境

**解法C**：patch hwcomposer.mt6755.so
- 把 `ro.sf.hwrotation` 读取改成硬编码 180（二进制 patch）或打补丁重编
- 副作用：后续换屏/换配置要重 patch
- 不推荐（脆）

---

## 八、与前面探索的关系（路线收敛）

| 时间 | 尝试 | 结果 |
|------|------|------|
| 11-03 | libhybris 编译 + GPU 离屏加速 | ✅ Mali-T860 可用，PBuffer 17654 FPS |
| 11-03 | libhybris 直连 hwcomposer（eglCreateWindowSurface / eglCreateImageKHR / hwcomposer 手动 prepare/set） | ❌ 全部撞墙（kmap=0 / 函数表 NULL / no session 196610） |
| 11-03 | CPU 读回（PBO 双缓冲） | ⚠️ 带宽硬上限 ~6MB/s（512×512 = 5.8 FPS） |
| 11-03 | panfork（Tensor G1 思路） | ❌ 需配套内核改动 |
| 11-04 | **chroot /system 原生跑 SF** | ✅ **成功** |

**结论**：真零拷贝的正确路径不是"让 glibc 桥接 bionic 的显示栈"，而是"让显示栈在它自己的原生环境里跑"。GPU 零拷贝本来就该由 SF + hwcomposer 完成——chroot /system 让它原生工作。

---

## 九、下一步（按价值排序）

1. **Debian 桌面接 SF**（双 chroot 协作终点）
   - chroot /data/debian 里经 libhybris 连 SF 的 `SurfaceFlinger` 服务
   - 或 libhybris 加一个"android/SurfaceComposerClient"平台
   - 预期：Debian X11/GL 应用 → libhybris → SF → hwcomposer → 屏幕，GPU 零拷贝
2. **同步 side 写脚本固化**（把第三节步骤做成一个 chroot-system-up.sh，开机自动）
3. **倒转按解法 A 处理**（Debian 侧 Rotate 180）
4. 资源与稳定性长跑（SF 跑几小时看有无泄漏）

---

## 十、附录：WiFi/网络脚本的库问题（同日分析）

**结论**：WiFi/网络脚本的库问题 = **bionic(64位) vs TWRP(32位库路径) 边界**，与 GPU 那条路同源（都是"库环境不匹配"）。

| 脚本 | 库问题 | 处理 |
|------|--------|------|
| `twrp/hooks/02-wifi.sh` | wpa_supplicant 64位需 lib64 | ✅ `LD_LIBRARY_PATH=/system/lib64:...` |
| `twrp/hooks/50-static-ip.sh` | ip 命令 64位需 lib64（否则 segfault 静默失败） | ✅ 同 |
| `twrp/hooks/30-chroot-services.sh` | 进 chroot 前库环境污染 glibc | ✅ `unset LD_PRELOAD LD_LIBRARY_PATH` |
| `twrp/modules/wifi-up/service.sh` | /system/bin/ip 需 system libs | ✅ 注释已写明 |

- 依赖库 64 位版**都存在**（/system/lib64，find 确认），不是"缺库"
- 根因：TWRP runner 默认 `LD_LIBRARY_PATH` 只指向 32 位库
- 通用规则：TWRP(32位) 跑任何 Android 64 位二进制都要 `LD_LIBRARY_PATH` 含 `/system/lib64` —— **和 SF 启动用的完全一致**
- 唯一未真修：`wpa_cli` segfault（是 ctrl_interface socket/路径问题，非库问题，被 `timeout` 掩盖）

---

## 十一、关键文件与命令速查

```bash
# chroot 环境进入
chroot /data/chroot-system /system/bin/sh -c '...'

# 必须的环境变量
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/vendor/lib64

# 修复清单（一次性）
cp /service_contexts /data/chroot-system/service_contexts
ln -sf /system/vendor /data/chroot-system/vendor      # 或 chroot 内 ln -sf
ln -sf hwcomposer.mt6755.so hwcomposer.default.so     # 在 /system/vendor/lib64/hw/
mount -t selinuxfs selinuxfs /sys/fs/selinux          # ★ 最容易被忽略

# 启动顺序
servicemanager → surfaceflinger → (客户端/bootanimation)

# 验证
service list | grep SurfaceFlinger
dumpsys SurfaceFlinger | grep -E 'GLES|powerMode|refresh|flips|layers'
```
