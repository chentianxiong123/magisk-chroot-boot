# Android GPU 驱动路径（不动框架、不换内核）

**状态**：探索中（未实现，仅思路）
**日期**：2026-11-03
**约束**：
- ❌ 不动现有框架（Xorg + openbox + lxpanel + pcmanfm + x11vnc）
- ❌ 不启 Android 框架（hwcomposer/SurfaceFlinger/gralloc）
- ❌ 不换内核（3.18.35+ mali_kbase r12p1 保留）
- ❌ 不用 Android blob（libGLES_mali.so 不用）
- ✅ 只用内核 mali_kbase + 用户态 Mesa Panfrost

---

## 驱动路径：Mesa Panfrost + kbase ioctl + X11 DRI3

### 核心思路

**Mesa Panfrost**（开源 Mali 驱动）+ **kbase ioctl**（直接调 /dev/mali0）+ **X11 DRI3**（呈现到 Xorg）

```
Linux 应用（glibc）
  ↓ libEGL / libGL（Mesa）
  ↓ Mesa Panfrost（开源 Mali 驱动，支持 T860）
  ↓ kbase ioctl（/dev/mali0，直接调）
  ↓ GPU 硬件（Mali-T860 MP2）
  ↓ DMA-BUF（GPU buffer）
  ↓ X11 DRI3（呈现到 Xorg）
  ↓ 屏幕（fbdev）
```

### 为什么可行

- ✅ mali_kbase r12p1（已在跑，/dev/mali0 存在）
- ✅ Mesa Panfrost（开源，支持 Midgard/T860）
- ✅ Xorg（已在跑，fbdev 输出）
- ✅ kbase ioctl（Panfrost 直接调，不依赖 DRM）
- ✅ DMA-BUF / ION（你的设备有）
- ❌ 不需要 hwcomposer
- ❌ 不需要 Android blob
- ❌ 不需要换内核
- ❌ 不需要启 Android 框架

---

## 具体步骤（探索，不动框架）

### 步骤 1：确认 mali_kbase 接口

```bash
# 内核版本
cat /sys/module/mali_kbase/version
# r12p1-01alp0 (UK version 10.2)

# 设备节点
ls -la /dev/mali0
# crw------- 1 root root 10, 60 ... /dev/mali0

# kbase ioctl（Panfrost 需要的接口）
cat /proc/kallsyms | grep kbase_ioctl
# ffffffc00053ccd0 t kbase_ioctl
```

**Panfrost 对 r12p1 的支持**：
- Midgard/T860（r12p1 应该支持）
- 参考：https://gitlab.freedesktop.org/panfrost/panfrost

### 步骤 2：编译 Mesa Panfrost（ARM64）

```bash
# Mesa 源码
git clone https://gitlab.freedesktop.org/mesa/mesa.git
cd mesa

# 编译选项（对接 kbase ioctl）
meson build \
  -Dgallium-drivers=panfrost \
  -Dplatforms=x11 \
  -Ddri3=enabled \
  --buildtype=release

# 依赖
# - libdrm（Panfrost 用 kbase ioctl，不依赖 DRM/KMS）
# - X11（X11 DRI3）
# - LLVM（可选，软渲染 fallback）
```

### 步骤 3：DMA-BUF + kbase ioctl（Panfrost 内存分配）

```bash
# Panfrost 用 kbase ioctl 分配 GPU buffer
# DMA-BUF 导出（用于 X11 DRI3 呈现）
# 需要：dma_heap 或 ION（你的设备有 ION）
ls -la /dev/ion
```

### 步骤 4：X11 DRI3 呈现（对接 Xorg）

```bash
# Panfrost 渲染到 DMA-BUF → X11 DRI3 呈现到 Xorg
# 需要：Xorg DRI3 支持（x11-dri3）
# 或者：CPU presenter（渲染结果拷贝到 fb0）

# Xorg 配置（DRI3）
# /etc/X11/xorg.conf
Section "Device"
    Driver "modesetting"
    Option "DRI3" "on"
EndSection
```

### 步骤 5：测试（glmark2 / glxgears）

```bash
# Mesa Panfrost + kbase ioctl + X11 DRI3
# 跑 glmark2（测试 GPU 硬件加速）
apt install glmark2
glmark2 --run
```

---

## 通用性

**这个方案通用**（换台 Mali 设备用同一套 Mesa Panfrost + kbase ioctl）：

- ✅ Mesa Panfrost 开源（跨 Midgard/T8xx）
- ✅ kbase ioctl 标准接口（跨设备）
- ✅ X11 DRI3 标准呈现（跨设备）

**依赖**：
- ✅ mali_kbase（内核驱动，已在跑）
- ✅ Mesa Panfrost（用户态驱动，编译）
- ✅ Xorg（显示服务器，已在跑）
- ✅ DMA-BUF / ION（内存分配，已在跑）

---

## 参考项目（只参考思路，不用别人写好的东西）

- **Mesa Panfrost**：https://gitlab.freedesktop.org/mesa/mesa
- **tensor-g1-proot-gpu**（Panfork 补丁，直接调 kbase）：https://github.com/SaicharanKandukuri/tensor-g1-proot-gpu
- **Panfork**（Panfrost 补丁版）：https://gitlab.freedesktop.org/panfrost/panfork

---

## 风险

- ⚠️ Panfrost 对 r12p1 的支持（需验证，r12p1 较老）
- ⚠️ kbase ioctl 接口兼容性（Panfrost 期望的 ioctl 版本）
- ⚠️ DMA-BUF / ION 兼容性（Panfrost 用 ION 分配）
- ⚠️ X11 DRI3 呈现（Xorg DRI3 支持，或 CPU presenter fallback）

---

## 结论（探索中，未验证）

**驱动路径**：Mesa Panfrost + kbase ioctl + X11 DRI3

**不动框架**：
- ❌ 不启 Android 框架
- ❌ 不用 hwcomposer
- ❌ 不用 Android blob
- ❌ 不换内核
- ✅ 只用内核 mali_kbase + 用户态 Mesa Panfrost

**通用性**：✅ 高（跨 Mali 设备，Mesa Panfrost + kbase ioctl + X11 DRI3 标准接口）

**下一步**：
1. 验证 Panfrost 对 r12p1 的支持
2. 编译 Mesa Panfrost（ARM64）
3. 测试 kbase ioctl + DMA-BUF
4. 对接 Xorg（X11 DRI3 或 CPU presenter）
5. 跑 glmark2（测试 GPU 硬件加速）
