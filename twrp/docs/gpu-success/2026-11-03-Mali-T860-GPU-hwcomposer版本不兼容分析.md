# Mali-T860 GPU hwcomposer 版本不兼容分析

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：drm-hwcomposer-shim 编译成功，但 hwcomposer 1.x vs HWC2 版本不兼容

---

## 问题（hwcomposer 版本不兼容）

### hwcomposer 1.x vs HWC2（2.x）
- **Android 7.1 hwcomposer.mt6755.so**：hwcomposer 1.x（HWC_MODULE_API_VERSION = 1.x）
- **drm-hwcomposer-shim 需要**：HWC2（hwcomposer 2.x，Android 8+）
- **结果**：hwcomposer 1.x 没有 HWC2 API（`hwc2_compat_device_new` 等函数），shim 初始化失败

### libhwc2_compat_layer.so 缺失
- **libhybris libhwc2.so**：dlopen "libhwc2_compat_layer.so"（运行时加载）
- **设备上没有**：libhwc2_compat_layer.so（Android 7.1 hwcomposer 1.x 没有 HWC2 compat layer）
- **结果**：HWC2 subsystem 初始化失败（"library libhwc2_compat_layer.so not found"）

### test_hwc 段错误
```
=== DRM-HWComposer Shim Test ===
Creating HwcDrmDevice...
Initializing hwcomposer via libhybris
Gralloc initialized
Calling hybris_hwc2_initialize()...
library "libhwc2_compat_layer.so" not found
HWC2 subsystem initialized
Creating HWC2 device...
library "libhwc2_compat_layer.so" not found
Segmentation fault
```

---

## 根本原因（hwcomposer 版本）

**Android hwcomposer 版本历史**：
- **hwcomposer 1.x**：Android 4.x - 7.x（HWC_MODULE_API_VERSION = 1.x）
  - API：`hwc_composer_device_1_t`（旧 API）
  - 函数：`prepare()`, `set()`, `eventControl()`, `blank()`, `query()`
- **HWC2（hwcomposer 2.x）**：Android 8.0+（HWC2_FUNCTION_COUNT = 40+）
  - API：`hwc2_device_t`（新 API，函数式）
  - 函数：`hwc2_display_present()`, `hwc2_layer_set_buffer()`, 等
  - 需要 `libhwc2_compat_layer.so`（hwcomposer 1.x → HWC2 兼容层）

**魅蓝 Note5 hwcomposer.mt6755.so**：
- **版本**：hwcomposer 1.x（Android 7.1 Flyme 6）
- **不支持**：HWC2 API（Android 8+）
- **结果**：drm-hwcomposer-shim 需要 HWC2，但 hwcomposer.mt6755.so 只有 1.x API

---

## 真实方案（hwcomposer 1.x vs HWC2）

### 方案 1：**写 hwcomposer 1.x → HWC2 适配层**（复杂，2-4 天）
```
drm-hwcomposer-shim → HWC2 API → 适配层 → hwcomposer 1.x API → hwcomposer.mt6755.so
```
- **实现**：写 hwcomposer 1.x → HWC2 适配层（把 HWC2 函数调用转成 hwcomposer 1.x）
- **时间**：2-4 天
- **风险**：hwcomposer 1.x 和 HWC2 API 差异大（函数式 vs 结构体）

### 方案 2：**升级 Android 8+（hwcomposer 2.x）**（复杂，1-2 周）
```
刷 Android 8+ ROM（hwcomposer 2.x）→ drm-hwcomposer-shim → HWC2 API → hwcomposer 2.x
```
- **实现**：刷 Android 8+ ROM（魅蓝 Note5 有 LineageOS 15.1 = Android 8.1）
- **时间**：1-2 周
- **风险**：魅蓝 Note5 LineageOS 15.1 可能不稳定（官方支持已停止）

### 方案 3：**改用 fbdev 平台**（简单，但 gralloc 失败）
```
应用 → libhybris EGL（fbdev 平台）→ fb0
```
- **实现**：用 eglplatform_fbdev.so（直接渲染到 fb0）
- **时间**：0 小时（已经编译）
- **风险**：gralloc 加载失败（gralloc.mt6755.so 不认 fb0）

### 方案 4：**应用级 GLES2**（已经跑通，最实际）
```
自定义 GLES2 应用 → libhybris libEGL → Mali GPU
```
- **例子**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **时间**：0 小时（已经跑通）
- **限制**：只支持 GLES2 应用（不支持桌面级 GPU 合成）

---

## 我的建议（诚实）

**hwcomposer 版本不兼容是硬伤**（魅蓝 Note5 Android 7.1 = hwcomposer 1.x，drm-hwcomposer-shim 需要 HWC2）。

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **hwcomposer 1.x → HWC2 适配层**（2-4 天，复杂）
3. **升级 Android 8+**（1-2 周，魅蓝 Note5 LineageOS 15.1）

**最实际**：**应用级 GLES2**（已经跑通）

---

## 当前资产（必须保护）

### 已完成（GPU 硬件加速 + FPS benchmark + 渲染到屏幕 + 压力测试 + 应用测试 + gl4es）
- **libhybris 编译**：commit f47e718（TLS patcher）
- **三个关键修复**：patchelf + HYBRIS_TLS_PATCH + glibc libhardware.so.2
- **GPU 验证**：GL_RENDERER Mali-T860 + glReadPixels ff0000ff
- **FPS benchmark**：17654 FPS（glClear + eglSwapBuffers）
- **GPU 渲染显示到 X11 窗口**：60 帧 OK（glReadPixels + XPutImage）
- **GPU 渲染直接到屏幕**：60 帧 OK（glReadPixels + mmap fb0）
- **GPU 性能对比**：12123 FPS（纯 GPU），vs CPU 5-10 FPS（1000-2000 倍加速）
- **3D 立方体**：17.72 FPS（纹理 + 光照 + 深度测试）
- **GPU 压力测试**：17.14 FPS（50 立方体 + 200 粒子 + 光照）
- **gl4es HYBRIS 翻译层**：初始化成功（GL vendor: GL4ES wrapping）

### drm-hwcomposer-shim 测试（编译成功，hwcomposer 1.x 不兼容）
- **drm-hwcomposer-shim 编译**：✅ libdrm_hwcomposer_shim.so（1.1 MB）+ test_hwc
- **HWCNativeWindowSetBufferCount wrapper**：✅ libhwc-wrapper.so（C wrapper）
- **test_hwc 测试**：❌ Segmentation fault（hwcomposer 1.x 没有 HWC2 API）
- **libhwc2_compat_layer.so 缺失**：❌ "library not found"（Android 7.1 没有 HWC2 compat layer）

### 真实应用测试（失败）
- **neverball**：❌ llvmpipe 软件渲染（缺 GLX + OpenGL compatibility profile）
- **teeworlds**：❌ llvmpipe 软件渲染（SDL2 + OpenGL 1.x，缺 GLX）
- **minetest**：❌ 未运行（transitional package，需要 luajit）
- **supertuxkart**：❌ 未安装（需要 libGL + libGLX）
- **extremetuxracer**：❌ 未安装（需要 libGL + libGLX）
- **glmark2-es2-x11**：❌ 失败（需要 GLX，libhybris 没有）

### weston 测试（失败）
- **x11-backend**：❌ segfault（libhybris EGL 不支持 X11）
- **wayland-backend**：❌ 失败（需要父 Wayland compositor）
- **headless-backend**：❌ segfault（libhybris EGL 不兼容）
- **vnc-backend**：❌ segfault（libhybris EGL 不兼容）

### 文档（全部归档）
- **`twrp/docs/gpu-success/`**（GitHub commit 8ba1eb2 + 后续）
  - `test-egl.c` + `test-gles2.c` + `test-window.c` + `test-1frame.c`
  - `bench-gles2.c` + `fps-simple.c` + `fps-30frames.c` + `fps-5frames.c`
  - `gpu-x11-display.c` + `gpu-x11-display-256.c` + `gpu-x11-display-shm.c`
  - `cube3d.c`（3D 立方体）
  - `gpu-stress-test.c`（50 立方体 + 200 粒子）
  - `test-hwc-screen.c` + `test-fbdev-screen.c` + `gpu-vs-cpu.c`
  - `readback-size-bench.c`（尺寸对比）
  - `gpu-x11-screenshot.png` + `desktop-screenshot.png` + `fb0-gpu-render.png`
  - `gpu-x11-live.png` + `computer-vnc-gpu.png` + `gpu-x11-256-live.png`
  - `cube3d-live.png`（3D 立方体截图）
  - `gpu-stress-live.png`（50 立方体 + 200 粒子截图）
  - `neverball-live.png`（neverball 截图，llvmpipe 软件渲染）
  - `teeworlds-live.png`（teeworlds 截图，llvmpipe 软件渲染）
  - `libGL-gl4es-hybris.so.1`（gl4es HYBRIS 翻译层，1.9 MB）
  - `2026-11-03-Mali-T860-GPU硬件加速-完整复现方法.md`（完整命令）
  - `2026-11-03-Mali-T860-GPU硬件加速-跑通实录.md`
  - `2026-11-03-Mali-T860-GPU硬件加速-验证结果.md`
  - `2026-11-03-Mali-T860-GPU硬件加速-FPS-benchmark.md`（17654 FPS）
  - `2026-11-03-Mali-T860-GPU硬件加速-桌面集成下一步.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-搜索结果与下一步.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-drm-hwcomposer-shim-计划.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-现状.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-hwcomposer-crash.md`
  - `2026-11-03-Mali-T860-GPU渲染到屏幕-成功.md`
  - `2026-11-03-Mali-T860-GPU性能对比-证明.md`
  - `2026-11-03-Mali-T860-GPU零拷贝探索.md`
  - `2026-11-03-Mali-T860-GPU压力测试.md`
  - `2026-11-03-Mali-T860-GPU-neverball-测试.md`
  - `2026-11-03-Mali-T860-GPU应用测试总结.md`
  - `2026-11-03-Mali-T860-GPU应用测试完整总结.md`
  - `2026-11-03-Mali-T860-GPU-gl4es-HYBRIS翻译层.md`
  - `2026-11-03-Mali-T860-GPU-Wayland方案技术分析.md`
  - `2026-11-03-Mali-T860-GPU-hwcomposer版本不兼容分析.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，12123 FPS）
- **GPU 渲染 X11 窗口**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **GPU 渲染直接到屏幕**：test-fbdev-screen（60 帧 OK，绕过 Xorg 写 fb0）

---

## 结论（诚实）

**hwcomposer 版本不兼容是硬伤**：
- **魅蓝 Note5 hwcomposer.mt6755.so**：hwcomposer 1.x（Android 7.1）
- **drm-hwcomposer-shim 需要**：HWC2（hwcomposer 2.x，Android 8+）
- **libhwc2_compat_layer.so 缺失**：hwcomposer 1.x 没有 HWC2 compat layer

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **hwcomposer 1.x → HWC2 适配层**（2-4 天，复杂）
3. **升级 Android 8+**（1-2 周，魅蓝 Note5 LineageOS 15.1）

**最实际**：**应用级 GLES2**（已经跑通）

**下一步**：**就此打住**（应用级 GLES2 够用），**或者升级 Android 8+**（1-2 周，风险高）
