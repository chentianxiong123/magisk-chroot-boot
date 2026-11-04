# WiFi/网络脚本库问题分析：bionic(64位) vs TWRP(32位库路径) 边界

**日期**：2026-11-04
**设备**：魅蓝 Note5（MT6755，Mali-T860 MP2，Android 7.0 Flyme 7.0.1.0A，kernel 3.18.35）
**范围**：TWRP 钩子脚本（`twrp/hooks/` + `twrp/modules/`）的 WiFi/网络/服务脚本
**结论**：库问题 = **同一个 bionic(64位) vs TWRP(32位库路径) 边界**，与 GPU 那条路同源；**大多已修好**（LD_LIBRARY_PATH 含 lib64），真正残留是 wpa_cli segfault（socket 问题非库问题）

---

## 一、脚本清单与库问题处理现状

| 脚本 | 库问题 | 处理 | 状态 |
|------|--------|------|------|
| `twrp/hooks/02-wifi.sh` | wpa_supplicant 64位需 lib64 | `LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib` | ✅ 已修 |
| `twrp/hooks/50-static-ip.sh` | ip 命令 64位需 lib64（否则 segfault 静默失败）| `export LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib` | ✅ 已修 |
| `twrp/hooks/30-chroot-services.sh` | 进 chroot 前库环境污染 glibc | `unset LD_PRELOAD LD_LIBRARY_PATH` | ✅ 已修 |
| `twrp/modules/wifi-up/service.sh` | /system/bin/ip 需 system libs | 注释明确"Running a /system binary in TWRP needs /system libs + linker" | ✅ 已修 |
| `twrp/modules/chroot-network/service.sh` | 用 /system/bin/ip 原生 | 设 `IP=/system/bin/ip` | ✅ 已修 |
| `twrp/modules/chroot-mount/service.sh` | 用 Debian busybox（glibc 环境）| 无跨库问题 | ✅ 无问题 |

---

## 二、关键证据：依赖库 64 位版全在（find 实测）

### ip / wpa_supplicant / wpa_cli 依赖库 64 位版位置（`find /system/lib64 /system/vendor/lib64`）

| 库 | 64位版位置 | 说明 |
|----|-----------|------|
| `libiprouteutil.so` | `/system/lib64` ✅ | ip 依赖，存在 |
| `libnetlink.so` | `/system/lib64` ✅ | ip 依赖，存在 |
| `libcrypto.so` | `/system/lib64` ✅ | wpa_supplicant 依赖，存在 |
| `libssl.so` | `/system/lib64` ✅ | wpa_supplicant 依赖，存在 |
| `libnl.so` | `/system/lib64` ✅ | wpa_supplicant 依赖，存在 |
| `libc++.so` | `/system/lib64` ✅ | 通用 |
| `libcutils.so` | `/system/lib64` ✅ | 通用 |
| `liblog.so` | `/system/lib64` ✅ | 通用 |
| `libwapi.so` | `/system/vendor/lib64` ✅ | wpa_supplicant 依赖 |

**结论**：所有依赖库的 **64 位版都存在**（/system/lib64 或 /system/vendor/lib64），**不存在"64位库缺失"**。

### 根因（脚本注释原文）

`50-static-ip.sh` 根因注释：
> "runner的LD_LIBRARY_PATH只有32位库, ip命令是64位需要lib64, 否则segfault静默失败"

`02-wifi.sh` 注释：
> "wpa_supplicant 64位需要lib64"

**根因**：TWRP runner（32位）默认 `LD_LIBRARY_PATH` **只指向 32 位库**，跑 64 位 Android 二进制（ip/wpa_supplicant/wpa_cli）时，**linker 找不到 64 位库 → segfault**。修复 = 显式设 `LD_LIBRARY_PATH` 含 `/system/lib64:/system/vendor/lib64`。

---

## 三、与 GPU 同源（同一库边界）

| 场景 | 边界 | 问题 |
|------|------|------|
| GPU（libhybris） | **bionic vs glibc** | glibc 进程加载 bionic 安卓库，需 libhybris 桥接 |
| WiFi/网络（ip/wpa） | **TWRP(32位) vs 64位 bionic** | 32位 runner 的库路径不含 64位 lib64 |

**两者是同一个"库环境不匹配"问题，但方向不同**：
- GPU：glibc ↔ bionic（跨 libc，需 libhybris）
- WiFi：32位 ↔ 64位（同 bionic 但位数不同，需 LD_LIBRARY_PATH 指向 lib64）

---

## 四、真正的残留：wpa_cli segfault（非库问题）

### 现象
`02-wifi.sh` 注释："wpa_cli会segfault但timeout+重定向兜住; runner会过滤Aborted"

### 排查（2026-11-04 实测）
wpa_cli 依赖库 64 位版**全在**：
- `libc++.so` `libc.so` `libcutils.so` `libdl.so` `liblog.so` `libm.so` → 全在 `/system/lib64`

**→ wpa_cli segfault 不是缺库**（依赖库全在），是**其他原因**：
- 最可能：wpa_supplicant 的 ctrl_interface socket 未就绪，或 TWRP 环境路径问题
- 被 `timeout 3` + 重定向**掩盖**，**没真修**

### 未验证（下一步）
- 需在 TWRP 环境实测：wpa_cli 是否 socket 未就绪（`-p /data/misc/wifi/sockets` 是否存在 + wpa_supplicant 是否先起）

---

## 五、综合结论

1. **WiFi/网络脚本的库问题 = bionic(64位) vs TWRP(32位库路径) 边界**，和 GPU 那条路同源（都是"库环境不匹配"）
2. **这些脚本大多已修好**——核心修复模式：`LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib`
3. **不是"64位库缺失"**（find 确认全在），是 **TWRP runner 库路径不含 lib64**
4. **真正的残留**：`wpa_cli segfault`（**非库问题**，是 socket/路径问题，被 timeout 掩盖未真修）
5. **修复模式已确立**：所有跑 64 位 Android 二进制的脚本，都要 `LD_LIBRARY_PATH` 含 `/system/lib64`——这是 TWRP(32位) 环境跑 Android(64位) 的通用规则

---

## 六、通用修复规则（可复用）

```bash
# 在 TWRP(32位) 环境跑任何 Android 64位二进制（ip/wpa_supplicant/wpa_cli/surfaceflinger/servicemanager）：
export LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib
```

这个规则和 chroot /system 方案（文档 `2026-11-04-chroot-system启动SurfaceFlinger-预演与资源占用.md`）里 SF 启动用的 LD_LIBRARY_PATH **完全一致**——同一个库边界，同一套修法。
