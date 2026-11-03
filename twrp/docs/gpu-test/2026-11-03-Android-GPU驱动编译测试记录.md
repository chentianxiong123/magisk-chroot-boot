# Android GPU 驱动编译测试记录

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，mali_kbase r12p1）
**状态**：探索中（Panfork 编译成功，但替换系统库导致桌面崩，已回滚）

---

## 编译成功（Panfork Mesa 23.0.0）

**源代码**：SolDev69/panfrost-gallium-mesa（https://github.com/SolDev69/panfrost-gallium-mesa）

**编译环境**：
- 电脑（Debian trixie，x86_64，12 核 16G）
- 交叉编译工具链（gcc-aarch64-linux-gnu）
- 内存盘（/dev/shm，避免磁盘 I/O）

**编译命令**：
```bash
# 1. 克隆源代码
git clone https://github.com/SolDev69/panfrost-gallium-mesa.git
cd panfrost-gallium-mesa

# 2. 创建交叉编译配置
cat > aarch64-cross.txt << 'EOF'
[binaries]
c = ['aarch64-linux-gnu-gcc']
cpp = ['aarch64-linux-gnu-g++']
ar = ['aarch64-linux-gnu-ar']
strip = ['aarch64-linux-gnu-strip']
pkg-config = ['pkg-config']

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[properties]
needs_exe_wrapper = false
EOF

# 3. 装 ARM64 依赖库
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install -y libdrm-dev:arm64 libgbm-dev:arm64 libexpat1-dev:arm64 \
    libxext-dev:arm64 libxfixes-dev:arm64 libxxf86vm-dev:arm64 \
    libxcb-glx0-dev:arm64 libxcb-dri3-dev:arm64 libxcb-present-dev:arm64 \
    libxcb-sync-dev:arm64 libxshmfence-dev:arm64 libx11-xcb-dev:arm64 \
    libxcb-dri2-0-dev:arm64 libxcb-xfixes0-dev:arm64 libxcb-randr0-dev:arm64 \
    libwayland-dev:arm64 libz-dev:arm64 libzstd-dev:arm64 \
    bison flex byacc python3-mako python3-setuptools

# 4. 编译（内存盘，12 核并行）
mkdir -p /dev/shm/panfork-build
cd /dev/shm/panfork-build
PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig \
CFLAGS="-Wno-implicit-function-declaration" \
CXXFLAGS="-Wno-implicit-function-declaration" \
meson setup build --cross-file aarch64-cross.txt \
    -Dgallium-drivers=panfrost -Dplatforms=x11 -Ddri3=enabled \
    -Dllvm=disabled -Dshared-glapi=enabled -Dvulkan-drivers=
ninja -C build -j12
```

**编译时间**：1 分 47 秒（12 核并行，内存盘）

**编译结果**：
- `panfrost_dri.so`（82MB，支持 Mali-T860 + kbase ioctl）
- `libGL.so.1`（3.2MB）
- `libEGL.so.1`（1.5MB）
- `libglapi.so.0`（1.1MB）

---

## 测试结果（替换系统库）

### 测试 1：替换系统 panfrost_dri.so
- **结果**：桌面崩（libGL 报 swrast_dri.so 找不到）
- **原因**：Panfork 23.0.0 vs 系统 25.0.7 版本不匹配

### 测试 2：替换系统 libGL/libEGL/libglapi + panfrost_dri.so
- **结果**：glxinfo 报 swrast_dri.so 找不到
- **原因**：Panfork 找不到 swrast_dri.so（23.0.0 版），系统只有 25.0.7 版

### 测试 3：回滚系统库
- **结果**：桌面恢复（llvmpipe 软件渲染）
- **原因**：系统 Mesa 25.0.7 完整（swrast/llvmpipe 都有）

---

## 核心问题

**Panfork Mesa 23.0.0 vs 系统 Mesa 25.0.7 版本不匹配**：
- Panfork 找不到 swrast_dri.so（23.0.0 版，系统只有 25.0.7 版）
- 替换系统库会导致桌面崩（版本不一致）

---

## 解决方案（2 个）

### 方案 A：完整替换 Mesa 栈（Panfork 23.0.0 全套）
- **替换系统 Mesa 25.0.7 全部库**（libGL/libEGL/libglapi/panfrost_dri.so）
- **风险**：桌面崩（版本不匹配），需要备份 + 回滚
- **时间**：30-60 分钟（编译 + 测试 + 调试）

### 方案 B：libhybris + Android blob（最稳）
- **用 Android libGLES_mali.so**（OpenGL ES 3.2，支持 Midgard）
- **不需要替换系统 Mesa**（libhybris 桥接）
- **风险**：hwcomposer HAL 缺失（需要 FBDEV workaround）
- **时间**：30-60 分钟（编译 libhybris）

---

## 我的建议

**方案 B（libhybris）更稳**（不替换系统 Mesa，桌面不崩），**但需要编译 libhybris**（电脑上，30-60 分钟）。

**方案 A（完整替换 Mesa）风险高**（桌面崩过一次），**但 Panfork 源代码已有**（SolDev69/panfrost-gallium-mesa）。

---

## 源代码位置

| 项目 | URL |
|---|---|
| **Panfork 源代码** | https://github.com/SolDev69/panfrost-gallium-mesa |
| **tensor-g1-proot-gpu**（kbase ioctl 补丁参考） | https://github.com/SaicharanKandukuri/tensor-g1-proot-gpu |
| **libhybris**（方案 B） | https://github.com/libhybris/libhybris |
| **Halium**（libhybris 生态） | https://halium.org/ |

---

## 下一步（你选）

1. **方案 A**：完整替换 Mesa 栈（Panfork 23.0.0 全套，风险高）
2. **方案 B**：libhybris + Android blob（最稳，需要编译 libhybris）
3. **不折腾**：软件渲染（llvmpipe，桌面稳定，OpenGL 应用慢）
