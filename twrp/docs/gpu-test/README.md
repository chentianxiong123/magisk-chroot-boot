# GPU 测试归档（魅蓝 Note5 Mali-T860）

**日期**：2026-11-03
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，mali_kbase r12p1）
**结果**：Panfork 编译成功，但替换系统库导致桌面崩，已回滚到软件渲染

---

## 文件清单

### 脚本（桌面 + VNC + Power 键）
- `start-desktop.sh`：桌面启动（Xorg + openbox + lxpanel + pcmanfm）
- `start-vnc.sh`：x11vnc 启动（VNC 服务器）
- `power-key-toggle.py`：Power 键监听（背光开关）
- `power-key-toggle.sh`：Power 键 shell 版
- `listen-keys.py`：input 事件监听
- `read-all-events.py`：读所有 input 设备
- `read-event4.py`：读 event4（指纹键）
- `draw_tri.py`：framebuffer 绘制三角形（测试）
- `fb0-flip-daemon.py`：fb0 翻转守护（已废弃，numpy 没装）

### 配置
- `xorg.conf`：Xorg 配置（fbdev + Rotate UD）
- `mesa-panfork.dsc`：Panfork Mesa 源码包描述（Launchpad PPA）

### 截图
- `screenshots/fb0_color.png`：fb0 颜色测试
- `screenshots/fb0_now.png`：fb0 当前状态
- `screenshots/fb0.png`：fb0 原始截图
- `screenshots/fb0_rot90.png`：fb0 旋转 90 度
- `screenshots/vnc-window.png`：VNC 窗口（电脑看手机屏）

### 文档
- `2026-11-03-Android-GPU对接Linux桌面-通用方案探索笔记.md`：通用方案对比（libhybris / DRM compat / Vulkan WSI）
- `2026-11-03-Android-GPU驱动路径-不动框架不换内核.md`：Panfork + kbase ioctl 路径
- `2026-11-03-Android-GPU驱动编译测试记录.md`：编译成功 + 替换失败记录
- `篇6-桌面环境与远程投屏实战.md`：篇6 归档（含 Rotate UD + GPU 探索）

---

## GPU 驱动栈（当前状态）

| 层 | 状态 |
|---|---|
| GPU 硬件 | ✅ Mali-T860 MP2（Midgard r12p1）|
| 内核驱动 | ✅ mali_kbase r12p1（/dev/mali0）|
| Android blob | ✅ libGLES_mali.so（OpenGL ES 3.2）|
| DRM | ❌ 无 /dev/dri/ |
| Mesa Panfrost | ✅ 编译成功（SolDev69/panfrost-gallium-mesa）|
| 桌面 GPU 加速 | ❌ 软件渲染（llvmpipe）|

---

## 测试结果

| 测试 | 结果 | 原因 |
|---|---|---|
| Panfork 编译 | ✅ 成功 | 1 分 47 秒（12 核并行，内存盘）|
| 替换系统 panfrost_dri.so | ❌ 桌面崩 | ABI 不匹配（23.0.0 vs 25.0.7）|
| 替换全套库（libGL/libEGL/libglapi） | ❌ 加载失败 | Panfork 找不到匹配的 swrast_dri.so |
| 回滚系统库 | ✅ 稳定 | llvmpipe 软件渲染 |

---

## 核心教训

**Mesa 内部 ABI 不稳定**，**所有组件（libGL/libEGL/libglapi/panfrost_dri.so）必须来自同一版本同一编译**，**不能混搭**。

---

## 下一步（你选）

1. **方案 A**：完整替换 Mesa 栈（Panfork 23.0.0 全套，风险高）
2. **方案 B**：libhybris + Android blob（最稳，需要编译 libhybris）
3. **不折腾**：软件渲染（llvmpipe，桌面稳定）
