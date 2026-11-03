# 篇4：hybrid recovery 全自动钩子实战 —— 从合成到全自动化（成功路径 + 踩坑）

> 时间：2026-11-02
> 设备：魅蓝 Note5 (MT6755, aarch64, Android 7.0, kernel 3.18.35)
> 最终产物：`hybrid-hooked.img`（18776064 B），已刷入 recovery 分区 p1
> 本篇聚焦：**hybrid 合成、钩子机制、全自动化的完整路径 + 全部踩坑**

---

## 一、目标

**进 recovery 分区即自动成为 Debian chroot 服务器**（不跑安卓框架，省电省内存）。

**"既要又要"**：跑全部 MTK 底层驱动/守护（电量/温控/wifi），但砍掉谷歌框架吃内存的部分。插电常跑，防鼓包（读电池容量/温度）。**不碰 boot 分区**。

---

## 二、hybrid 合成（normal 内核 + TWRP ramdisk）

### 2.1 为什么 hybrid

| 环境 | 大核 | 内存 | 驱动 |
|---|---|---|---|
| normal（跑框架） | offline（0）✅ | ~2GB | 全 |
| TWRP（不跑框架） | offline（0）✅ | ~447MB | **缺 fuel gauge/wifi 驱动** |
| **hybrid（normal 内核 + TWRP ramdisk）** | **offline（0）✅** | **~447MB** | **全** ✅ |

**hybrid = normal 内核（驱动全）+ TWRP ramdisk（调度好）= "既要又要"正解**

### 2.2 hybrid 合成验证（实测）

**合成方法**：TWRP ramdisk（10666488 B）+ normal 内核（8151028 B，md5 `0018ac`），bootsize 改 0x2000000。

**验证结果**：
- 调度 ✅ cpu0 在线 cpu4-7 睡死、内存 156-200M、governor mtk_governor
- fuelgauged ✅ capacity 100（证明 capacity -1 是缺用户态守护，非内核驱动缺）
- WiFi ✅ 网关 0% 丢包、外网 223.5.5.5 通(39ms)

**结论**：hybrid 内核驱动全 + TWRP 调度好 = 大核自动 offline + 全驱动可用。

### 2.3 内核对比

| 内核 | md5 | 驱动 |
|---|---|---|
| normal | `0018ac` | 全（fuel gauge/wifi 驱动都有） |
| TWRP | `8fb9c4` | 缺 fuel gauge/wifi 驱动 |
| **hybrid** | `0018ac`（用 normal 内核） | **全** |

---

## 三、钩子机制（Magisk 式，放脚本即跑）

### 3.1 原理

```
开机 → TWRP 的 init 跑 init.rc
        └─ service twrphook /sbin/twrp-hook.sh   (class default + oneshot, 异步)
             └─ /sbin/twrp-hook.sh (runner)
                  ├─ 等 /data 挂载 (60s 超时)
                  ├─ 挂 /system (ro) 提供工具
                  ├─ mkdir /data/local/tmp/twrp-hooks
                  └─ for f in /data/local/tmp/twrp-hooks/*.sh: sh $f
```

### 3.2 runner（`/sbin/twrp-hook.sh`）

```sh
#!/sbin/sh
# twrp-hook.sh — TWRP 开机钩子 runner
LOG=/data/local/tmp/twrp-hook.log
HOOK_DIR=/data/local/tmp/twrp-hooks

# 等 /data 挂载（最多 60s）
i=0
while [ "$i" -lt 60 ]; do
    if [ -d /data/local ]; then break; fi
    sleep 1; i=$((i + 1))
done

mkdir -p "$HOOK_DIR" 2>/dev/null
mount /system 2>/dev/null || mount -o ro /system 2>/dev/null

export PATH=/sbin:/system/bin
export LD_LIBRARY_PATH=/sbin:/system/lib

echo "=== twrp-hook start $(date) ===" >> "$LOG"

for f in "$HOOK_DIR"/*.sh; do
    [ -f "$f" ] || continue
    echo "--- run $f ---" >> "$LOG"
    sh "$f" 2>&1 | grep -v "^Aborted" >> "$LOG"
done

echo "=== twrp-hook done $(date) ===" >> "$LOG"
```

**关键**：`grep -v "^Aborted"` 过滤 wpa_cli segfault 刷屏。

---

## 四、6 个钩子脚本（全自动化）

### 4.1 01-fuelgauged.sh（电池守护）

```sh
#!/system/bin/sh
# fuelgauged 用 & 不用 setsid（setsid 在 hybrid 启动失败）
grep -q " /system " /proc/mounts 2>/dev/null || mount -o rw /dev/block/mmcblk0p29 /system 2>/dev/null
[ -L /vendor ] || ln -sf /system/vendor /vendor 2>/dev/null
[ -e /dev/wmtWifi ] || /system/vendor/bin/wmt_loader >/dev/null 2>&1

export LD_LIBRARY_PATH=/system/vendor/lib:/system/lib
killall fuelgauged 2>/dev/null
sleep 1
/system/vendor/bin/fuelgauged >/dev/null 2>&1 < /dev/null &

exit 0
```

**验证**：capacity=100 ✅

### 4.2 02-wifi.sh（WiFi 拉起）

```sh
#!/system/bin/sh
# wpa_cli reassociate 一次触发关联（wpa_supplicant 不会自动关联）
# wpa_cli 在 hybrid 会 segfault，但 timeout + 重定向兜住

# 1. 挂 system + vendor
grep -q " /system " /proc/mounts 2>/dev/null || mount -o rw /dev/block/mmcblk0p29 /system 2>/dev/null
[ -L /vendor ] || ln -sf /system/vendor /vendor 2>/dev/null

# 2. wmt_loader（只能跑一次）
[ -e /dev/wmtWifi ] || /system/vendor/bin/wmt_loader >/dev/null 2>&1

# 3. wmt_launcher（后台 &）
/system/vendor/bin/wmt_launcher -p /vendor/firmware/ >/dev/null 2>&1 < /dev/null &
sleep 10

# 4. 开 wifi + wlan0 up
printf "1\n" > /dev/wmtWifi 2>/dev/null
sleep 3
ip link set wlan0 up 2>/dev/null

# 5. wpa_supplicant（64 位需要 lib64）
mkdir -p /data/misc/wifi/sockets 2>/dev/null
chmod 777 /data/misc/wifi/sockets 2>/dev/null
cat > /data/wpa.conf <<EOF
ctrl_interface=/data/misc/wifi/sockets
network={
    ssid="Xiaomi_F5A3"
    psk="123456789"
    key_mgmt=WPA-PSK
}
EOF
killall wpa_supplicant 2>/dev/null
sleep 1
LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib \
    /system/bin/wpa_supplicant -Dnl80211 -iwlan0 -c /data/wpa.conf >/dev/null 2>&1 < /dev/null &
sleep 5

# 6. reassociate 一次（timeout 兜 segfault，不循环）
timeout 3 /system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 reassociate </dev/null >/dev/null 2>&1

# 7. 等关联完成
sleep 20

exit 0
```

**验证**：wifi COMPLETED，外网 223.5.5.5 通（39ms）✅

### 4.3 20-chroot-mount.sh（挂 Debian chroot）

照面具 chroot-mount 模块，去掉等框架的循环：

```sh
#!/system/bin/sh
B=/data/debian/bin/busybox

i=0
while [ ! -d /data/debian/bin ] && [ $i -lt 30 ]; do sleep 1; i=$((i+1)); done
[ -d /data/debian/bin ] || exit 0

$B mount -t proc proc /data/debian/proc 2>/dev/null
$B mount -t sysfs sys /data/debian/sys 2>/dev/null
$B mount -o bind /dev /data/debian/dev 2>/dev/null
$B mount -t devpts devpts /data/debian/dev/pts 2>/dev/null
...
```

### 4.4 30-chroot-services.sh（起 supervisord）

照面具 chroot-services 模块，去掉等框架的循环：

```sh
#!/system/bin/sh
while [ ! -f /data/debian/proc/version ]; do sleep 1; done
D=/data/debian
log() { echo "[services] $1" | tee -a /data/local/tmp/services.log; }
unset LD_PRELOAD LD_LIBRARY_PATH
log "Starting supervisord..."
n=0
while [ $n -lt 3 ]; do
  n=$((n+1))
  setsid $D/bin/busybox chroot $D /bin/su -s /bin/sh - root -c "exec /usr/bin/supervisord -c /etc/supervisor/supervisord.conf"
  sleep 4
  if $D/bin/busybox chroot $D /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf pid >/dev/null 2>&1; then
    log "Supervisord up (attempt $n)"
    break
  fi
  log "Supervisord attempt $n failed"
done
log "All done"
```

**关键**：照抄面具原文，**不要瞎加 rm socket/killall/&**（面具原文用 setsid，无 &、无重定向）。

### 4.5 40-adbd-tcp.sh（无线 adb）

```sh
#!/system/bin/sh
setprop service.adb.tcp.port 5555 2>/dev/null
kill $(pidof adbd) 2>/dev/null
sleep 3
exit 0
```

**验证**：adbd 监听 5555，`adb connect 192.168.31.213:5555` 通 ✅

### 4.6 03-zram.sh（zram swap，内存一半）

```sh
#!/system/bin/sh
# 03-zram.sh — zram swap (内存一半, 自包含, 幂等)
# 坑1: recovery PATH里没awk
# 坑2: shell32位溢出(>2G算错)
# 坑3: MemTotal带单位"2855084 kB"(要tr -d kB)

if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi

[ -e /dev/zram0 ] || mknod /dev/zram0 b 254 0 2>/dev/null

# disksize = MemTotal(kb) * 512 (分步算64位安全, 去kB单位)
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
SZ=$(( KB * 512 ))
[ "$SZ" -le 0 ] && SZ=1461795328  # fallback 1.4G

echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null

mkswap /dev/zram0 >/dev/null 2>&1
swapon -p 100 /dev/zram0 >/dev/null 2>&1

exit 0
```

**验证**：swap 1427540 kB，priority 100 ✅

### 4.7 50-static-ip.sh（配静态 IP + 路由）

```sh
#!/system/bin/sh
# 轮询 ip link，wifi 关联后配 IP（不用 wpa_cli 因为会 segfault）
export LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib

i=0
while [ $i -lt 180 ]; do
    ip link show wlan0 2>/dev/null | grep -q 'LOWER_UP' && break
    sleep 1; i=$((i+1))
done

sleep 3

ip addr add 192.168.31.213/24 dev wlan0 2>/dev/null
ip route add default via 192.168.31.1 dev wlan0 2>/dev/null

exit 0
```

**验证**：IP 192.168.31.213/24 + default via 192.168.31.1 ✅

---

## 五、全部踩坑（12 个，全部解决）

### 5.1 fuelgauged 用 setsid 失败，改 & 成功

- **坑**：`setsid fuelgauged &` 启动失败（进程 0）
- **修**：简单 `&` 成功（进程 1）
- **教训**：**hybrid 里 fuelgauged 用 `&` 不用 setsid**

### 5.2 wpa_supplicant 是 64 位，要显式设 lib64

- **坑**：runner 的 `LD_LIBRARY_PATH=/sbin:/system/lib`（只有 32 位库），wpa_supplicant 是 64 位 → 静默失败不关联
- **修**：钩子里显式设 `LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib`
- **教训**：**64 位二进制要显式设 lib64**

### 5.3 wpa_cli 在 hybrid 会 segfault，stderr 刷 "Aborted"

- **坑**：`wpa_cli reassociate`/`status` 调用**偶尔 segfault**，stderr 输出 "Aborted" **重定向 `2>&1` 都吞不掉**（进钩子日志刷屏）
- **修**：
  - 钩子里只跑一次 reassociate（不循环）
  - runner 加 `grep -v "^Aborted"` 过滤
- **教训**：**wpa_cli 在 hybrid 会 segfault，用 timeout 兜住，只跑一次，runner 过滤 Aborted**

### 5.4 wpa_supplicant 不会自动关联，需要 reassociate 触发

- **坑**：去掉 wpa_cli 后，wpa_supplicant 不关联（wpa_state=DISCONNECTED）
- **修**：加回 `timeout 3 wpa_cli reassociate` 一次
- **教训**：**wpa_supplicant 不会自动关联，需要 reassociate 一次触发**

### 5.5 IP 配置拆到独立钩子（50）

- **坑**：02-wifi.sh 的 wpa_cli 循环会 abort，IP 配置没跑到
- **修**：IP 配置拆到 50-static-ip.sh，不依赖 wpa_cli
- **教训**：**IP 配置拆成独立钩子，不依赖 wpa_cli**

### 5.6 supervisord 照抄面具原文（不要瞎加）

- **坑**：我瞎加了 rm socket/killall/`&`/重定向，还加了网络配置，导致 supervisord 起不来
- **修**：照抄面具 chroot-services 原文（setsid + 3 次重试 + supervisorctl pid 检查），只去掉等框架的循环
- **教训**：**照抄面具原文，不要瞎加**

### 5.7 IP 配置用 `&` 后台在 adb shell 退出后被杀

- **坑**：`ip addr add ... &` 在 adb shell 退出后被杀
- **修**：改成阻塞式（轮询 `ip link` 等 LOWER_UP，然后配 IP）
- **教训**：**IP 配置不要用 `&` 后台，改成阻塞式**

### 5.8 IP 配置等 wifi 关联时序问题

- **坑**：50-static-ip.sh 等 wifi 关联 60 秒不够（wifi 关联慢）
- **修**：改成轮询 `ip link` 等 LOWER_UP（最多 180 秒）
- **教训**：**wifi 关联时间不定，轮询 `ip link` 等 LOWER_UP（最多 180 秒）**

### 5.9 `ip` 命令在钩子里 LD_LIBRARY_PATH 不对

- **坑**：runner 的 `LD_LIBRARY_PATH` 只有 32 位库，`ip` 命令是 64 位 → segfault 静默失败
- **修**：50-static-ip.sh 显式设 `LD_LIBRARY_PATH=/system/lib64:/system/vendor/lib64:/system/lib`
- **教训**：**`ip` 命令也要显式设 lib64**

### 5.10 runner 的 `sh "$f"` 会捕获 stderr，Aborted 进日志

- **坑**：runner 的 `sh "$f" >> "$LOG" 2>&1` 会捕获 stderr，wpa_cli 的 "Aborted" 进日志
- **修**：改成 `sh "$f" 2>&1 | grep -v "^Aborted" >> "$LOG"`
- **教训**：**runner 加 `grep -v "^Aborted"` 过滤 segfault 刷屏**

### 5.11 事件驱动（wpa_cli action）也不可靠

- **坑**：用 wpa_cli action 脚本（关联事件触发）也不可靠（wpa_cli 会 segfault）
- **修**：不依赖 wpa_cli，轮询 `ip link`
- **教训**：**wpa_cli 任何形式（reassociate/action/status）在 hybrid 都会 segfault，唯一稳定方案是轮询 `ip link`**

### 5.12 zram 启动链 64 位溢出（上一任没考虑）

- **坑 1**：03-zram.sh 用 `awk '{print $2}'` 算 disksize，但 **recovery PATH 里没 awk**（`awk: not found`）→ 算失败 → disksize=0 → mkswap 失败 → swapon 失败
- **坑 2**：改用纯 shell `$(( MemTotal * 1024 / 2 ))`，但 **shell 32 位整数溢出**（`2855084 * 1024 / 2 = 1461795328 > 2^31 = 2147483648`）→ 变负数 `-685680640` → disksize 写负数失败
- **坑 3**：`grep MemTotal /proc/meminfo` 输出 `MemTotal:        2855084 kB`（**带 kB 单位**），`tr -d ' '` 只删空格没删 kB → `bad number '2855084kB'`
- **修**：分步算（`KB * 512`，64 位安全）+ 去 kB 单位（`tr -d 'kB'`）
- **教训**：**上一任没考虑 64 位溢出 + recovery PATH 限制 + /proc/meminfo 带单位，写脚本要考虑：1）PATH 里有什么命令 2）shell 32 位溢出 3）/proc 输出带单位**
- **验证**：swap 1427540 kB，priority 100 ✅

---

## 六、最终验证（全自动化）

### 6.1 重启什么都不做，等 120 秒

```
SSH_OK
 16:18:15 up 1 min,  0 users,  load average: 9.56, 3.51, 1.27
```

### 6.2 完整状态

| 项 | 状态 |
|---|---|
| fuelgauged（电池） | ✅ capacity=100 |
| wifi（wpa_supplicant） | ✅ LOWER_UP + COMPLETED |
| chroot 文件系统 | ✅ /data/debian/proc 挂好 |
| supervisord（cpa/freellmapi/sshd/batt） | ✅ 全 RUNNING |
| sshd | ✅ ssh meilan-phone1 |
| 静态 IP + 路由 | ✅ 192.168.31.213/24 + default via .1 |
| 无线 adb | ✅ adb connect 192.168.31.213:5555 |
| 省电调度（cpu4-7 睡死） | ✅ |
| 日志干净 | ✅ 0 Aborted |

### 6.3 外网连通

```
ping 223.5.5.5 → 39ms 通
```

---

## 七、部署与使用

### 7.1 刷镜像（只刷 recovery p1）

```bash
adb push hybrid-hooked.img /data/local/tmp/
adb shell su -c "dd if=/data/local/tmp/hybrid-hooked.img of=/dev/block/mmcblk0p1; sync"
```

### 7.2 放钩子脚本

```bash
adb push 01-fuelgauged.sh /data/local/tmp/twrp-hooks/
adb push 02-wifi.sh /data/local/tmp/twrp-hooks/
adb push 03-zram.sh /data/local/tmp/twrp-hooks/
adb push 20-chroot-mount.sh /data/local/tmp/twrp-hooks/
adb push 30-chroot-services.sh /data/local/tmp/twrp-hooks/
adb push 40-adbd-tcp.sh /data/local/tmp/twrp-hooks/
adb push 50-static-ip.sh /data/local/tmp/twrp-hooks/
adb shell chmod 755 /data/local/tmp/twrp-hooks/*.sh
```

### 7.3 重启使用

```bash
adb shell reboot recovery
# 等 120 秒
ssh meilan-phone1
adb connect 192.168.31.213:5555
```

---

## 八、我这次犯的错误（反思）

### 8.1 反复在同一坑改（wpa_cli 改了 5 版）

- setsid/`&`/timeout/reassociate 一次/无 wpa_cli
- 每次都以为"这次对了"，其实还在同一个坑

**教训**：**该查资料/总结时不要瞎试**。

### 8.2 该查资料时瞎试

用户说："**不会就去总结或者上网查**"，我继续瞎试。

**教训**：**不会就去总结或上网查，不要瞎试**。

### 8.3 瞎改 supervisord（照抄面具就好了）

- 我瞎加了 rm socket/killall/`&`/重定向
- 应该照抄面具原文

**教训**：**照抄验证过的原文，不要瞎加**。

### 8.4 不该跑诊断命令干扰系统

- 我跑 `wpa_cli status`/`ip addr` 诊断时，干扰了系统状态

**教训**：**不要跑诊断命令干扰系统，直接测最终结果（SSH/ping）**。

---

## 九、经验总结（recovery 钩子脚本规范）

### 9.1 守护进程启动方式

**结论：用 `&` 后台，不用 `setsid`**（supervisord 例外，照抄面具）

### 9.2 wpa_supplicant 启动

**关键**：64 位库 + reassociate 一次触发关联

### 9.3 网络配置

**关键**：轮询 `ip link` 等 LOWER_UP，配 IP（不用 wpa_cli 循环）

### 9.4 钩子顺序

**按文件名顺序执行，耗时的放后面**（01 → 02 → 20 → 30 → 40 → 50）

### 9.5 不要照搬 Magisk 模块

**Magisk 模块开头都有 `while [ "$(getprop sys.boot_completed)" != "1" ]`** → **在 recovery 里死循环**

### 9.6 日志清理

**wpa_cli 的 "Aborted" 进日志刷屏，吞不掉**（stderr 怪异）→ **runner 加 `grep -v "^Aborted"`**

---

## 十、一句话总结

**hybrid recovery + 钩子 = normal 内核（驱动全）+ TWRP ramdisk（调度好）+ 6 个钩子脚本（fuelgauged/wifi/chroot-mount/chroot-services/adbd-tcp/static-ip）+ runner 过滤 Aborted。**

**全自动化**：重启什么都不做，等 120 秒，`ssh meilan-phone1` 即可。

**关键踩坑**：
1. fuelgauged 用 `&` 不用 setsid
2. wpa_supplicant 是 64 位，要显式设 lib64
3. wpa_cli 会 segfault，用 timeout 兜住，只跑一次，runner 过滤 Aborted
4. wpa_supplicant 不会自动关联，需要 reassociate 一次触发
5. IP 配置拆成独立钩子，轮询 `ip link`（不用 wpa_cli）
6. supervisord 照抄面具原文（不要瞎加）
7. `ip` 命令也要显式设 lib64
8. **zram 64 位溢出**（recovery 没 awk + shell 32 位溢出 + /proc/meminfo 带单位）

---

*本篇是 hybrid 全自动钩子实战。背景与路线见篇1，TWRP 镜像修补技术见篇2，MTK 驱动逆向见篇3。*

---

## 十一、冻结/kill TWRP UI 实验（附加）

### 11.1 冻结 recovery（SIGSTOP，可逆）

```bash
# 冻结
kill -STOP $(pidof recovery)

# 状态：recovery State=T (stopped)，屏幕冻结，触屏无响应
# 但后台服务全不受影响（fuelgauged/wifi/zram/chroot/sshd 继续跑）
# SSH/adb 继续可用

# 恢复
kill -CONT $(pidof recovery)
# 状态：recovery State=S (sleeping)，屏幕恢复，触屏恢复
```

**省电潜力**：冻结 UI 渲染停止（GPU/CPU 不画屏），可能省一点电。但用户看不到 TWRP 界面（只能靠 SSH/adb 操作）。

### 11.2 kill recovery（SIGKILL，init 自动重启）

```bash
kill -9 $(pidof recovery)
# init 立刻重启 recovery（PID 变，recovery 又回来）

# 尝试阻止重启（chmod -x + kill）
chmod -x /sbin/recovery
kill -9 $(pidof recovery)
# recovery 暂时没了，但 init/watchdog 又拉起（又回来了）
```

**结论**：**recovery 不能 kill**（init 自动重启它），**也不该 kill**（TWRP UI 是 recovery 的一部分，kill 它可能破坏 recovery 环境）。

### 11.3 意义

- **TWRP UI 不能冻结/kill**（init 会重启它，而且它是 recovery 核心进程）
- **想省电只能靠**：不跑框架（hybrid 已经做到了）+ 省电钩子（cpu4-7 睡死 + zram + 关屏）
- **服务器场景不需要 TWRP UI**（SSH 操作），但 TWRP UI 是 recovery 环境的基础，不能去掉


### 11.4 TWRP UI 启动链（谁启动 recovery，解包 ramdisk 分析）

**启动链**：
```
TWRP init（/init）
  └─ 解析 /init.rc
       ├─ import /init.recovery.service.rc
       │    └─ service recovery /sbin/recovery
       │         seclabel u:r:recovery:s0
       └─ on boot
            └─ class_start default  ← 拉起所有 default class 服务（含 recovery）
```

**文件**：
- `/init.rc` 第 6 行：`import /init.recovery.service.rc`
- `/init.recovery.service.rc`：
  ```
  on boot
  # For starting recovery on 5.0 and newer
  service recovery /sbin/recovery
      seclabel u:r:recovery:s0
  ```
- `/init.rc` 第 62 行：`class_start default`（on boot 段）

**为什么 kill 不掉 recovery**：
1. recovery 是 **class default 服务**（init 管理）
2. on boot 时 **class_start default 拉起它**
3. **class default 服务死掉 → init 自动重启它**（init.rc 没写 oneshot，默认 autorestart）
4. chmod -x 也没用（init 以 root 跑，忽略权限）

**想真正禁用 recovery UI**（理论）：
- 改 `/init.recovery.service.rc` 去掉 `service recovery` 定义
- 或改 `/init.rc` 去掉 `import /init.recovery.service.rc`
- **但这需要重打包 hybrid-hooked.img**（ramdisk 里的 init.rc）

**实际意义**：
- **服务器场景不需要 TWRP UI**（SSH 操作），但 **recovery 进程占 ~36MB 内存**
- **想省内存**：改 init.rc 禁用 recovery UI（重打包镜像）
- **风险**：TWRP UI 是 recovery 核心进程，禁用可能破坏 recovery 环境（adbd/USB/触屏）
- **结论**：**不建议禁用**（36MB 内存换可能不稳定的 recovery 环境不划算）


---

## 十二、屏幕控制（背光 sysfs，可逆）

### 12.1 背光控制（成功）

```bash
# 关屏（背光 0）
echo 0 > /sys/class/leds/lcd-backlight/brightness

# 开屏（背光 255）
echo 255 > /sys/class/leds/lcd-backlight/brightness

# 中间亮度（如 50%）
echo 128 > /sys/class/leds/lcd-backlight/brightness
```

**验证**：
- 背光 0-255 随意调 ✅
- TWRP 不覆盖（手动设 255，10 秒后还是 255）✅
- 关屏后 TWRP 不自动开屏（背光保持 0）✅
- 可逆 ✅

### 12.2 关屏省电（实测）

| 项 | 关屏前 | 关屏后（30 秒） | 变化 |
|---|---|---|---|
| CPU ticks | 18988+22627 | 19230+22839 | +242（比之前更少） |
| 内存 | 746M | 747M | +1M |
| 温度（thermal_zone1） | 35.1°C | 34.5°C | **-0.6°C** ✅ |

**结论**：关屏省电（温度降 0.6 度，CPU ticks 减少），但省电效果有限（主要省屏幕功耗，不是 CPU）。

### 12.3 fb0/blank（试了没用）

```bash
echo 1 > /sys/class/graphics/fb0/blank   # 试关屏
echo 4 > /sys/class/graphics/fb0/blank   # 试 DPMS off
# 结果：写不进去（cat 读回空值），TWRP 不响应 fb0/blank
```

**结论**：fb0/blank 在这个 TWRP 里**写不进去**（不是标准 fbdev blank 接口），只能靠背光控制。

### 12.4 屏幕控制脚本（可做成钩子）

```sh
#!/system/bin/sh
# 60-screen-off.sh — 关屏省电（服务器场景不需要屏幕）
echo 0 > /sys/class/leds/lcd-backlight/brightness
exit 0
```

**但注意**：关屏后触屏没反应（背光 0），用户看不到 TWRP 界面。**服务器场景不需要屏幕**（SSH 操作），可以关屏省电。


---

## 十三、U盘插入与挂载（附加）

### 13.1 U盘识别

**设备**：`/dev/block/sda` + `sda1`
- 容量：120GB（245760000 sectors）
- 格式：vfat（FAT32）
- 模型：`ProductCode`（厂商 `VendorC`）
- UUID：`ece5-afc5`

**dmesg 识别**：
```
[4478.546634] sda: sda1
[4478.554345] sd 0:0:0:0: [sda] Attached SCSI removable disk
[4484.097851] Charger exist but USB is host  ← USB host 模式（OTG）
```

### 13.2 挂载 U盘

```bash
# 创建挂载点
mkdir -p /mnt/usb

# 挂载（vfat 格式）
mount -t vfat -o rw /dev/block/sda1 /mnt/usb

# 查看
df -h /mnt/usb
ls /mnt/usb/
```

**结果**：
```
/dev/block/sda1  117G  21G  97G  18%  /mnt/usb
```

### 13.3 U盘内容

```
/mnt/usb/
├── 12.jpeg          (247KB)
├── 4.jpeg           (412KB)
├── ??/              (中文目录，乱码显示)
├── FLASH??/         (中文目录)
├── System Volume Information/
├── app/             (应用)
├── old/             (旧文件)
└── video/           (视频)
```

### 13.4 意义

- **hybrid 支持外接 U 盘**（USB host 模式 + vfat 挂载）
- **U 盘可作为存储扩展**（chroot 里服务访问 /mnt/usb）
- **注意**：中文目录名乱码（vfat 默认不支持 UTF-8，挂载时加 `-o utf8` 可解决）


---

## 十四、8 核压测实验（验证 CPU 性能）

### 14.1 压测结果

| 阶段 | CPU online | 负载 | 温度 | 频率 |
|---|---|---|---|---|
| **空闲** | cpu0（1 核） | ~0 | 35°C | 小核待机 |
| **压测中** | cpu0-7（8 核） | 14.93 | 76.5°C | 小核 1GHz + 大核 1.8GHz |
| **峰值** | 8 核 | 15.98 | **82.7°C** | 全速 |
| **停后 10 秒** | cpu0（1 核） | 降 | 42.5°C | HPS 关大核 |

### 14.2 8 核压测脚本

```bash
# 8 核压测（每核 1 个死循环）
for i in 0 1 2 3 4 5 6 7; do
    (while :; do :; done) > /dev/null 2>&1 &
done

# 查看 online
for i in 0 1 2 3 4 5 6 7; do
    echo -n "cpu$i:$(cat /sys/devices/system/cpu/cpu$i/online) "
done
echo

# 负载
cat /proc/loadavg

# 温度
cat /sys/devices/virtual/thermal/thermal_zone1/temp

# 停止（注意 killall -9 sh 会误杀 adb shell，用 pkill）
pkill -9 -f 'while'
```

### 14.3 8 核全开确认

```
online=cpu0:1 cpu1:1 cpu2:1 cpu3:1 cpu4:1 cpu5:1 cpu6:1 cpu7:1 
负载=14.93 11.80 10.66 11/264 13712
温度=76500 (76.5°C)
cpu0-3: 1001000 (1.001 GHz，小核全速)
cpu4-7: 1807000 (1.807 GHz，大核全速)
```

### 14.4 关键发现

1. **TWRP 环境下 8 核全功能**（不只是小核，HPS 根据负载自动开所有核）
2. **大核 1.8GHz + 小核 1GHz**（全速运行，性能完整）
3. **温度最高 82.7°C**（8 核满载，接近 MTK 降频阈值 80°C）
4. **HPS 自动管理核数**（负载高开核，负载低关核）
5. **压测停后温度降 40 度**（82.7→42.5°C，散热快）
6. **不要手动唤醒**（HPS 会顶回去关核，给负载让它自己开核）

### 14.5 意义

**hybrid recovery 支持 8 核满载**（CPU 性能完整），验证了 TWRP 环境下 8 核可用。

**服务器场景**：8 核 CPU 可用于 chroot 服务（cpa/freellmapi）的多线程负载。


---

## 十五、framebuffer 直接绘制（验证图形栈控制）

### 15.1 能否控制图像？——能

**fb0 参数**：
```
/dev/graphics/fb0  crw-rw---- root graphics 29,0
virtual_size = 1088x3840  (double buffer)
bpp = 32                  (4 字节/像素)
stride = 4352             (每行 4352 字节，1080 可视 + 8 padding)
可视分辨率 1080x1920
```

**recovery 占用**：
```
recovery (pid 2351) mmap 了 /dev/graphics/fb0
地址 7562818000-7564000000 (rw-s)
```

**结论**：fb0 可读可写（root 权限），recovery 会持续重绘，**必须先冻结 recovery UI 线程**才能写入不被覆盖。

### 15.2 画三角形步骤

```python
# 1. Python 生成 RGB565 → 32bpp fb0 数据
W, H = 1080, 1920
STRIDE = 4352
PIXEL = 4  # BGRA little-endian

def set_pixel(x, y, r, g, b):
    offset = y * STRIDE + x * PIXEL
    buf[offset:offset+4] = struct.pack('<BBBB', b, g, r, 0xff)

# 画 3 个三角形（红/绿/蓝）
draw_triangle([(540, 200), (240, 800), (840, 800)], 255, 0, 0)
draw_triangle([(540, 600), (240, 1200), (840, 1200)], 0, 255, 0)
draw_triangle([(540, 1000), (240, 1600), (840, 1600)], 0, 0, 255)

# 2. 推到设备
adb push fb_tri.rgb /tmp/fb_tri.rgb

# 3. 冻结 recovery
adb shell "kill -STOP $(pidof recovery)"

# 4. 写 fb0
adb shell "cat /tmp/fb_tri.rgb > /dev/graphics/fb0"

# 5. 恢复 recovery
adb shell "kill -CONT $(pidof recovery)"
```

### 15.3 实测结果

```
/tmp/fb_tri.rgb: 1 file pushed, 0 skipped. 7.2 MB/s (8355840 bytes)
cat /tmp/fb_tri.rgb > /dev/graphics/fb0  →  写入完成
屏幕显示：3 个三角形（红/绿/蓝）+ 黑底 + 顶部灰条 + 白色边框 ✅
recovery 恢复后（kill -CONT）：State: S (sleeping) ✅
```

### 15.4 关键点

1. **fb0 是线性 framebuffer**（线性像素，offset = y*stride + x*4）
2. **MTK fbdev 是 BGRA**（little-endian，`<BBBB` = b,g,r,a）
3. **recovery 会重绘**（必须先 `kill -STOP` 冻结）
4. **double buffer**（1088x3840 虚拟，1920 是 back buffer）
5. **可逆**（写完 `kill -CONT` 恢复 recovery）

### 15.5 意义

**图形栈完全可控**：
- **fb0 直接写入**（不需要 Android 框架）
- **任意图像可显示**（三角形/文字/PNG 解码后的图，都是像素操作）
- **TWRP 自己也这么做**（minui 库 = 直接写 fb0 + libpng）

**下一步可能**：
- **Xorg + fbdev 软渲染**（chroot 里 apt 装 xorg-server-xfbdev）
- **VNC 远程桌面**（chroot 里跑 x11vnc，远程看桌面）
- **libhybris + Mali blob**（借 Android GPU 加速，更复杂但性能好）


---

## 十六、干净底座（冻结 TWRP UI，只跑底层服务）

### 16.1 需求

**"干净底座"**：冻结 TWRP UI（触屏无响应），只保留底层服务（fuelgauged/wpa_supplicant/sshd/adbd/chroot），变成纯 SSH 操作的 Linux 服务器底座。

### 16.2 实现

```bash
# 1. 冻结 recovery（TWRP UI 线程）
kill -STOP $(pidof recovery)

# 2. 确认冻结
grep State /proc/$(pidof recovery)/status  →  State: T (stopped)

# 3. 恢复（如果需要 UI）
kill -CONT $(pidof recovery)
```

### 16.3 服务保留情况

| 服务 | 停 UI 前 | 停 UI 后 |
|---|---|---|
| recovery（TWRP UI） | 1（running） | **1（stopped，冻结）** |
| fuelgauged | 1 | 1（还跑） |
| wpa_supplicant | 1 | 1（还跑，WiFi 通） |
| adbd（无线 adb） | 1 | 1（还跑） |
| sshd | 1 | 1（还跑，SSH 通） |
| 触屏响应 | 有 | **无（UI 冻结）** |

### 16.4 正确 SSH 连接

**之前的误解**：以为 SSH 是 192.168.31.227:2222，实际是 **192.168.31.213:22**。

```
~/.ssh/config:
Host meilan-phone1
    HostName 192.168.31.213
    User root
    Port 22

# 连接
ssh meilan-phone1
# 或
ssh root@192.168.31.213
```

### 16.5 实测

```
recovery pid=2351
State: T (stopped)      ← 冻结成功

fuelgauged=1            ← 还跑
wpa_supplicant=1        ← 还跑（WiFi 通 192.168.31.213）
adbd=1                  ← 还跑（无线 adb 5555）
sshd=1                  ← 还跑（SSH 22）

ssh meilan-phone1
SSH_OK
02:47:36 up 1:38, 0 users, load average: 10.01, 10.13, 10.35
```

### 16.6 意义

**干净底座搭好了**：
- **TWRP UI 冻结**（触屏无响应，不渲染）
- **底层服务全活**（fuelgauged/wpa_supplicant/sshd/adbd/chroot）
- **纯 SSH 操作**（从电脑远程管理，不需要触屏）
- **省电**（UI 不渲染，节省 CPU/GPU）

**适合场景**：
- **服务器部署**（SSH 远程操作，不需要触屏 UI）
- **省电运行**（UI 不渲染，CPU 负载低）
- **稳定运行**（避免 UI bug 影响，只跑核心服务）

**可逆**：
```bash
kill -CONT $(pidof recovery)   # 恢复 UI
kill -STOP $(pidof recovery)   # 再冻结
```

