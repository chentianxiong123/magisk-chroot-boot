# Mali-T860 GPU - neverball 测试

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：neverball 用 llvmpipe 软件渲染（libhybris 缺 GLX，无法 GPU 加速）

---

## 测试结果（neverball 3D 滚球游戏）

### neverball 运行（X11 + llvmpipe）
```
Creating a window (800x600, windowed)
Created a window (1, 800x600, windowed)
GL vendor: Mesa
GL renderer: llvmpipe (LLVM 19.1.7, 128 bits)
GL version: 4.5 (Compatibility Profile) Mesa 25.0.7-2+deb13u1
```

**问题**：neverball 用 llvmpipe 软件渲染（不是 Mali-T860 GPU）

### neverball ldd（链接库）
```
libSDL2-2.0.so.0 => /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0
libGL.so.1 => /usr/lib/aarch64-linux-gnu/libGL.so.1
libGLdispatch.so.0 => /usr/lib/aarch64-linux-gnu/libGLdispatch.so.0
libGLX.so.0 => /usr/lib/aarch64-linux-gnu/libGLX.so.0
```

**原因**：neverball 链接 Mesa libGL.so.1 + libGLX.so.0（需要 GLX，libhybris 没有）

---

## 问题（libhybris 缺 GLX）

**libhybris 只有 EGL + GLES2**：
- **EGL**：✅（libhybris libEGL.so.1）
- **GLES2**：✅（libhybris libGLESv2.so.2）
- **GLX**：❌（libhybris 没有 libGLX.so.0）
- **OpenGL compatibility profile**：❌（libhybris 没有 libGL.so.1）

**neverball 需要**：
- **OpenGL compatibility profile**（不是 GLES2）
- **GLX**（X11 + OpenGL 集成）
- **Mesa libGL.so.1 + libGLX.so.0**

**结论**：neverball 无法用 libhybris GPU 加速（缺 GLX + OpenGL compatibility profile）

---

## 真实方案（neverball GPU 加速）

### 方案 1：**OpenGL-to-GLES2 shim**（复杂，1-2 天）
```
neverball → libGL.so.1 shim → GLES2 → libhybris libGLESv2 → Mali GPU
```
- **实现**：写 libGL.so.1 shim（把 OpenGL 1.x/2.x 调用转成 GLES2）
- **时间**：1-2 天
- **风险**：OpenGL compatibility profile 和 GLES2 有差异（glBegin/glEnd 不支持）

### 方案 2：**Mesa GLES2 driver + libhybris**（复杂，2-4 天）
```
neverball → Mesa libGL → libhybris GLES2 backend → Mali GPU
```
- **实现**：改 Mesa 源代码（加 libhybris GLES2 backend）
- **时间**：2-4 天
- **风险**：Mesa 内部 ABI 不稳定（Panfork 失败的经验）

### 方案 3：**应用级 GPU 加速**（已经跑通，最实际）
```
自定义 GLES2 应用 → libhybris libEGL → Mali GPU
```
- **例子**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **时间**：0 小时（已经跑通）
- **限制**：只支持 GLES2 应用（不支持 OpenGL compatibility profile）

---

## 我的建议（诚实）

**neverball 无法用 libhybris GPU 加速**（缺 GLX + OpenGL compatibility profile）。

**应用级 GPU 加速已经跑通**：
- **cube3d**：3D 立方体 + 纹理 + 光照，17.72 FPS
- **gpu-stress-test**：50 立方体 + 200 粒子，17.14 FPS
- **gpu-x11-display**：256x256 动画，17.81 FPS

**桌面集成需要 drm-hwcomposer-shim**：
- **时间**：4-8 小时（编译 Rust + 配置）
- **风险**：魅蓝 Note5 可能不兼容（drm-hwcomposer-shim 只测了 Pixel 3a）

---

## 当前资产（必须保护）

### 已完成（GPU 硬件加速 + FPS benchmark + 渲染到屏幕 + 压力测试）
- **libhybris 编译**：commit f47e718（TLS patcher）
- **三个关键修复**：patchelf + HYBRIS_TLS_PATCH + glibc libhardware.so.2
- **GPU 验证**：GL_RENDERER Mali-T860 + glReadPixels ff0000ff
- **FPS benchmark**：17654 FPS（glClear + eglSwapBuffers）
- **GPU 渲染显示到 X11 窗口**：60 帧 OK（glReadPixels + XPutImage）
- **GPU 渲染直接到屏幕**：60 帧 OK（glReadPixels + mmap fb0）
- **GPU 性能对比**：12123 FPS（纯 GPU），vs CPU 5-10 FPS（1000-2000 倍加速）
- **3D 立方体**：17.72 FPS（纹理 + 光照 + 深度测试）
- **GPU 压力测试**：17.14 FPS（50 立方体 + 200 粒子 + 光照）
- **应用级 GPU 加速可用**（应用直接用 libhybris libEGL）

### neverball 测试（失败）
- **neverball**：❌ llvmpipe 软件渲染（缺 GLX + OpenGL compatibility profile）
- **原因**：neverball 链接 Mesa libGL.so.1 + libGLX.so.0，libhybris 没有

### 文档（全部归档）
- **`twrp/docs/gpu-success/`**（GitHub commit 448dfaf + 后续）
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
  - `2026-11-03-Mali-T860-GPU-neverball-测试.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，12123 FPS）
- **GPU 渲染 X11 窗口**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **GPU 渲染直接到屏幕**：test-fbdev-screen（60 帧 OK，绕过 Xorg 写 fb0）

---

## 结论（诚实）

**neverball 无法用 libhybris GPU 加速**（缺 GLX + OpenGL compatibility profile）。

**应用级 GPU 加速已经跑通**：
- **cube3d**：3D 立方体 + 纹理 + 光照，17.72 FPS
- **gpu-stress-test**：50 立方体 + 200 粒子，17.14 FPS
- **gpu-x11-display**：256x256 动画，17.81 FPS

**桌面集成需要 drm-hwcomposer-shim**（4-8 小时，魅蓝 Note5 可能不兼容）。

**下一步**：**OpenGL-to-GLES2 shim**（1-2 天，让 neverball 用 GPU），**或者桌面集成**（drm-hwcomposer-shim，4-8 小时）。
