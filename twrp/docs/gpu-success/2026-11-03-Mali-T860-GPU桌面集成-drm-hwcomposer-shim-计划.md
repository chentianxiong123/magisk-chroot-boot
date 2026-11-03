# Mali-T860 GPU 桌面集成 - drm-hwcomposer-shim 编译计划

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2）
**状态**：GPU 硬件加速 + FPS benchmark 跑通（17654 FPS），桌面集成待做

---

## drm-hwcomposer-shim 架构（网上方案）

```
Wayland Compositor (Flick/Phosh/Sway)
         ↓
   Standard DRM/KMS/GBM APIs
         ↓
   drm-hwcomposer-shim（Rust）
         ↓
   libhybris
         ↓
   Android HAL (hwcomposer, gralloc)
         ↓
   GPU Driver (Mali-T860)
```

**原理**：
- **drm-hwcomposer-shim** 提供 DRM/KMS/GBM 兼容接口
- **内部调用 Android hwcomposer**（通过 libhybris）
- **任何 Wayland compositor** 都能用（Flick/Phosh/Sway）

---

## 编译依赖（设备上）

### 1. Rust 工具链（设备上）
```bash
# 安装 Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain stable --profile minimal
source ~/.cargo/env

# 验证
rustc --version  # 1.70+
cargo --version
```

### 2. libhybris 开发包（设备上）
```bash
# Debian chroot 里
apt-get install -y libhybris-common libhybris-dev libhwc2-compat-layer-dev
```

### 3. Clone Flick（设备上）
```bash
cd /root
git clone https://github.com/ruapotato/Flick.git
cd Flick/drm-hwcomposer-shim
```

### 4. 编译 drm-hwcomposer-shim
```bash
# 设备上编译（需要 30 分钟 - 1 小时）
cargo build --release

# 产物
ls -la target/release/
# libdrm_hwcomposer_shim.so（Rust 静态库）
# test_hwc（测试程序，颜色循环）
# test_gbm（GBM 测试）
# test_c_api（C API 测试）
```

### 5. 测试 drm-hwcomposer-shim
```bash
# 测试颜色循环（应该看到 RED -> GREEN -> BLUE 循环）
./target/release/test_hwc

# 如果成功，说明 drm-hwcomposer-shim 工作
```

---

## 编译时间预估

| 步骤 | 时间 |
|---|---|
| 安装 Rust | 10 分钟 |
| 安装 libhybris-dev | 5 分钟 |
| Clone Flick | 1 分钟 |
| 编译 drm-hwcomposer-shim | 30-60 分钟（Rust 编译慢）|
| 测试 | 5 分钟 |
| **总计** | **50-80 分钟** |

---

## 风险（魅蓝 Note5 可能不兼容）

### 风险 1：drm-hwcomposer-shim 只测了 Pixel 3a
- **Pixel 3a**：Adreno 615（Qualcomm）
- **魅蓝 Note5**：Mali-T860 MP2（ARM）
- **验证方法**：编译 test_hwc，看能不能跑

### 风险 2：hwcomposer.mt6755.so 可能不兼容 hwc2_compat_layer
- **hwcomposer.mt6755.so**：Android 7.1（hwcomposer 1.x）
- **hwc2_compat_layer**：hwcomposer 2.x 兼容层
- **验证方法**：看 hwc2_compat_layer 能不能加载 hwcomposer.mt6755.so

### 风险 3：Rust 编译可能失败
- **设备 RAM**：2GB（可能不够）
- **编译时间**：Rust 编译慢（可能 OOM）
- **验证方法**：加 swap，或用电脑交叉编译

---

## 回退方案（如果 drm-hwcomposer-shim 失败）

### 方案 A：应用级 GPU 加速（已经跑通）
- **应用直接用 libhybris libEGL**（渲染 PBuffer，输出到文件/网络）
- **GPU benchmark**：17654 FPS（glClear + eglSwapBuffers，不用 shader）

### 方案 B：Wayland compositor + libhybris EGL（Mer/Sailfish 方案）
- **weston** 或 **sway**（用 libhybris libEGL）
- **时间**：2-4 小时
- **风险**：Jolla 用，但 "far from perfect"

### 方案 C：Xorg + libhybris EGL driver（glamor 2D 加速）
- **Xorg driver "modesetting" + glamor**（用 libhybris libEGL）
- **时间**：1-2 小时
- **风险**：xorg-driver-libhybris 可能没有 arm64 版

---

## 下一步（分阶段）

### 阶段 2.1：安装 Rust + libhybris-dev（15 分钟）
```bash
# 设备 chroot Debian 里
apt-get install -y libhybris-common libhybris-dev libhwc2-compat-layer-dev

# 安装 Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain stable --profile minimal
source ~/.cargo/env
```

### 阶段 2.2：Clone Flick + 编译 drm-hwcomposer-shim（30-60 分钟）
```bash
cd /root
git clone https://github.com/ruapotato/Flick.git
cd Flick/drm-hwcomposer-shim
cargo build --release
```

### 阶段 2.3：测试 drm-hwcomposer-shim（5 分钟）
```bash
# 测试颜色循环
./target/release/test_hwc

# 如果成功，说明 drm-hwcomposer-shim 工作
```

### 阶段 2.4：编译 Flick compositor + 桌面（1-2 小时）
```bash
cd /root/Flick
cargo build --release

# 运行 Flick（用 drm-hwcomposer-shim）
./target/release/flick --backend=drm-shim
```

---

## 当前资产（必须保护）

### 已完成（GPU 硬件加速 + FPS benchmark）
- **libhybris 编译**：commit f47e718（TLS patcher）
- **三个关键修复**：patchelf + HYBRIS_TLS_PATCH + glibc libhardware.so.2
- **GPU 验证**：GL_RENDERER Mali-T860 + glReadPixels ff0000ff（红色背景）
- **FPS benchmark**：17654 FPS（glClear + eglSwapBuffers，不用 shader）
- **应用级 GPU 加速可用**（应用直接用 libhybris libEGL）

### 文档（全部归档）
- **`twrp/docs/gpu-success/`**（GitHub commit 8993586）
  - `test-egl.c` + `test-gles2.c` + `test-window.c` + `test-1frame.c`
  - `bench-gles2.c` + `fps-simple.c` + `fps-30frames.c` + `fps-5frames.c`
  - `2026-11-03-Mali-T860-GPU硬件加速-完整复现方法.md`（完整命令）
  - `2026-11-03-Mali-T860-GPU硬件加速-跑通实录.md`
  - `2026-11-03-Mali-T860-GPU硬件加速-验证结果.md`
  - `2026-11-03-Mali-T860-GPU硬件加速-FPS-benchmark.md`（17654 FPS）
  - `2026-11-03-Mali-T860-GPU硬件加速-桌面集成下一步.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-搜索结果与下一步.md`
  - `2026-11-03-Mali-T860-GPU桌面集成-drm-hwcomposer-shim-计划.md`（本文件）

### 设备状态（稳定运行）
- **桌面**：4 进程（Xorg + openbox + lxpanel + pcmanfm）✅
- **VNC**：5900 端口（电脑远程连接）✅
- **软件渲染**：llvmpipe（桌面 + VNC + OpenGL 应用能用）
- **GPU 加速**：libhybris libEGL（null + hwcomposer 平台跑通，17654 FPS）

---

## 结论（诚实）

**GPU 硬件加速 + FPS benchmark 跑通**：
- **Mali-T860 GPU 硬件加速**：✅
- **FPS benchmark**：✅（17654 FPS，PBuffer 离屏渲染）
- **应用级 GPU 加速**：✅（应用直接用 libhybris libEGL）

**桌面集成（drm-hwcomposer-shim + Flick）**：
- **时间**：50-80 分钟（安装 Rust + 编译 drm-hwcomposer-shim + 测试）
- **风险**：魅蓝 Note5 可能不兼容（drm-hwcomposer-shim 只测了 Pixel 3a）
- **回退方案**：应用级 GPU 加速（已经跑通，17654 FPS）

**下一步**：**阶段 2.1（安装 Rust + libhybris-dev，15 分钟）**，**然后阶段 2.2（编译 drm-hwcomposer-shim，30-60 分钟）**。
