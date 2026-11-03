# Android GPU 对接 Linux 桌面：通用方案探索笔记

**状态**：探索中（未实现，仅思路）
**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，mali_kbase r12p1，Android 6.0）

---

## 背景问题

**目标**：Android 设备（有 mali_kbase 内核驱动 + libGLES_mali blob）跑 chroot Linux 桌面，**用 GPU 硬件加速**（不是软件渲染）。

**当前方案**（软件渲染）：
- Xorg fbdev + x11vnc，CPU 3.5%（软件渲染）
- 桌面流畅，OpenGL 应用慢（视频/3D 走 CPU）

**目标方案**（硬件加速）：
- 用 GPU（Mali-T860）渲染 OpenGL
- 桌面流畅 + OpenGL 应用快

---

## 通用方案对比（3 条路径）

| 路径 | 原理 | 通用性 | 工程量 | 风险 |
|---|---|---|---|---|
| **A: libhybris** | glibc 桥接 Bionic libc，调 Android libGLES_mali.so，FBDEV 后端 | ✅ 高（Halium/Ubuntu Touch 用）| 中 | hwcomposer HAL 缺失 |
| **B: DRM compat + Mesa** | 写 kbase_ioctl → DRM ioctl 翻译层，Mesa Panfrost 标准栈 | ✅ 高（Panfrost 开源，支持 Midgard）| 大 | 内核无 DRM，需改内核 |
| **C: Vulkan WSI wrapper** | Mesa wrapper 加 X11 WSI，调 Android Mali Vulkan | ⚠️ 半通用 | 中 | Vulkan blob 存在性 |

---

## 路径 A：libhybris（最通用，Halium 生态）

### 原理
- **libhybris**：glibc 和 Bionic libc 桥接层
- **Android blob**：libGLES_mali.so（用户态 GPU 驱动）
- **显示后端**：FBDEV（/dev/graphics/fb0）或 hwcomposer HAL

### 调用链
```
Linux 应用（glibc）
  ↓ libEGL（libhybris 版）
  ↓ libhybris（桥接 Bionic libc）
  ↓ Android libEGL.so（Bionic libc）
  ↓ Android libGLES_mali.so（Bionic libc）
  ↓ mali_kbase ioctl（/dev/mali0）
  ↓ GPU 硬件
  ↓ DMA-BUF / ION buffer
  ↓ hwcomposer HAL（合成）或 FBDEV（直出）
  ↓ 屏幕
```

### 依赖（你的设备验证）
- ✅ Bionic libc（libc/libcutils/libutils/libbinder）
- ❌ hwcomposer HAL（**你的 ROM 裁剪了 hwcomposer**，只有 gralloc.default.so）
- ✅ FBDEV（/dev/graphics/fb0）
- ✅ ION（/dev/ion）
- ✅ binder（/dev/binder）
- ✅ libGLES_mali.so（24.5MB blob）+ mali_kbase r12p1（版本匹配）

### 通用性
- ✅ 高（Halium/Ubuntu Touch/Sailfish 都用）
- ✅ 跨 SoC/GPU/内核（Android HAL 标准接口）
- ✅ libhybris 项目现成（不需要自己写桥接层）

### 工程量
- **中**（libhybris 现成，需适配 hwcomposer workaround）
- hwcomposer 缺失可用 FBDEV 后端绕过（libhybris 支持 FBDEV）

### 风险
- ⚠️ hwcomposer HAL 缺失（可绕过，但需要改 libhybris 配置）
- ⚠️ Android blob 版本兼容性（libGLES_mali r12p1 vs mali_kbase r12p1，已验证匹配）
- ⚠️ DMA-BUF 导出（ION → DMA-BUF，libhybris 有 workaround）

### 参考
- **libhybris**：https://github.com/libhybris/libhybris
- **Halium**：https://halium.org/
- **Ubuntu Touch**：用 libhybris 跑 Android HAL

---

## 路径 B：DRM compat + Mesa Panfrost（最干净，工程量大）

### 原理
- **Mesa Panfrost**：开源 Mali 驱动（支持 Midgard/T860）
- **DRM compat 层**：kbase_ioctl → DRM ioctl 翻译
- **标准 Linux 栈**：DRM + GBM + EGL + X11

### 调用链
```
Linux 应用（glibc）
  ↓ libEGL（Mesa）
  ↓ Mesa Panfrost（开源 Mali 驱动）
  ↓ DRM ioctl（期望 /dev/dri/renderD128）
  ↓ DRM compat 层（kbase_ioctl → DRM ioctl 翻译）
  ↓ mali_kbase ioctl（/dev/mali0）
  ↓ GPU 硬件
  ↓ GBM buffer
  ↓ X11 DRI3（显示）
  ↓ 屏幕
```

### 依赖
- ❌ 内核 DRM/KMS（**你的内核无 /dev/dri/**，需改内核加 CONFIG_DRM + DRM/KMS 驱动）
- ✅ Mesa Panfrost（开源，ARM 官方）
- ✅ GBM/EGL/X11（标准 Linux 栈）

### 通用性
- ✅ 高（Panfrost 开源，支持 Midgard/T860）
- ✅ 标准 Linux 栈（跨设备）
- ❌ 需要改内核加 DRM/KMS（MTK DRM 复杂）

### 工程量
- **大**（改内核 + 编译 Mesa + 写 DRM compat 层）

### 风险
- ❌ 内核无 DRM（需改内核加 DRM/KMS，MTK DRM 复杂）
- ❌ DRM compat 层（kbase_ioctl → DRM ioctl 翻译，语义不匹配）

### 参考
- **Mesa Panfrost**：https://gitlab.freedesktop.org/mesa/mesa
- **ARM Mali 内核驱动**：https://gitlab.freedesktop.org/lima/lima

---

## 路径 C：Vulkan WSI wrapper（半通用）

### 原理
- **Mesa Vulkan wrapper**：加 X11 WSI，调 Android Mali Vulkan blob
- **Vulkan WSI**：X11/XCB WSI 层

### 调用链
```
Linux 应用（glibc）
  ↓ libVulkan（Mesa wrapper）
  ↓ Mesa Vulkan wrapper（加 X11 WSI）
  ↓ Android Mali Vulkan blob（Bionic libc）
  ↓ mali_kbase ioctl（/dev/mali0）
  ↓ GPU 硬件
  ↓ DMA-BUF
  ↓ X11 WSI（显示）
  ↓ 屏幕
```

### 依赖
- ✅ Android Mali Vulkan blob（需验证 libGLES_mali.so 是否含 Vulkan）
- ✅ Mesa Vulkan wrapper（tensor-g1 项目有）
- ✅ X11 WSI 层

### 通用性
- ⚠️ 半通用（需要 Android Vulkan blob）

### 工程量
- **中**

### 风险
- ⚠️ Vulkan blob 存在性（需验证）
- ⚠️ Vulkan WSI 适配（X11/XCB）

### 参考
- **tensor-g1-proot-gpu**：https://github.com/SaicharanKandukuri/tensor-g1-proot-gpu

---

## 通用性判断标准

**一个方案通用 = 换台 Android 手机（不同 SoC/GPU/内核）也能用同一套方法**。

**通用性高的方案**：
- **libhybris**（Halium 生态，跨 SoC/GPU/内核）
- **Panfrost Mesa**（开源 Mali 驱动，跨 Midgard/T8xx GPU）
- **DRM compat 层**（标准 Linux GPU 栈，跨设备）

**通用性低的方案**：
- **tensor-g1-proot-gpu**（特定 Tensor G1 + 特定补丁，不可移植）
- **mesa-for-android-container**（只支持 Adreno，不支持 Mali）

---

## 下一步探索（通用方案）

### 优先级 A：libhybris（最通用，工程量中）
1. **克隆 libhybris**（https://github.com/libhybris/libhybris）
2. **适配 FBDEV 后端**（绕过 hwcomposer 缺失）
3. **编译 libhybris**（ARM64，对接 Android blob）
4. **跑 glmark2**（测试 GPU 硬件加速）
5. **对接 Xorg**（GLX/EGL，输出到 fbdev）

### 优先级 B：DRM compat + Mesa Panfrost（最干净，工程量大）
1. **改内核加 DRM/KMS**（CONFIG_DRM + DRM/KMS 驱动）
2. **编译 Mesa Panfrost**（ARM64，对接 mali_kbase）
3. **写 DRM compat 层**（kbase_ioctl → DRM ioctl 翻译）
4. **跑 glmark2**（测试 GPU 硬件加速）
5. **对接 Xorg**（GLX/EGL，输出到 fbdev）

### 优先级 C：Vulkan WSI wrapper（半通用，工程量中）
1. **验证 libGLES_mali.so 是否含 Vulkan**（strings 检查）
2. **克隆 tensor-g1-proot-gpu**（参考 Mesa Vulkan wrapper）
3. **编译 Vulkan wrapper**（ARM64，对接 Android Mali Vulkan）
4. **跑 vkcube**（测试 Vulkan 硬件加速）
5. **对接 Xorg**（X11 WSI，输出到 fbdev）

---

## 结论（探索中，未验证）

**通用方案**：
- **libhybris**（最通用，Halium 生态，工程量中，hwcomposer 缺失可绕过）
- **Panfrost Mesa + DRM compat**（最干净，标准 Linux 栈，工程量大）
- **Vulkan WSI wrapper**（半通用，需 Vulkan blob）

**建议优先级**：
- **A（libhybris）** → 最通用，工程量中，Halium 生态现成
- **B（Panfrost Mesa）** → 最干净，标准 Linux 栈，工程量大
- **C（Vulkan wrapper）** → 半通用，需 Vulkan blob

**未验证**：所有方案都是思路，**未实现**（需要工程验证）。
