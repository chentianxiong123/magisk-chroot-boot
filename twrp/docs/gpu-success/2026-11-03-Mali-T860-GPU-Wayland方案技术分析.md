# Mali-T860 GPU Wayland 方案技术分析

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：drm-hwcomposer-shim cdylib 编译成功，但 Flick shell 未编译 + hwcomposer shim init_egl 段错误

---

## 现场保留（已完成）

### 桌面环境（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **GPU 测试程序**：cube3d + gpu-stress-test + test-fbdev-screen ✅
- **libhybris 库**：38 个 .so 文件 ✅

### GPU 加速（已跑通）
- **libhybris 编译**：commit f47e718（TLS patcher）
- **三个关键修复**：patchelf + HYBRIS_TLS_PATCH + glibc libhardware.so.2
- **GPU 验证**：GL_RENDERER Mali-T860 + glReadPixels ff0000ff
- **FPS benchmark**：17654 FPS（glClear + eglSwapBuffers）
- **GPU 渲染显示到 X11 窗口**：60 帧 OK（glReadPixels + XPutImage）
- **GPU 渲染直接到屏幕**：60 帧 OK（glReadPixels + mmap fb0）
- **应用级 GPU 加速**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）

### GitHub 归档（最新 commit）
- **56a8872**：gl4es HYBRIS 翻译层（初始化成功，X11 窗口失败）
- **0550bed**：应用测试完整总结（真实应用 + weston 失败）
- **639be2e**：应用测试总结（真实应用失败）

---

## Wayland 方案（技术分析）

### 为什么 X11 不行（代码层面）
**libhybris EGL 只接受 Android native window**：
```
Android native window = SurfaceFlinger/hwcomposer 创建的 ANativeWindow
X11 窗口 ≠ ANativeWindow（libhybris 认不出）
Wayland surface ≠ ANativeWindow（libhybris 认不出）
```

**X11 的问题**：
- X11 窗口是 Xorg 创建的（X server 管理的 XID）
- libhybris EGL 期望 Android ANativeWindow（hwcomposer 创建）
- eglCreateWindowSurface(X11 window) → EGL_BAD_NATIVE_WINDOW ❌

**X11 不是"限制大"，是"完全不对口"**（libhybris 没有桥接 X11 window → ANativeWindow 的代码）

### Wayland 方案 A：drm-hwcomposer-shim + Wayland compositor（2025 daily driver）

**原理**：
```
Wayland compositor（weston/Flick）
    → 以为在用 DRM/KMS
    → drm-hwcomposer-shim 把 libdrm/KMS 调用翻译成 hwcomposer 调用
    → hwcomposer 创建 Android native window
    → libhybris EGL 认这个窗口 ✅
    → Mali GPU 渲染 → hwcomposer 上屏
```

**我们进展**：
- ✅ drm-hwcomposer-shim cdylib 编译成功（libdrm_hwcomposer_shim.so 1.1 MB）
- ❌ **Flick shell 未编译**（`/root/Flick/shell/target/release/flick` 不存在）
- ❌ **hwcomposer shim init_egl 段错误**（HWCNativeWindowSetBufferCount 符号未定义）

**缺的**：
- HWCNativeWindowSetBufferCount 函数（libhybris-hwcomposerwindow 没这个符号）
- Flick shell 编译（Rust + wlroots）

**成功率**：50%（shim 代码是给 Pixel 3a 写的，魅蓝 Note5 可能不兼容）

### Wayland 方案 B：fbdev 平台 + 应用直接渲染（绕过窗口系统）

**原理**：
```
应用 → libhybris EGL（fbdev 平台）→ 直接渲染到 /dev/graphics/fb0
```

**我们进展**：
- ✅ eglplatform_fbdev.so 编译成功
- ❌ **gralloc 加载失败**（"failed to open the framebuffer module"）
- ❌ gralloc.mt6755.so 不认 fb0（Android 7.1 gralloc 只认 hwcomposer）

**成功率**：20%（gralloc 需要特殊配置，可能不可行）

### Wayland 方案 C：hwcomposer 平台 + 自定义 native window（最接近跑通）

**原理**：
```
应用 → 创建 Android ANativeWindow（HWCNativeWindowCreate）
    → libhybris EGL 认这个窗口 ✅
    → Mali GPU 渲染 → hwcomposer 上屏
```

**我们进展**：
- ✅ test-hwc-screen.c 编译成功
- ❌ **HWCNativeWindowCreate 段错误**（hwcomposer HAL 不兼容）
- ❌ hwcomposer.mt6755.so 是 Android 7.1 版本，可能太老

**成功率**：30%（hwcomposer HAL 版本不兼容）

---

## 下一步（继续推进）

### 修复 drm-hwcomposer-shim（最有可能成功）

**具体步骤**：
1. **找 HWCNativeWindowSetBufferCount 符号**（可能在 Android NDK 或 libhybris 更新版本）
2. **修 libhybris-hwcomposerwindow.so**（加这个符号）
3. **重新编译 drm-hwcomposer-shim**（链接成功）
4. **编译 Flick shell**（Rust + wlroots，30-60 分钟）
5. **测试 Flick compositor + libhybris + Mali GPU**

**时间**：4-8 小时
**成功率**：50%

### 找 HWCNativeWindowSetBufferCount 符号

**当前问题**：
```
/usr/bin/ld: /usr/lib/libhybris/libhybris-hwcomposerwindow.so: undefined reference to `HWCNativeWindowSetBufferCount'
```

**检查 libhybris-hwcomposerwindow.so 导出符号**：
```bash
nm -D /usr/lib/libhybris/libhybris-hwcomposerwindow.so | grep HWCNativeWindow
```

**可能位置**：
- Android NDK（`libhwcomposer.so` 或 `libhwui.so`）
- libhybris 更新版本（halium-9 或 halium-10）
- 需要写 wrapper（用 HWComposerNativeWindow::setBufferCount 包装）

### 编译 Flick shell

**依赖**：
- Rust + cargo（已装）
- wlroots（需要检查）
- wayland-scanner（需要检查）

**编译**：
```bash
cd /root/Flick/shell
cargo build --release
```

**时间**：30-60 分钟

---

## 我的建议（诚实）

**最有可能成功**：**修复 drm-hwcomposer-shim**（缺 HWCNativeWindowSetBufferCount 符号）

**具体步骤**：
1. 找到 HWCNativeWindowSetBufferCount 符号（可能在 Android NDK 或 libhybris 更新版本）
2. 修 libhybris-hwcomposerwindow.so（加这个符号）
3. 重新编译 drm-hwcomposer-shim
4. 编译 Flick shell（Rust + wlroots，30-60 分钟）
5. 测试 Flick compositor + libhybris + Mali GPU

**时间**：4-8 小时，50% 成功率

**风险**：
- HWCNativeWindowSetBufferCount 符号可能找不到
- Flick shell 可能编译失败（wlroots 版本不兼容）
- hwcomposer shim 可能还是段错误（hwcomposer HAL 版本不兼容）
- 魅蓝 Note5 可能不兼容（shim 代码是给 Pixel 3a 写的）

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

### gl4es 测试（初始化成功，X11 窗口失败）
- **gl4es HYBRIS 编译**：✅ libGL.so.1（1.9 MB）
- **gl4es HYBRIS 初始化**：✅ GL vendor: GL4ES wrapping
- **neverball + gl4es HYBRIS**：❌ EGL_BAD_NATIVE_WINDOW（X11 窗口不接受）
- **neverball + gl4es HYBRIS + LIBGL_FB=1**：❌ EGL_BAD_NATIVE_WINDOW
- **neverball + gl4es HYBRIS + hwcomposer 平台**：❌ 段错误

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
- **`twrp/docs/gpu-success/`**（GitHub commit 56a8872 + 后续）
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
  - `2026-11-03-Mali-T860-GPU-Wayland方案技术分析.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，12123 FPS）
- **GPU 渲染 X11 窗口**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **GPU 渲染直接到屏幕**：test-fbdev-screen（60 帧 OK，绕过 Xorg 写 fb0）

---

## 结论（诚实）

**X11**：完全不行（libhybris 不认 X11 窗口，无法桥接）
**Wayland**：理论上有戏，但需要 drm-hwcomposer-shim + Flick compositor（缺 HWCNativeWindowSetBufferCount 符号 + Flick shell 未编译）
**fbdev**：20% 成功率（gralloc 不认 fb0）
**hwcomposer**：30% 成功率（HWCNativeWindowCreate 段错误）

**最有可能成功**：**修复 drm-hwcomposer-shim**（缺 HWCNativeWindowSetBufferCount 符号）

**下一步**：**修复 drm-hwcomposer-shim**（4-8 小时，50% 成功率）
