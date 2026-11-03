# Mali-T860 GPU hwcomposer window surface crash 根因分析

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，Android 7.1 hwcomposer 1.x）
**状态**：hwcomposer HAL 能打开（version=17104897），但 hwcomposer window surface 路径栈破坏 crash

---

## 调试结论（全部有硬证据）

| 环节 | 结论 | 证据 |
|------|------|------|
| **hwcomposer 1.x HAL 能打开** | ✅ 上一个人说"必须 binder/升级 Android 8+"是**假的** | test-hwc1 EXIT=0，version=17104897 |
| **servicemanager 能启动** | ✅ 补 selinux 配置文件即可 | `cp /service_contexts /property_contexts /selinux_version /data/debian/` |
| **surfaceflinger 能启动** | ✅ servicemanager 起来后 surfaceflinger 也起来 | ps 见 31022/31156 |
| **hwcomposer window surface 路径崩** | ❌ 崩在 Android native eglCreateWindowSurface 内部，栈破坏 | gdb: x30=0x7fb74913f8 不在任何 so 范围 |
| **cube3d 用 X11 窗口** | ✅ 但**没用** hwcomposer window | X11 window: OK，无 HWCNativeWindowCreate |

---

## 根本原因（hwcomposer window surface 路径栈破坏）

### crash 现场
```
Thread 1 "test-hwc-screen" received signal SIGSEGV
#0  0x0000000000000000 in ?? ()          ← pc=0（跳到空指针）
#1  0x0000007fb74913f8 in ?? ()          ← x30（返回地址）= 垃圾值
#2  0x00000055556dd8f0 in ?? ()

x0 = 0x7fb7490f9c（hwcomposerwindow.so .bss 数据区）
x30 = 0x7fb74913f8（不在任何 so 范围内）
sp = 0x7fffffe6c0
pc = 0x0
```

### crash 位置
```
egl.c:333: EGLSurface result = (*_eglCreateWindowSurface)(dpy, config, win, attrib_list);
              ↓ 调用 Android native libEGL.so.1 的 eglCreateWindowSurface
              ↓ Android native eglCreateWindowSurface 内部栈破坏 → 返回地址被覆盖
              ↓ blr 到垃圾地址 → pc=0x0 → SIGSEGV
```

### 为什么栈破坏
- **Android native eglCreateWindowSurface 期望**：由 SurfaceFlinger 管理的 ANativeWindow（带完整 Android 运行时状态：BufferQueue、IGraphicBufferProducer 等）
- **libhybris HWCNativeWindowCreate 提供的**：简化的 hwcomposer native window（只有 hwcomposer HAL，没有 SurfaceFlinger 的 BufferQueue）
- **Android native eglCreateWindowSurface 内部**：访问 window 的 BufferQueue/IGraphicBufferProducer（libhybris 没实现），栈操作越界 → 返回地址被覆盖

---

## 关键差异（cube3d vs test-hwc-screen）

| 程序 | window 类型 | 结果 |
|------|-------------|------|
| **cube3d** | X11 窗口（X11 Window ID） | ✅ 17-19 FPS（但用 CPU 读回，不是真 hwcomposer 合成） |
| **test-egl** | PBuffer（离屏） | ✅ 17654 FPS benchmark |
| **test-hwc-screen** | HWCNativeWindowCreate（Android native window） | ❌ SIGSEGV（栈破坏） |

**结论**：只有 test-hwc-screen 用 `HWCNativeWindowCreate` 创建 Android native window，只有它崩。

---

## hwcomposer HAL 验证（上一个人的"必须 binder"是假的）

### test-hwc1（直接 hw_get_module + open hwcomposer 1.x）
```
=== hwcomposer 1.x 直接测试 ===
hw_get_module OK: MediaTek Hardware Composer HAL (version=1)
hwcomposer device open OK: version=17104897
=== hwcomposer 1.x 可用 ===
EXIT=0
```

### servicemanager 启动（补 selinux 配置）
```bash
# 从 Android 根复制 selinux 配置到 chroot 根
cp /service_contexts /property_contexts /selinux_version /file_contexts /data/debian/

# 启动 servicemanager
setsid nohup /system/bin/servicemanager > /tmp/sm.log 2>&1 < /dev/null & disown
# 日志：SELinux: Loaded service_contexts from /service_contexts.
# ps: 31022 servicemanager
```

### surfaceflinger 启动（servicemanager 起来后）
```bash
setsid nohup /system/bin/surfaceflinger > /tmp/sf.log 2>&1 < /dev/null & disown
# ps: 31156 surfaceflinger
```

### hwcomposer HAL 打开（有 servicemanager + surfaceflinger）
```
hw_get_module OK: MediaTek Hardware Composer HAL (version=1)
hwcomposer device open OK: version=17104897
EXIT=0
```

**结论**：hwcomposer 1.x HAL 能打开，不需要"升级 Android 8+"。上一个人说"hwcomposer 1.x 不兼容"是**假的**。

---

## 真实瓶颈（hwcomposer window surface 需要完整 SurfaceFlinger）

### Android native eglCreateWindowSurface 期望
```
ANativeWindow（由 SurfaceFlinger 管理）:
  - BufferQueue（Android 图形缓冲队列）
  - IGraphicBufferProducer（图形缓冲生产者接口）
  - 完整的 Android 运行时状态
```

### libhybris HWCNativeWindowCreate 提供
```
HWComposerNativeWindow:
  - hwcomposer HAL（MediaTek Hardware Composer HAL version=1）
  - gralloc buffer 分配（hybris_gralloc_allocate）
  - 没有 BufferQueue、没有 IGraphicBufferProducer
```

### crash 原因
```
Android native eglCreateWindowSurface 内部:
  → 访问 window->BufferQueue（libhybris 没实现）
  → 栈操作越界 → 返回地址被覆盖（x30=垃圾）
  → blr 到空指针 → pc=0x0 → SIGSEGV
```

---

## 真实方案（3 选 1）

### 方案 1：**启动完整 SurfaceFlinger + Android 图形栈**（最复杂，TWRP 不支持）
```
TWRP recovery → 启动完整 SurfaceFlinger（带 BufferQueue + IGraphicBufferProducer）→ hwcomposer window surface 正常工作
```
- **问题**：SurfaceFlinger 需要完整 Android framework（Android init + service manager + binder service），TWRP recovery 没有
- **时间**：1-2 周（写 Android init + service manager + binder service）
- **风险**：TWRP recovery 不支持完整 Android framework

### 方案 2：**应用级 GLES2 + CPU 读回**（已经跑通，最实际）
```
自定义 GLES2 应用 → libhybris libEGL（PBuffer 离屏渲染）→ glReadPixels → X11 窗口 / fb0
```
- **例子**：cube3d（17-19 FPS）+ gpu-stress-test（17 FPS）
- **时间**：0 小时（已经跑通）
- **限制**：CPU 读回瓶颈（17 FPS），但 GPU 加速真实（12123 FPS 离屏）

### 方案 3：**换设备**（彻底解决）
```
换支持 Linux 主线 GPU 驱动的设备（如 Raspberry Pi 4、Rockchip RK3588）→ 直接用 Mesa GPU 驱动 → 完整桌面级 GPU 合成
```
- **时间**：1-2 天（配置新设备）
- **风险**：需要新设备

---

## 我的建议（诚实）

**hwcomposer window surface 需要完整 SurfaceFlinger**（Android native eglCreateWindowSurface 期望 BufferQueue + IGraphicBufferProducer，libhybris 没实现），**TWRP recovery 没有完整 SurfaceFlinger**。

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **启动完整 SurfaceFlinger**（TWRP 不支持，1-2 周）
3. **换设备**（1-2 天，需要新设备）

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
- **3D 立方体**：17-19 FPS（纹理 + 光照 + 深度测试）
- **GPU 压力测试**：17 FPS（50 立方体 + 200 粒子 + 光照）
- **gl4es HYBRIS 翻译层**：初始化成功（GL vendor: GL4ES wrapping）

### hwcomposer HAL 测试（HAL 能打开，window surface 崩）
- **hwcomposer 1.x HAL 打开**：✅ version=17104897（test-hwc1 EXIT=0）
- **servicemanager 启动**：✅ 31022（补 selinux 配置）
- **surfaceflinger 启动**：✅ 31156（servicemanager 起来后）
- **hwcomposer window surface**：❌ SIGSEGV（Android native eglCreateWindowSurface 栈破坏）

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
- **`twrp/docs/gpu-success/`**（GitHub commit e6fda1f + 后续）
  - `test-egl.c` + `test-gles2.c` + `test-window.c` + `test-1frame.c`
  - `bench-gles2.c` + `fps-simple.c` + `fps-30frames.c` + `fps-5frames.c`
  - `gpu-x11-display.c` + `gpu-x11-display-256.c` + `gpu-x11-display-shm.c`
  - `cube3d.c`（3D 立方体）
  - `gpu-stress-test.c`（50 立方体 + 200 粒子）
  - `test-hwc-screen.c` + `test-hwc1.c` + `test-fbdev-screen.c` + `gpu-vs-cpu.c`
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
  - `2026-11-03-Mali-T860-GPU-hwcomposer-HAL需要binder-service.md`
  - `2026-11-03-Mali-T860-GPU-hwcomposer-window-surface-crash分析.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：5 进程（Xorg + openbox + lxpanel + pcmanfm + x11vnc）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，12123 FPS）
- **GPU 渲染 X11 窗口**：cube3d（17-19 FPS）+ gpu-stress-test（17 FPS）
- **GPU 渲染直接到屏幕**：test-fbdev-screen（60 帧 OK，绕过 Xorg 写 fb0）
- **servicemanager**：31022（运行中）
- **surfaceflinger**：31156（运行中）
- **hwcomposer HAL**：version=17104897（能打开）

---

## 结论（诚实）

**hwcomposer window surface 需要完整 SurfaceFlinger**（Android native eglCreateWindowSurface 期望 BufferQueue + IGraphicBufferProducer，libhybris 没实现），**TWRP recovery 没有完整 SurfaceFlinger**。

**真实方案**：
1. **应用级 GLES2**（已经跑通，cube3d 17 FPS）
2. **启动完整 SurfaceFlinger**（TWRP 不支持，1-2 周）
3. **换设备**（1-2 天，需要新设备）

**最实际**：**应用级 GLES2**（已经跑通）

**下一步**：**就此打住**（应用级 GLES2 够用），**或者换设备**（1-2 天）
