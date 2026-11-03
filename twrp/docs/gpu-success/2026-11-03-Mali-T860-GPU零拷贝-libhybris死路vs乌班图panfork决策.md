# Mali-T860 GPU 零拷贝：libhybris 死路 vs 乌班图 panfork（最终决策）

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，mali_kbase r12p1，Android 7.1，TWRP recovery + chroot Debian）
**状态**：libhybris 零拷贝探测完结（不可行），乌班图 panfork 方案确认为更高可行性的下一步

---

## 一、目标回顾

要**真零拷贝**：GPU 渲染结果 → 显示控制器直扫，不经过 CPU 读回。
两条技术路线：
- **A. libhybris + Android HAL**（我们折腾了一整天）：GPU → gralloc buffer → hwcomposer 合成 → 屏幕
- **B. Mesa panfrost + kbase ioctl**（乌班图方案）：GPU → dma-buf → DRI3 → Xorg → 屏幕（纯 Linux 栈）

---

## 二、libhybris 路线：四条 GPU→gralloc 路径全部探测失败

| 路径 | 结果 | 根因（硬证据） |
|------|------|------|
| `eglCreateWindowSurface(hwcomposer)` | ❌ SIGSEGV | window 对象 0x38~0x68 函数指针 NULL，Android native eglCreateWindowSurface 跳 NULL 崩 |
| `eglCreatePbufferFromClientBuffer(gralloc)` | ❌ EGL_NO_SURFACE | Mali EGL error 0x3000，不支持 gralloc 当 client buffer |
| `eglCreateImageKHR + glEGLImageTargetTexture2DOES` | ❌ 崩 | EGLImage 包成功但 Mali kernel import 失败，dmesg ION kmap=0 |
| `hwcomposer 手动 prepare/set` | ❌ 崩 | MTK 需要 overlay session（`no session 196610 found`），probe 没注册 session |

**关键探测结论**：
- **hwcomposer 1.x HAL 能打开**（version=17104897）——上一个人说"必须 Android 8+"是**假的**
- **eglCreateImageKHR 并非禁用**（eglGetProcAddress 能拿到）——但 Mali kernel import gralloc buffer 失败（kmap=0）
- **probe20（填 stub 到 0x38~0x68）后 glClear 崩**（不是 eglSwapBuffers）——Mali GPU 渲染到 hwcomposer window 的 gralloc buffer 本身失败
- **根因**：libhybris 分配的 gralloc buffer Mali kernel 认不出（kmap=0）+ MTK hwcomposer 需要 session + libhybris hwcomposerwindow 缺 BufferQueue

**当前 CPU 读回瓶颈**（libhybris 可用路径）：512x512 = 5.8 FPS，1080x1920 = 0.75 FPS（慢，用户不接受）

---

## 三、乌班图方案（panfork）：可行性更高，正是真零拷贝 + 完美 Linux 栈

### 来源
- **13:20 文档** `2026-11-03-Android-GPU驱动路径-不动框架不换内核.md` 的方案 B：Mesa Panfrost + kbase ioctl + X11 DRI3
- 参考项目 **`tensor-g1-proot-gpu`**（github.com/SaicharanKandukuri/tensor-g1-proot-gpu）

### 真相：tensor-g1-proot-gpu 就是 panfork（Panfrost + kbase 后端）

README 关键证据（Playwright 抓的原文）：
```
"Experimental Panfork OpenGL and Mali Vulkan acceleration for Tensor G1 Termux/PRoot"
"kbase drivers, including the Tensor G1 Mali-G78 Job Manager path.
  It is intended for arm64 PRoot, chroot, and similar Linux environments which can access /dev/mali0.
  It is not a universal Mali driver and is not Vulkan-conformant."
```

Meson 配置（核心）：
```bash
meson setup build-gallium-jm \
  -Dgallium-drivers=panfrost,softpipe \
  -Degl-native-platform=x11 \
  -Dglx=dri \
  -Dpanfrost-kmds=panthor,kbase \      # ← 关键：kbase 后端 = 直接调 /dev/mali0
  -Dplatforms=x11 \
  ...
```

运行时开关：
```bash
export PAN_KBASE_GL_DRI3=1            # ← raw dma-buf GLX bridge（真零拷贝呈现！）
# 不设则用较慢的 X11 image fallback
```

### 为什么可行性比 libhybris 高（对比表）

| | libhybris（方案 A） | panfork（方案 B） |
|----|----|----|
| GPU 驱动 | Android blob（libGLES_mali.so）+ hwcomposer HAL | **Mesa panfrost（开源，标准）** |
| 内核 | 不换，但 gralloc buffer Mali kernel 认不出（kmap=0）| **不换，直接调 /dev/mali0 的 kbase ioctl** |
| 内存分配 | gralloc HAL（Mali 认不出）| **panfrost 自己的 dma-buf** |
| 呈现 | 卡死（hwcomposer session / BufferQueue）| **DRI3 dma-buf 直出 Xorg** |
| 真零拷贝 | 探测全部失败 | **原生支持**（PAN_KBASE_GL_DRI3=1）|
| 依赖 | Android HAL 全家 + libhybris 补丁 | Mesa + Xorg（**全 Linux 标准栈**）|
| 内核换不换 | 不换 | **不换**（kbase 后端直接访问 /dev/mali0）|

### 结论
**tensor-g1-proot-gpu 证明了这条路真实可行**：Panfrost 编译时 `-Dpanfrost-kmds=panthor,kbase`，运行时检测到 `/dev/mali*` 就走 kbase 后端，**直接调 /dev/mali0 的 kbase ioctl**，不依赖 DRM/KMS（解决了当年 18:35 零拷贝文档里"缺 /dev/dri"的误判——panfork 绕过 DRM，直接走 kbase ioctl）。

**关键修正**：18:35 `零拷贝探索.md` 当时判断"零拷贝不可行，缺 DMA-BUF/dri/dma_heap"，只看了 `/dev/dri` 节点，**漏掉了 panfork 可以绕过 DRM 直接走 kbase ioctl 这条路径**。这就是乌班图方案可行性更高的核心。

---

## 四、决策记录

**选 B（panfork）**，因为：
1. 全 Linux 标准栈（Mesa panfrost），不依赖 Android HAL/hwcomposer/gralloc
2. 真零拷贝原生支持（PAN_KBASE_GL_DRI3=1 的 raw dma-buf bridge）
3. 不换内核（kbase 后端直接访问 /dev/mali0）
4. 有真实成功案例（tensor-g1-proot-gpu 在 Mali-G78 kbase 上跑通 glmark2 + Plasma，见 README 截图 `panfrost-plasma-proof.png`）

**弃 A（libhybris）**，因为四条 GPU→gralloc 路径全部探测失败（kmap=0 / hwcomposer session / BufferQueue），根因是 libhybris 在 Android 7.1 MTK hwcomposer 1.1 上不完整，除非补 BufferQueue + IGraphicBufferProducer（2-4 天，成功率 30%）。

---

## 五、未验证（下一步）

- [ ] **拉 panfork 源码**（`/dev/shm/panfork-build/` 目前是空目录，只有 `aarch64-cross.txt` 计划）
- [ ] **交叉编译**：`aarch64-linux-gnu` + `-Dpanfrost-kmds=panthor,kbase -Dplatforms=x11 -Dglx=dri`
- [ ] **设备验证**：`/dev/mali0` 的 kbase ioctl 是否被 Android SELinux 允许（README 提示："PRoot cannot bypass Android permissions or SELinux; seeing a device node does not guarantee that its ioctls are permitted"）
- [ ] **风险**：Mali-G78 是 JM（job manager），T860 也是 JM，架构同系；但需验证 panfrost kbase 后端对 r12p1 ioctl 版本兼容
- [ ] 若 kbase ioctl 被 SELinux 挡 → 需降级 / 改 selinux，或回退 libhybris CPU 读回

---

## 六、本次探索留下的调试资产（可复现）

- **probe 程序**（电脑 `/dev/shm/libhybris-build/probe*.c/cpp`，设备 `/root/probe*`）：probe20-fill-stubs 填 window 函数指针 stub
- **hwcomposerwindow-patched.so**（84K，补丁版）：设置 ANativeWindow 函数指针
- **关键发现**：dladdr 确认 ANativeWindow 函数指针实际在 **0x70~0xa8**（不是 0x38~0x68），0x38~0x68 是数据字段
- **反汇编**：libEGL offset 0x3b60-0x3c2c，`3c24: ldr x4, [x20, #144]` + `blr x4` 跳 NULL 崩溃