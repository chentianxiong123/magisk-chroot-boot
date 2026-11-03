# Android GPU 驱动对接 Linux 桌面 - 最终探索记录

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，mali_kbase r12p1）
**状态**：探索完成（Panfork + libhybris 都试过，都跑不通，回到软件渲染）

---

## 结论（诚实）

**Mali-T860 GPU 在 chroot Debian 桌面上跑不通硬件加速**，两个方案都试过：

1. **Panfork（开源 Mesa Panfrost）**：编译成功，但替换系统库桌面崩（ABI 不匹配 23.0.0 vs 25.0.7）
2. **libhybris（桥接 Android blob）**：编译成功，但 test-egl 跑到 bionic linker 加载 libGLES_mali.so 时 segfault（chroot 里 Android 用户空间没有 hybris 补丁）

**回到软件渲染（llvmpipe）稳定**（桌面 + VNC + OpenGL 应用能用，但慢）。

---

## 尝试 1：Panfork（开源 Mesa Panfrost）

### 编译成功
- **源代码**：SolDev69/panfrost-gallium-mesa（支持 Midgard r12p1 + kbase ioctl）
- **时间**：1 分 47 秒（12 核并行，内存盘）
- **产物**：panfrost_dri.so（82MB）+ libGL.so + libEGL.so + libglapi.so

### 替换失败
| 测试 | 结果 | 原因 |
|---|---|---|
| 只替换 panfrost_dri.so | ❌ 桌面崩 | ABI 不匹配（23.0.0 vs 25.0.7）|
| 替换全套库（libGL + libEGL + libglapi + panfrost_dri.so）| ❌ 加载失败 | Panfork 找不到匹配的 swrast_dri.so（23.0.0 版，系统只有 25.0.7）|
| 回滚系统库 | ✅ 稳定 | llvmpipe 软件渲染 |

### 核心问题
**Mesa 内部 ABI 不稳定**，所有组件（libGL/libEGL/libglapi/panfrost_dri.so）必须来自同一版本同一编译，**不能混搭**。完整替换 Mesa 栈风险高（桌面崩过一次）。

---

## 尝试 2：libhybris（桥接 Android blob）

### 编译成功
- **源代码**：libhybris/libhybris（GitHub）
- **Android headers**：Halium/android-headers（halium-7.1 分支）
- **配置**：
  ```bash
  ./autogen.sh --host=aarch64-linux-gnu \
      --with-android-headers=/dev/shm/libhybris-build/android-headers \
      --enable-arch=arm64 --enable-trace --disable-glvnd --prefix=/usr
  make -j12
  ```
- **产物**：libEGL.so（211KB）+ libGLESv2.so（376KB）+ libGLESv1_CM.so（301KB）+ libhybris-common.so（811KB）+ eglplatform_null.so + eglplatform_fbdev.so + libgralloc.so + libhybris-eglplatformcommon.so

### gralloc.c 补丁（halium-7.1 headers 缺声明）
```c
// hybris/gralloc/gralloc.c，加在 #include <hardware/gralloc.h> 后
extern native_handle_t* native_handle_clone(const native_handle_t* handle);
```

### 运行失败（segfault）
| 测试 | 结果 | 原因 |
|---|---|---|
| test-egl（eglInitialize）| ❌ segfault | bionic linker 加载 libGLES_mali.so 崩 |
| HYBRIS_EGLPLATFORM=null | ❌ segfault | null 平台也崩（不是 hwcomposer 问题）|
| HYBRIS_LINKER=n / mm | ❌ segfault | 两个 linker 都崩 |
| LD_PRELOAD stub-rseq.so | ❌ segfault | rseq syscall 不是原因 |
| LD_LIBRARY_PATH 只 hybris 库 | ❌ Aborted | eglplatform_null.so 找不到 libcutils.so |
| 复制 Android libcutils/liblog/libdl/libc++/libc/libm | ❌ segfault | bionic libc + glibc libc 冲突 |

### 核心问题
**libhybris 需要完整的 Android 容器**（Halium 项目做法）：
1. **Android 侧打 hybris 补丁**（mer-hybris/hybris-patches 或 halium/hybris-patches）
2. **移除 TLS 访问**（bionic libc 的 TLS 槽会冲突 glibc）
3. **移除 zygote / surfaceflinger / bootanim**（不需要的 Android 进程）
4. **Android init 作为 systemd service / container 跑**

**魅蓝 Note5 没跑 Halium**（Android 侧是原厂 Flyme 6），**chroot Debian 里没有 Android 容器服务**，**libhybris 跑不通**。

---

## dmesg 关键日志（test-egl segfault）

```
[15556.714681] (0)[12391:test-egl]CPU: 0 PID: 12391 Comm: test-egl Tainted: G W 3.18.35+ #1
[15556.755743] (0)[12397:test-egl]Code: d2800002 d28024a8 b9000405 d4000001 (3140041f)
[15556.755759] (0)[12397:test-egl]Hardware name: MT6755V/CM (DT)
```

- **syscall 293（rseq）**：内核 3.18.35 不支持（rseq 是 4.15+ 引入），但 stub 后还是 segfault
- **真正原因**：bionic linker 加载 libGLES_mali.so 时 TLS 访问冲突 glibc TLS 槽（Android 侧没打 hybris 补丁）

---

## GPU 驱动栈（当前状态）

| 层 | 状态 |
|---|---|
| GPU 硬件 | ✅ Mali-T860 MP2（Midgard r12p1）|
| 内核驱动 | ✅ mali_kbase r12p1（/dev/mali0）|
| Android blob | ✅ libGLES_mali.so（OpenGL ES 3.2，26MB）|
| DRM | ❌ 无 /dev/dri/ |
| Mesa Panfrost | ✅ 编译成功（SolDev69/panfrost-gallium-mesa）|
| libhybris | ✅ 编译成功（libhybris/libhybris）|
| 桌面 GPU 加速 | ❌ 软件渲染（llvmpipe）|

---

## Android GPU blob 位置（魅蓝 Note5）

```
/system/vendor/lib64/egl/libGLES_mali.so  （26MB，Mali-T860 blob）
/system/lib64/libcutils.so
/system/lib64/liblog.so
/system/lib64/libdl.so
/system/lib64/libc++.so
/system/lib64/libc.so  （bionic libc）
/system/lib64/libm.so
/vendor/lib64/hw/hwcomposer.mt6755.so  （hwcomposer HAL）
/vendor/lib64/hw/gralloc.mt6755.so  （gralloc HAL）
```

---

## 解决方案（2 个，都需要大改）

### 方案 A：完整替换 Mesa 栈（Panfork 23.0.0 全套）
- **替换系统 Mesa 25.0.7 全部库**（libGL + libEGL + libglapi + panfrost_dri.so + swrast_dri.so）
- **风险**：桌面崩（ABI 不匹配），需要备份 + 回滚
- **时间**：30-60 分钟（编译 + 测试 + 调试）

### 方案 B：libhybris + Android 容器（Halium 路线）
- **Android 侧刷 Halium ROM**（mer-hybris/hybris-patches + halium/hybris-patches）
- **Linux 侧跑 libhybris + Android 容器服务**（systemd service / LXC container）
- **风险**：需要刷 Halium ROM（魅蓝 Note5 Halium 支持可能不完善）
- **时间**：2-4 小时（刷 ROM + 配置容器 + 调试）

---

## 我的建议（诚实）

**软件渲染（llvmpipe）是最实际的选择**（桌面稳定，OpenGL 应用能用，但慢）。

**方案 A（完整替换 Mesa）风险高**（桌面崩过一次，ABI 不匹配），**但 Panfork 源代码完整**。

**方案 B（Halium + libhybris）门槛高**（需要刷 Halium ROM + 配置 Android 容器），**魅蓝 Note5 Halium 支持可能不完善**。

**GPU 加速不是必须的**（软件渲染够用，桌面 + VNC + OpenGL 应用都能用）。

---

## 源代码位置

| 项目 | URL |
|---|---|
| **Panfork 源代码** | https://github.com/SolDev69/panfrost-gallium-mesa |
| **libhybris** | https://github.com/libhybris/libhybris |
| **Halium android-headers** | https://github.com/Halium/android-headers |
| **Halium hybris-patches** | https://github.com/halium/hybris-patches |
| **mer-hybris hybris-patches** | https://github.com/mer-hybris/hybris-patches |
| **tensor-g1-proot-gpu**（kbase ioctl 补丁参考）| https://github.com/SaicharanKandukuri/tensor-g1-proot-gpu |

---

## 下一步（你选）

1. **方案 A**：完整替换 Mesa 栈（Panfork 23.0.0 全套，风险高）
2. **方案 B**：libhybris + Android 容器（Halium 路线，门槛高）
3. **不折腾**：软件渲染（llvmpipe，桌面稳定，OpenGL 应用慢）

---

## 清理记录

### /tmp 清理
- GPU 测试垃圾（fb0*.raw/png、xorg.conf.*、panfrost-gallium-mesa 源码、mesa 编译目录）
- 重要文件归档到 `/mnt/shared/my-project/magisk-chroot-boot/twrp/docs/gpu-test/`

### /dev/shm 清理
- Panfork 编译目录（panfrost-gallium-mesa/build）
- libhybris 编译目录（libhybris-build/）

### 设备清理
- chroot `/root` 下：panfrost_dri.so + libGL.so.1 + libEGL.so.1 + libglapi.so.0 + libEGL_hybris.so + libGLESv2_hybris.so + libGLESv1_hybris.so + libsync_hybris.so + libhybris-common.so.1 + n.so + stub-rseq.so + test-egl + symlink
- chroot `/usr/lib/aarch64-linux-gnu/dri/`：panfrost_dri.so.bak（84MB）
- chroot `/usr/lib/aarch64-linux-gnu/`：libGL.so.1.7.0.bak + libEGL.so.1.1.0.bak
- chroot `/usr/local/lib/dri/`：swrast_dri.so
- chroot `/usr/lib/libhybris/`：libhybris 所有库 + eglplatform_*.so + Android libcutils/liblog/libdl/libc++/libc/libm
