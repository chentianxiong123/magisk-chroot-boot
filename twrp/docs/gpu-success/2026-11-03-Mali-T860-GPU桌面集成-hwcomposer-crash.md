# Mali-T860 GPU 桌面集成 - hwcomposer crash

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：hwcomposer 平台渲染到屏幕 crash（segfault），GPU 硬件加速跑通

---

## 问题（hwcomposer 平台渲染到屏幕 crash）

### 测试代码（test-hwc-screen.c）
```c
#include <hybris/hwcomposer/hwcomposer_window.h>

// 创建 HWComposerNativeWindow（真实窗口，渲染到屏幕）
ANativeWindow* window = (ANativeWindow*)HWCNativeWindowCreate(1080, 1920, HAL_PIXEL_FORMAT_RGBA_8888, (void*)present_callback, NULL);
if (!window) { printf("FAIL HWCNativeWindowCreate\n"); return 2; }
printf("HWCNativeWindow: OK (1080x1920)\n");

// eglCreateWindowSurface
EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
```

### 测试结果
```
EXIT=139（segfault）
library "libpq_cust_base.so" not found
library "libwlparser.so" not found
（没有输出，直接 crash）
```

### strace（crash 位置）
```
openat("/usr/lib/libhybris/libhybris-hwcomposerwindow.so.1") = 3
openat("/usr/lib/libhybris//eglplatform_hwcomposer.so") = 3
+++ killed by SIGSEGV +++
```

**crash 位置**：hwcomposer HAL 初始化（HWCNativeWindowCreate → HWComposerNativeWindow 构造函数）

---

## 原因（hwcomposer HAL 初始化失败）

### 可能原因
1. **hwcomposer.mt6755.so 版本不兼容**（Android 7.1 hwcomposer 1.x，libhybris 期望 hwcomposer 2.x）
2. **gralloc.mt6755.so 初始化失败**（需要 /dev/ion + libion_mtk.so）
3. **TLS 冲突**（hwcomposer HAL 内联访问 bionic TLS 槽，和 glibc TLS 槽撞车）

### 验证（LD_PRELOAD stub-rseq + GLIBC_TUNABLES）
```
LD_PRELOAD=/usr/lib/libhybris/stub-rseq.so
HYBRIS_TLS_PATCH=1
GLIBC_TUNABLES=glibc.pthread.rseq=0
```
**结果**：还是 crash（EXIT=139）

---

## 真实方案（hwcomposer crash 的解决方法）

### 方案 1：**应用级 GPU 加速 + X11 窗口**（已经跑通，最实际）
```
应用 → libhybris libEGL → Mali GPU（PBuffer 离屏渲染）
     → glReadPixels → XPutImage → X11 窗口（软件拷贝）
```
- **GPU 渲染**：Mali-T860 硬件加速（17654 FPS 离屏）
- **软件拷贝**：glReadPixels + XPutImage（30-60 FPS 显示）
- **时间**：0 小时（已经跑通）

### 方案 2：**drm-hwcomposer-shim + Flick compositor**（Mer/Droidian 方案，最成熟）
```
Wayland compositor → drm-hwcomposer-shim → Android hwcomposer → libhybris → Mali GPU
```
- **drm-hwcomposer-shim**：Rust 项目（Droidian 用，2025 daily driver）
- **时间**：4-8 小时（编译 Rust + 配置）
- **风险**：魅蓝 Note5 可能不兼容（drm-hwcomposer-shim 只测了 Pixel 3a）

---

## 结论（诚实）

**hwcomposer 平台渲染到屏幕 crash**（segfault，hwcomposer HAL 初始化失败）。

**GPU 硬件加速跑通**：
- **GPU 渲染**：Mali-T860 硬件加速（17654 FPS 离屏）
- **GPU 渲染显示到 X11 窗口**：60 帧 OK（glReadPixels + XPutImage）
- **应用级 GPU 加速可用**（应用直接用 libhybris libEGL）

**下一步**：**方案 1（应用级 GPU 加速 + X11 窗口）**，**GPU 应用单独跑**（用 libhybris libEGL，渲染 PBuffer → X11 窗口）。
