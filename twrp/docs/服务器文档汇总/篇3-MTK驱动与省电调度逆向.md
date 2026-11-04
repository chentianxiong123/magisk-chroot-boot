# 篇3：MTK 驱动 + 省电调度逆向 —— thermalloadalgod 真相与大核 offline 决策者

> 时间：2026-11-02
> 设备：魅蓝 Note5 (MT6755, aarch64, Android 7.0, kernel 3.18.35)
> 本篇聚焦：**MTK 驱动接口、守护依赖链、thermalloadalgod 真相、大核 offline 决策者**
> 数据来源：设备实测扫描 + 二进制逆向（readelf/strings）+ /proc 采样

---

## 一、背景：为什么要逆向 MTK 驱动

**用户需求**：跑全部 MTK 底层驱动/守护（电量/温控/CPU调度），但砍掉谷歌框架吃内存的部分。

**核心问题**：
1. MTK 守护（fuelgauged/thermalloadalgod/thermald 等）能否脱离谷歌框架（zygote/system_server）独立跑？
2. 大核 offline（省电）的决策者是谁？thermalloadalgod？system_server？内核 governor？

**数据来源**：
- 设备实测扫描：`/tmp/drv-scan/{drv,scr,led}.txt`
- 二进制逆向：`/tmp/drv-scan/bins/`（readelf + strings）
- /proc 采样：`/proc/599/stack`（thermalloadalgod pid 599）

---

## 二、MTK 用户态驱动接口（sysfs 全扫描）

### 2.1 屏幕

| 项 | 节点 | 说明 |
|---|---|---|
| 背光开关 | `/sys/class/leds/lcd-backlight/brightness` (0-255) | **背光真实路径**（不是 /sys/class/backlight/） |
| 辅助灯 | `/sys/class/leds/mx-led/brightness` (0-255) | |
| LED 控制器 | `/sys/devices/platform/leds-mt65xx/leds/` | MTK led 平台驱动 |
| framebuffer | `/sys/class/graphics/fb0/` | blank/power/mode 等 |
| 背光降功耗 | `cooling_device3-6 = mtk-cl-backlight0X` | MTK 把背光当 cooling device 限流 |

**结论**：屏幕亮灭/背光 = 写 `/sys/class/leds/lcd-backlight/brightness`，纯 sysfs，**不需要框架**。

### 2.2 电量 / 充电

`/sys/class/power_supply/` 下：`ac` `battery` `usb` `wireless`

battery 全属性（`/sys/class/power_supply/battery/`）：
- 容量/状态：`capacity` `status` `health` `present` `technology` `cycle_count`
- 电压电流：`voltage_now` `current_now` `batt_vol`
- MTK 扩展：`BatteryAverageCurrent` `BatterySenseVoltage` `ChargerVoltage` `ISenseVoltage` `InstatVolt` `TempBattVoltage` `TemperatureR` `adjust_power` `batt_temp` `capacity_smb` `status_smb` `present_smb`
- 实测：capacity=100 voltage_now=4391000 current_now=0 status=Full health=Good technology=Li-ion

ac/usb/wireless 各有 `online`（充电插入状态）。

**相关 native 守护（class main）**：`fuelgauged`（电量计）、`poad`（power off alarm）、`batterywarning`（电量警告）

**结论**：电量读数 = 纯 sysfs 读 `power_supply`，**不需要框架**。

### 2.3 温控

- thermal_zone：`mtktsbattery`(0) `mtktscpu`(1) `mtktspa`(2) `mtktspmic`(3) `mtktstsx`(4) `mtktswmt`(5) `mtkts6311`(6)
- cooling_device（71 个）：cpu00-cpu32（CPU 限频）、`mtk-cl-backlight0X`（背光）、`mtk-cl-shutdown0X`（关机）、`cpu_adaptive_0-2`、`abcct` 等
- 接口：`/sys/class/thermal/thermal_zoneX/temp` `/type`，`cooling_deviceX/cur_state` `/max_state`

**相关 native**：`thermald` `thermal` `thermal_manager` `thermalloadalgod`

**结论**：温控 = sysfs 读温度 + 写 cooling device，**节点层面不需要框架**。

### 2.4 CPU 调度 / 热插拔

- cpufreq 节点齐全：`scaling_governor/cur_freq/max_freq/min_freq/setspeed`（interactive governor，参数 `hispeed_freq=1807000 go_hispeed_load=99 target_loads=90 timer_rate=20000`）
- **热插拔**：`/proc/cpu_hotplug/`（`cpu_lat` 统计上下线延迟、`timestamp_enable`）
- **大核热插拔决策 = thermalloadalgod（MTK 算法）**，实测它依赖 system_server（stop system_server 后大核全醒）

**结论**：调频 sysfs 直接可用；**但"自动关大核省电"的算法（thermalloadalgod）依赖 system_server**——这是脱离框架省电的**关键卡点**。

---

## 三、native 守护清单（class main，53 个，全 MTK）

功耗/电量/热（你要的）：
- `thermalloadalgod` /vendor/bin/thermalloadalgod
- `thermald` /vendor/bin/thermald
- `thermal` /vendor/bin/thermal
- `thermal_manager` /vendor/bin/thermal_manager (oneshot)
- `fuelgauged` /vendor/bin/fuelgauged
- `poad` /vendor/bin/poad
- `batterywarning` /vendor/bin/batterywarning

其它 class main：modem/mdlogger（基带日志）、dhcpd（网络）、agpsd/slpd（定位）、mediaserver、atcid、NvRAMAgent、msensord 等——**全是 native，没有 Java**。

**依赖（初步 strings，未 readelf 确认）**：
- thermalloadalgod 依赖：libc++ libc libcutils libdl libm **libthermalalgo**(vendor) libutils
- **不依赖 libbinder**（静态 strings 判定，待 chroot readelf 复核）
- 这些守护都是 `user system`，由 init 的 `class_start main` 拉起

---

## 四、MTK 守护二进制级逆向（颠覆性结论）

### 4.1 核心结论（颠覆）

**thermalloadalgod（省电调度核心）完全不依赖 binder / system_server。**

之前判"thermalloadalgod 依赖 system_server"是**错的**——那是 boot_slim 里用 `LD_LIBRARY_PATH=... thermalloadalgod &` 手动瞎起导致环境不对的假象。二进制证明它就是为脱离框架设计的。

### 4.2 依赖链全套（readelf 实测）

| 二进制 | 位数 | NEEDED 依赖 | binder符号 | socket符号 |
|---|---|---|---|---|
| **thermalloadalgod** | 32 | libcutils libutils libc++ libm libc libdl | 0 | netlink+TA socket |
| **libthermalalgo.so**（dlopen） | 32 | 同上（无 binder） | 0 | socket/bind/sendmsg/recvmsg |
| **fuelgauged** | 32 | libcutils libutils libc++ libm libc libdl | 0 | 0 |
| **thermal** | 32 | libcutils libc libnetutils libc++ libm | 0 | 1 |
| **thermald** | 64 | libcutils libutils libbinder libc++ | 0 | 0 |
| **batterywarning** | 32 | libcutils libutils libbinder libc++ | 0 | 0 |

注：thermald/batterywarning 的 NEEDED 写了 libbinder，但**导入符号 binder 计数=0**——链接期残留，运行时不用 binder。

### 4.3 thermalloadalgod 真实启动机制

它是**壳**，真正算法在 vendor 库里：

```
thermalloadalgod (壳, /vendor/bin/)
  └─ dlopen("libthermalalgo.so")
     └─ dlsym("libthermal_algo_setup")  ← 关键 setup 入口
        └─ 算法: CATM / TJ (junction temp) 温控 + 调度
```

libthermalalgo.so 通信机制（strings 实测）：
- `ta_init_socket` / `ta_exit_socket` — TA socket
- `ta_send_nl_message` / `ta_wait_kernel_notify` / `ta_receiving_nl_thread` — **netlink 与内核 thermal 驱动通信**
- `ta_daemon_init` — 设 TA daemon pid
- `tad_ctrl_cmd_to_kernel` — 控制命令下发内核
- 关键常量: `TA_DAEMON_CMD_SET_TTJ` `TA_DAEMON_CMD_NOTIFY_DAEMON_CATMINIT` `CATM_ON` `TARGET_TJ`
- 错误串: `get control socket error/success` — 起不起得来取决于这个 control socket

**→ 它对接的是"内核 thermal 驱动 + TA(control) socket"，不是 binder。** boot_slim 有 netlink/thermal 接口 → 理论可跑。

---

## 五、boot_slim 拉起 MTK 守护机制

### 5.1 官方拉起链（init 如何启动服务）

```
/init (二级 init)
 └─ import /init.${ro.hardware}.rc   →  /init.mt6755.rc  (ro.hardware=mt6755, 77KB, 确认加载)
     └─ 解析全部 service 定义 (98 个 service)
         └─ on nonencrypted 段
             ├─ class_start core      →  起所有 class core (驱动/基础, boot_slim 保留)
             ├─ # class_start main    →  boot_slim 已注释 (原本起全部 class main: 53个MTK守护+谷歌框架)
             └─ # class_start late_start →  已注释
```

- 服务定义例（`/init.mt6755.rc:1897`）：
```
service thermalloadalgod /vendor/bin/thermalloadalgod
    class main
    user system
    group system
```
- **这些 MTK 守护和谷歌框架(zygote/system_server)混在同一个 `class main`，`class_start main` 是打包起，没法用 class 分开。**

### 5.2 关键机制：class_start main 起单个 → 用 `start <名>`

- `class_start main` 无法只起单个服务
- init 支持 **`start <服务名>`** 单独拉起（不受 class 限制）
- **这就是"既要又要"的技术基础**：不 class_start main（谷歌框架不起），改用 `start thermalloadalgod` `start fuelgauged` 等单独起 MTK 守护

### 5.3 实测：boot_slim 下 `start thermalloadalgod`

```
start thermalloadalgod  →  started
ps: pid 599, user system, S 睡眠   ← init 官方拉起成功(uid/环境对)
```

### 5.4 问题：thermalloadalgod 起来后不工作

- ticks 完全不动（11983 → 11983，6秒零CPU）→ **阻塞，在等某东西**
- 大核没自动 offline（cpu4-7 仍 online）→ **没在做调度**

### 5.5 它打开的 fd（阻塞点）

```
3 -> socket:[11346]        ← TA control socket (libthermalalgo ta_init_socket)
4,5 -> /sys/devices/virtual/misc/mz_rpmb_ctl/dev   ← rpmb (安全存储)
6 -> /dev/pmsg0
7 -> socket:[15978]        ← netlink (内核 thermal notify)
```

**结论：thermalloadalgod 不是"缺接口"，而是起来后阻塞在 socket，在等：**
1. 配套服务喂它（thermal_manager 初始化？内核 notify？）
2. 或 TA(control) socket 的对端没就绪
3. 或需要内核侧先 trigger 一个 thermal notify

---

## 六、thermalloadalgod 阻塞真因：等内核 thermal netlink notify

### 6.1 决定性证据：阻塞在内核 netlink recv

`cat /proc/599/stack`（thermalloadalgod pid 599）：
```
__skb_recv_datagram      ← 阻塞在这
skb_recv_datagram
netlink_recvmsg          ← 在收 netlink 消息
sock_recvmsg
___sys_recvmsg
compat_SyS_recvmsg
el0_svc_naked
```
`wchan` = `__skb_recv_datagram`

**→ thermalloadalgod 阻塞在 `netlink_recvmsg`，等内核 thermal netlink notify。没等到 → 不调度、大核不 offline。**

### 6.2 配套服务测试

| 服务 | 起没起 | 说明 |
|---|---|---|
| thermalloadalgod | ✅ 起（但阻塞） | pid 599 |
| thermald | ✅ 起 | pid 1043 |
| thermal_manager | ❌ 没起 | `class main` `oneshot` `user root`，`start thermal_manager` 没起 |
| thermal | ❌ 没起 | `class main` `user system`，`start thermal` 没起 |

thermal_manager 是 `oneshot`，它起来执行一次就退，可能就是"初始化内核 thermal/喂第一个 notify"的角色——**它没起可能是 thermalloadalgod 收不到 notify 的原因之一**。

---

## 七、thermalloadalgod 阻塞判定：等事件（正常设计）vs 卡死

### 7.1 已排除的卡点

| 假设 | 验证 | 结果 |
|---|---|---|
| 阻塞在等内核 thermal netlink notify | /proc/599/stack = `netlink_recvmsg` | ✅ 确认阻塞点 |
| thermal_zone disabled 导致无 notify | `echo enabled > */mode` 全 enable | ❌ 不是——enable 后 ticks 仍 11983 恒定 |
| 配套服务没起 | `start thermal_manager/thermal/thermald` | thermal_manager/thermal 没起（oneshot/user权限?），thermald 起了 |

### 7.2 当前判断：idle 阻塞等事件，可能就是正常设计

- thermalloadalgod 是**事件驱动**：内核 thermal/load 事件 → netlink notify → 它醒来算 TTJ/调度 → 没事件就阻塞在 `netlink_recvmsg`
- **idle 时 ticks 不动 = 它在阻塞等事件**，不一定异常
- boot_slim 是纯服务器，无 UI 无温度飙升，**可能根本没有 thermal/load 事件触发 notify** → 它就一直阻塞（正常）

---

## 八、大核 offline 决策者再定位（最终结论）

### 8.1 决定性对照数据

| 环境 | thermalloadalgod ticks (idle) | 大核 cpu4-7 |
|---|---|---|
| **normal** | 不动 (7083→7083, 8秒) | **offline** (0) |
| **boot_slim** | 不动 (11983→11983) | **online** (1) |
| **hybrid（TWRP ramdisk）** | 不跑（没有这个守护） | **offline** (0) ✅ |

**→ thermalloadalgod 在 normal idle 时也阻塞等事件（ticks 不动）。它 idle 不主动调度。**

**→ 但 normal 大核 offline、boot_slim online。这个差异，在 thermalloadalgod 都"idle 睡着"的情况下出现 → 大核 offline 决策者可能不是 thermalloadalgod 的主动轮询。**

### 8.2 最终结论：大核 offline 的真相

**大核 offline 的决策者 = "是否跑安卓框架"（不是 thermalloadalgod 主动调度）**：

| 环境 | 是否跑安卓框架 | 大核状态 |
|---|---|---|
| normal（跑框架） | ✅ | offline（0） |
| boot_slim（不跑框架，但有 thermalloadalgod） | ❌ | online（1） |
| **hybrid（TWRP ramdisk，不跑框架，没 thermalloadalgod）** | ❌ | **offline（0）** ✅ |

**解释**：
1. normal 跑框架 → system_server 的 PowerManager/AMS 调用 CPU hotplug → 大核 offline（可能是"保持在线以满足 app 需求"的反向逻辑，或 thermal governor 的 CFS idle 预测）
2. boot_slim 不跑框架，但有 thermalloadalgod → thermalloadalgod idle 时阻塞等事件，不发 offline 命令 → 大核 online
3. **hybrid（TWRP ramdisk）不跑框架，也没 thermalloadalgod** → 没人保持大核在线 → **内核 idle governor 自动 offline 大核**（CFS idle 预测，事件触发一次性 offline）

### 8.3 为什么 hybrid 大核 offline（省电的关键）

**TWRP ramdisk 环境不跑安卓框架**（zygote/system_server/ART）：
- 没有 system_server 的 PowerManager/AMS 调用 CPU hotplug
- 没有 thermalloadalgod 阻塞等事件（不跑它）
- **内核 idle governor 自动管理 CPU hotplug**（CFS idle 预测）→ **大核自动 offline**

**这就是省电的关键**：不跑框架 → 没人保持大核在线 → 内核 idle governor 自动 offline → 省 CPU 功耗。

**不是 thermalloadalgod 主动调度**，而是 **TWRP 环境的副作用**（不跑框架 → 没人保持大核在线）。

### 8.4 之前归因错误的地方

| 假设 | 验证 | 结果 |
|---|---|---|
| thermalloadalgod 依赖 system_server（binder） | 二进制 readelf | ❌ **无 binder 符号**（NEEDED 无 libbinder，导入符号 binder 计数=0） |
| thermalloadalgod 主动调度大核 offline | /proc/599/stack + ticks 采样 | ❌ **idle 时阻塞等事件，不调度** |
| 大核 offline = thermalloadalgod 主动决策 | normal vs boot_slim vs hybrid 对照 | ❌ **是 TWRP 环境不跑框架的副作用** |

**真正原因**：TWRP ramdisk 不跑安卓框架 → 没人保持大核在线 → 内核 idle governor 自动 offline。

---

## 九、逆向方案可行性判断

### 9.1 ✅ 纯 sysfs 直接可用（无需任何框架）

- 屏幕亮灭/背光：写 lcd-backlight/brightness
- 电量读数：读 power_supply/battery/*
- 温度读数：读 thermal_zoneX/temp
- 手动调频：写 cpufreq 节点
- **手动关核省电**：echo 0 > cpuX/online（已验证可行）

### 9.2 ⚠️ 需要轻量 native 守护（不依赖框架，但要 init 正确拉起）

- thermald/thermal/fuelgauged/poad：native，RSS 各 ~2MB，可独立跑（boot_slim 已验证 thermald 能跑）
- **关键**：要让 init 正确拉起（service + class main + class_start main），不能手动 `&`（环境不对会空转）

### 9.3 ❌ 依赖 system_server（脱离不了）

- **thermalloadalgod 的"自动关大核省电"算法**：实测 stop system_server 后大核全醒 → 它的调度依赖 system_server 提供的某个 binder 服务
- 要真正脱离，需逆向 thermalloadalgod ↔ system_server 的 binder 协议（工作量大、风险高）

**但最终发现**：thermalloadalgod 不依赖 binder（二进制证明），且它 idle 时阻塞等事件不调度。**大核 offline 是 TWRP 环境不跑框架的副作用**，不是 thermalloadalgod 主动调度。

---

## 十、"既要又要"的真正解法（最终）

### 10.1 方案 A（失败）：保留 system_server + 砍纯 UI app

保留 system_server + zygote（省电调度保住），砍纯 UI app（systemui/meizu/flyme）——但 force-stop 无效，需在 boot 层阻止它们起（改 zygote/ams 启动参数，复杂）。

**结论**：❌ 复杂、风险高、改 boot 危险。

### 10.2 方案 B（失败）：砍 zygote/system_server + 手动热插拔兜底

砍 zygote/system_server，放弃 thermalloadalgod 自动调度，用 MTK 其它轻量守护 + 手动/轻量热插拔兜底。

**结论**：❌ 用户反感"我写的调度脚本"（`echo 0 > cpuX/online` 这种）。

### 10.3 方案 C（成功）：hybrid recovery（TWRP ramdisk 不跑框架）

**hybrid = normal 内核 + TWRP ramdisk**：
- **normal 内核驱动全**（"既要"）：跑全部 MTK 驱动/守护（电量/温控/wifi）
- **TWRP ramdisk 不跑框架**（"又要"）：不跑 zygote/system_server/ART → **大核自动 offline（省电）**
- **大核 offline 的真相**：TWRP 环境不跑框架 → 没人保持大核在线 → 内核 idle governor 自动 offline（**不是 thermalloadalgod 主动调度**）

**结论**：✅ 这才是"既要又要"的正解。

---

## 十一、下一步（用户定的方向）

1. ~~在 normal 下逆向 thermalloadalgod 的 binder 依赖~~（已证明无 binder）
2. ~~该服务若可最小化 → boot_slim 保留最小 system_server 子集 + MTK 守护~~（不需要，TWRP 环境已经够）
3. ~~倒回改 boot 镜像（这次不砍太狠，精准砍谷歌肥肉 UI）~~（不需要，hybrid 不改 boot）
4. **最终方向**：hybrid recovery + 钩子（normal 内核 + TWRP ramdisk + `/data/local/tmp/twrp-hooks/*.sh`）

**原始扫描数据**：`/tmp/drv-scan/`

---

## 十二、关键技术洞察（背景故事）

### 12.1 用户反感"我写的调度脚本"

用户明确说："**你反感我写的调度脚本，要官方二进制/标准化方法（Magisk 模块式钩子）**"。

**所以最终方案**：
- **不用手写热插拔脚本**（`echo 0 > cpuX/online` 这种"我写的"）
- **用官方二进制**：fuelgauged、wpa_supplicant、supervisord
- **用 Magisk 式钩子**：`/data/local/tmp/twrp-hooks/*.sh`，放脚本即跑

### 12.2 大核 offline 的真相（颠覆认知）

**最初假设**：thermalloadalgod（MTK 省电调度算法）负责大核 offline，它依赖 system_server。

**二进制逆向证明**：thermalloadalgod **完全不依赖 binder/system_server**（NEEDED 无 libbinder，导入符号 binder 计数=0）。它对接的是"内核 thermal 驱动 + TA(control) socket"。

**最终发现**：**大核 offline 的真相是"TWRP ramdisk 环境不跑框架"**（不是 thermalloadalgod 主动调度）：
- normal idle：大核 offline（0）✅
- boot_slim idle：大核 online（1）❌
- **hybrid（TWRP ramdisk）idle：大核 offline（0）✅** ← 这就是省电的关键

**thermalloadalgod idle 时 ticks 不动**（在等内核 thermal netlink notify），**它不主动调度**。大核 offline 是 TWRP 环境的副作用（不跑框架 → 没人保持大核在线）。

### 12.3 内存省 530M（砍框架的收益）

| 环境 | 内存使用 |
|---|---|
| normal（跑框架） | ~2GB |
| hybrid recovery（不跑框架） | ~447MB |
| **节省** | **~1.5GB** |

**砍掉的**：zygote、system_server、ART、systemui、meizu/flyme UI app。
**保留的**：MTK 守护（fuelgauged/thermalloadalgod/thermald）、chroot 服务（sshd/cpa/freellmapi）。

---

## 十三、一句话总结

**MTK 驱动全 sysfs 直接可用（电量/温控/CPU），守护不依赖 binder/system_server（二进制证明），但 thermalloadalgod idle 时阻塞等内核 thermal netlink notify（不主动调度）。大核 offline 的真相是"TWRP ramdisk 环境不跑框架 → 没人保持大核在线 → 内核 idle governor 自动 offline"（不是 thermalloadalgod 主动调度）。**

**"既要又要"的正解 = hybrid recovery（normal 内核驱动全 + TWRP ramdisk 不跑框架 + 大核自动 offline）**。

---

## 十四、TWRP UI 直接读 sysfs/rtc（不通过框架，重要发现）

**用户发现**：**"哪怕是以前 twrp 好像也是可以读温度 时间 电池的"**

**逆向 recovery 二进制（strings 扫描）**，TWRP 直接读以下路径：

### 14.1 电池（sysfs power_supply）
```
/sys/class/power_supply/AC/online
/sys/class/power_supply/battery/capacity   ← 电量百分比
/sys/class/power_supply/battery/status     ← 充电状态
```

### 14.2 温度（sysfs thermal）
```
/sys/devices/virtual/thermal/thermal_zone1/temp   ← CPU 温度
```

### 14.3 时间（/dev/rtc 硬件时钟）
```
/dev/rtc[0-9]   ← RTC 硬件时钟（TWRP 读这个显示时间，不依赖系统时间）
```

### 14.4 实测验证（hybrid recovery 里）
```
# 电池
cat /sys/class/power_supply/battery/capacity  → 83
cat /sys/class/power_supply/battery/status    → Not charging
cat /sys/class/power_supply/battery/voltage_now → 4059000 (mV)

# 温度（thermal_zone）
thermal_zone0: 30000 (30°C)  ← 电池
termal_zone1: 36900 (36.9°C) ← CPU
termal_zone5: 42000 (42°C)   ← 其他
termal_zone6: 60000 (60°C)   ← PMIC

# 时间
date  → Tue Nov  3 01:01:05 GMT 2026
cat /proc/uptime → 268.15 267.69
```

### 14.5 意义：TWRP UI 不依赖 fuelgauged 也能读电池/温度

**TWRP UI 直接读 sysfs/rtc**（不通过 fuelgauged）：
- battery/capacity（电量）
- battery/status（充电状态）
- thermal_zone1/temp（温度）
- /dev/rtc（时间）

**但这只是 UI 显示**，不代表系统其他部分（比如 batt-display bind mount）不需要 fuelgauged。

### 14.6 batt-display 服务（上一任写的 TWRP 专用，不依赖 fuelgauged）

**重大发现**：chroot 里有个 `batt-display` 服务（`/usr/local/bin/batt-display`），**完全不依赖 fuelgauged**，直接读 dmesg + thermal_zone0。

**背景**：TWRP 下 MTK 电量计不初始化：
- `capacity=-1`、`batt_temp=0`、`voltage_now 冻结不刷新`

**batt-display 的逻辑**：
```sh
# 电压 ← 内核 bat_routine_thr 日志的 AvgVbat（10 秒一条，实时）
# 温度 ← thermal_zone0（32.0C -> 写 320，工具按 0.1C 解释）
# 电量 ← 电压查表插值（估算，非库仑计）
# bind mount 覆盖 capacity / batt_temp；不写闪存，umount 即恢复

while :; do
    line=$(dmesg | grep "bat_routine_thr][kernel]" | tail -1)
    mv=$(echo "$line" | sed -n 's/.*AvgVbat \([0-9][0-9]*\).*/\1/p')  # 电压
    t=$(cat /sys/class/thermal/thermal_zone0/temp)                     # 温度
    # 电压查表插值算 capacity（3400mV=1%, 4400mV=100%）
    p=$(awk -v mv="$mv" 'BEGIN{...}')
    echo "$p" > /tmp/batt/capacity
    echo $((t / 100)) > /tmp/batt/batt_temp
    sleep 5
done
```

**关键点**：
1. **不读 fuelgauged**，直接读 `dmesg`（内核 bat_routine_thr 日志）拿实时电压
2. **读 thermal_zone0** 拿温度
3. **电压查表插值**算 capacity（估算，非库仑计）
4. **bind mount 覆盖** `/sys/class/power_supply/battery/capacity` 和 `batt_temp`（不写闪存，umount 即恢复）

### 14.7 实测：停 fuelgauged 后 capacity=-1（batt-display 挂错路径）

**实验**：停掉 fuelgauged，看 batt-display 能否接管：

```
# 停 fuelgauged
killall fuelgauged

# 结果：capacity=-1（batt-display 的 bind mount 没生效）
 capacity=-1
```

**根因：batt-display 在 hybrid 挂错路径**

| 项 | batt-display 挂到的路径 | 宿主实际路径 |
|---|---|---|
| capacity | `/data/debian/sys/devices/platform/battery/...` | `/sys/class/power_supply/battery/capacity` |
| batt_temp | 同上（chroot 内） | `/sys/class/power_supply/battery/batt_temp` |

**为什么挂错**：batt-display 脚本里 `SYS=/sys/class/power_supply/battery`，在 chroot 内执行时相对路径变成 `/data/debian/sys/...`（chroot 根是 /data/debian），但**宿主的 `/sys/class/power_supply/` 没被覆盖**。

**结论**：**fuelgauged 在 hybrid 里真的需要**（batt-display 挂错路径，接管不了）。

### 14.8 batt-display 是上一任在 2026-11-01 15:52 写的（背景）

**解决什么**：TWRP 下 MTK 电量计不初始化（`capacity=-1`、`batt_temp=0`、`voltage_now` 冻结）。

**逻辑**：读 dmesg（内核 bat_routine_thr 日志）拿实时电压 + 读 thermal_zone0 拿温度 + 查表算 capacity → bind mount 覆盖 sysfs。

**在 hybrid 的问题**：脚本里的 `SYS=/sys/class/power_supply/battery` 在 chroot 内执行时，相对路径变成 `/data/debian/sys/...`（chroot 根），挂不到宿主。

**修法（如果要修）**：在宿主环境（非 chroot）执行 bind mount，或改用绝对路径。

**实测：恢复 fuelgauged 后 capacity 回正常**
```
fuelgauged=1
capacity=83
SSH_OK
```

### 14.8 fuelgauged 在 hybrid 里是需要的（batt-display 挂错路径接管不了）

**之前判断错误**：以为 batt-display 能接管电量显示，可以不跑 fuelgauged。

**实测纠正**：

| 场景 | capacity | fuelgauged | batt-display |
|---|---|---|---|
| 跑 fuelgauged | 83 | 1 | RUNNING |
| 停 fuelgauged | -1（错） | 0 | RUNNING（但挂错路径） |
| 恢复 fuelgauged | 83 | 1 | RUNNING |

**batt-display 在 hybrid 挂错路径**：
- 脚本里 `SYS=/sys/class/power_supply/battery`
- chroot 内执行时相对路径 → `/data/debian/sys/devices/platform/battery/...`（chroot 内）
- 宿主的 `/sys/class/power_supply/battery/capacity` 没被覆盖
- 结果：capacity = -1

**结论**：**fuelgauged 在 hybrid 里是必需的**（batt-display 挂错路径接管不了）。

**修 batt-display 挂错路径的方案**（如果真想不跑 fuelgauged）：
1. **在宿主环境执行 bind mount**（不在 chroot 内）
2. **改用绝对路径**：把 `SYS=/sys/class/power_supply/battery` 改成 `SYS=/data/debian/sys/class/power_supply/battery`
3. **或者接受 fuelgauged 必需**（当前方案，简单可靠）

**当前选择**：保留 fuelgauged（01-fuelgauged.sh 钩子），因为 batt-display 挂错路径、修起来麻烦。

### 14.9 结论：sysfs 是 TWRP UI 的通解，但 fuelgauged 在 hybrid 必需

**TWRP UI 层**：直接读 sysfs/rtc（不依赖 fuelgauged）→ 可行。

**系统层**：batt-display 挂错路径（chroot 相对路径），接管不了宿主 sysfs → **fuelgauged 必需**。

---

*本篇是 MTK 驱动 + 省电调度逆向。背景与路线见篇1，TWRP 镜像修补技术见篇2，hybrid 全自动钩子实战见篇4。*

---

## 十五、8 核压测（验证 HPS 自动开核）

### 15.1 TWRP 下 8 核可用（不只是小核）

**之前发现**：TWRP 不跑框架，cpu4-7 睡死（只有 cpu0 活）。

**压测验证**：**给负载，HPS 自动开所有核**。

| 阶段 | CPU online | 负载 | 温度 | 频率 |
|---|---|---|---|---|
| 空闲 | cpu0（1 核） | ~0 | 35°C | 小核待机 |
| 压测中 | cpu0-7（8 核） | 14.93 | 76.5°C | 小核 1GHz + 大核 1.8GHz |
| 峰值 | 8 核 | 15.98 | 82.7°C | 全速 |
| 停后 10 秒 | cpu0（1 核） | 降 | 42.5°C | HPS 关大核 |

### 15.2 HPS 自动开核（不是手动唤醒）

**之前误判**：以为要手动唤醒 cpu1-7（循环写 `/sys/devices/system/cpu/cpuN/online=1`）。

**实际**：**HPS 根据负载自动开核**（压测一上 loadavg 14.93，cpu0-7 全开）。

**手动唤醒的问题**：
- HPS 主线程（hps_main，pid 114）会**顶回去关核**
- 循环唤醒对抗 HPS 是**错的做法**（HPS 是 MTK 热插拔调度器，它管这个）

**正确做法**：**给足够负载，让 HPS 自己开核**（压测脚本 → loadavg 高 → HPS 开核）。

### 15.3 压测脚本

```bash
# 8 核压测（每核 1 个死循环）
for i in 0 1 2 3 4 5 6 7; do
    (while :; do :; done) > /dev/null 2>&1 &
done

# 查看 online
for i in 0 1 2 3 4 5 6 7; do
    echo -n "cpu$i:$(cat /sys/devices/system/cpu/cpu$i/online) "
done

# 负载
cat /proc/loadavg

# 停止
pkill -9 -f 'while'
```

### 15.4 温度控制

**8 核满载温度**：
- 压测中 76.5°C
- 峰值 82.7°C（超过 80°C 阈值）

**停后 10 秒**：42.5°C（降 40 度，散热快）。

**意义**：8 核满载会触发温控（82.7°C 接近 MTK 降频阈值），但 HPS + 温控会自动管理（降频/关核）。

### 15.5 意义

**TWRP 环境下 8 核全功能**（不只是小核），验证 hybrid recovery 方案的 CPU 性能：
- **8 核全开**（loadavg 14.93 → HPS 自动开所有核）
- **大核 1.8GHz + 小核 1GHz**（全速运行）
- **温度控制**（HPS + 温控自动管理，不会过热）
- **8 核压测完成**（hybrid recovery 支持 8 核满载）

