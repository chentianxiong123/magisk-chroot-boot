# Mali-T860 GPU hwcomposer HAL 需要 binder service

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：hwcomposer HAL 初始化需要 binder service（TWRP recovery 没有），hwcomposer 1.x 挂起

---

## 问题（hwcomposer HAL 需要 binder service）

### strace 证据
```
faccessat("/vendor/lib64/hw/hwcomposer.mt6755.so", R_OK) = 0
openat("/vendor/lib64/hw/hwcomposer.mt6755.so", O_RDONLY) = 3
openat("/system/lib64/libbinder.so", O_RDONLY) = 13
openat("/dev/binder", O_RDWR) = 9
ioctl(9, BINDER_WRITE_READ, ...) = 0
nanosleep({tv_sec=1, tv_nsec=0}, ...) = 0
ioctl(9, BINDER_WRITE_READ, ...) = 0
nanosleep({tv_sec=1, tv_nsec=0}, ...) = 0
... （无限循环，等待 binder service 响应）
```

**分析**：
- **hwcomposer.mt6755.so 加载成功**（openat = 3）
- **加载 libbinder.so**（openat = 13）
- **打开 /dev/binder**（openat = 9）
- **ioctl BINDER_WRITE_READ**（hwcomposer HAL 初始化，等待 binder service）
- **nanosleep 1 秒**（等待）
- **无限循环**（binder service 不响应，hwcomposer HAL 挂起）

### TWRP recovery 没有 hwcomposer service
```
ps | grep hwcomposer → 空（没有 hwcomposer service）
ps | grep surfaceflinger → 空（没有 surfaceflinger service）
```

**结论**：
- **hwcomposer HAL 初始化需要 binder service**（hwcomposer service 或 surfaceflinger service）
- **TWRP recovery 没有 Android service**（只有 init + adbd + busybox）
- **hwcomposer HAL 挂起**（等待 binder service，永远不响应）

---

## 根本原因（hwcomposer HAL 设计）

**hwcomposer HAL 架构**：
- **hwcomposer HAL**：内核驱动接口（hwcomposer.mt6755.so）
- **binder service**：Android IPC 服务（hwcomposer service / surfaceflinger service）
- **SurfaceFlinger**：Android 窗口合成器（调用 hwcomposer HAL）

**hwcomposer HAL 初始化流程**：
```
hw_get_module("hwcomposer") → hwcomposer.mt6755.so 加载
    → open() → hwcomposer HAL 初始化
        → 需要 binder service（hwcomposer service）
            → ioctl BINDER_WRITE_READ（等待 hwcomposer service 响应）
            → TWRP recovery 没有 hwcomposer service → 挂起 ❌
```

**TWRP recovery 限制**：
- **没有 Android service**（hwcomposer service / surfaceflinger service）
- **只有 init + adbd + busybox**（最小化环境）
- **hwcomposer HAL 无法初始化**（binder service 不响应）

---

## 真实方案（hwcomposer HAL 需要 binder service）

### 方案 1：**启动 hwcomposer service**（复杂，TWRP recovery 不支持）
```
TWRP recovery → 启动 hwcomposer service → hwcomposer HAL 初始化成功
```
- **实现**：在 TWRP recovery 里启动 hwcomposer service（需要 Android init + binder service manager）
- **时间**：1-2 天
- **风险**：TWRP recovery 不支持 Android service（没有 Android init + binder service manager）

### 方案 2：**启动完整 Android**（刷 Halium ROM，用户拒绝）
```
启动完整 Android → hwcomposer service 运行 → hwcomposer HAL 初始化成功
```
- **实现**：刷 Halium ROM（完整 Android 用户空间）
- **时间**：1-2 周
- **风险**：用户拒绝（必须在 TWRP recovery + chroot 里跑）

### 方案 3：**应用级 GLES2**（已经跑通，最实际）
```
自定义 GLES2 应用 → libhybris libEGL（离屏渲染）→ CPU 读回 → X11 窗口 / fb0
```
- **例子**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **时间**：0 小时（已经跑通）
- **限制**：只支持 GLES2 应用（不支持桌面级 GPU 合成）

---

## 我的建议（诚实）

**hwcomposer HAL 需要 binder service**（hwcomposer service / surfaceflinger service），**TWRP recovery 没有 Android service**（hwcomposer HAL 挂起）。

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **启动完整 Android**（刷 Halium ROM，用户拒绝）
3. **TWRP recovery 启动 hwcomposer service**（TWRP 不支持，复杂）

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

### hwcomposer HAL 测试（挂起，需要 binder service）
- **hwcomposer.mt6755.so 加载**：✅（openat = 3）
- **libbinder.so 加载**：✅（openat = 13）
- **/dev/binder 打开**：✅（openat = 9）
- **ioctl BINDER_WRITE_READ**：❌ 挂起（等待 hwcomposer service，TWRP 没有）
- **TWRP recovery 没有 Android service**：❌ hwcomposer HAL 挂起

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
- **`twrp/docs/gpu-success/`**（GitHub commit 26aedcd + 后续）
  - `test-egl.c` + `test-gles2.c` + `test-window.c` + `test-1frame.c`
  - `bench-gles2.c` + `fps-simple.c` + `fps-30frames.c` + `fps-5frames.c`
  - `gpu-x11-display.c` + `gpu-x11-display-256.c` + `gpu-x11-display-shm.c`
  - `cube3d.c`（3D 立方体）
  - `gpu-stress-test.c`（50 立方体 + 200 粒子）
  - `test-hwc-screen.c` + `test-fbdev-screen.c` + `gpu-vs-cpu.c` + `test-hwc1.c`
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
  - `2026-11-03-Mali-T860-GPU-hwcomposer版本不兼容分析.md`
  - `2026-11-03-Mali-T860-GPU-hwcomposer-HAL需要binder-service.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，12123 FPS）
- **GPU 渲染 X11 窗口**：cube3d（17.72 FPS）+ gpu-stress-test（17.14 FPS）
- **GPU 渲染直接到屏幕**：test-fbdev-screen（60 帧 OK，绕过 Xorg 写 fb0）

---

## 结论（诚实）

**hwcomposer HAL 需要 binder service**（hwcomposer service / surfaceflinger service），**TWRP recovery 没有 Android service**（hwcomposer HAL 挂起）。

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **启动完整 Android**（刷 Halium ROM，用户拒绝）
3. **TWRP recovery 启动 hwcomposer service**（TWRP 不支持，复杂）

**最实际**：**应用级 GLES2**（已经跑通）

**下一步**：**就此打住**（应用级 GLES2 够用），**或者升级 Android 8+**（1-2 周，风险高）
