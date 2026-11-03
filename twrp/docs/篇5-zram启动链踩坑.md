# 篇5：zram 启动链踩坑 —— 上一任没考虑 64 位溢出

> 时间：2026-11-03
> 设备：魅蓝 Note5 (MT6755, aarch64, Android 7.0, kernel 3.18.35)
> 产物：`03-zram.sh`（zram swap，内存一半）
> 本篇聚焦：**zram 启动链为什么没生效、上一任的代码哪里错了、怎么修**

---

## 一、问题现象

**用户描述**：**"我昨天自己还加入了 zram，不过加入脚本重启后好像也没生效，直接加是生效的"**

**现象**：
- 手动跑 `03-zram.sh` → zram swap 生效 ✅
- 重启后自动跑钩子 → zram 没生效 ❌

**诊断**：
```
# 重启后
cat /proc/swaps   → 空（zram 没激活）
cat /sys/block/zram0/disksize → 0（没设置大小）
```

**结论**：**钩子自动跑时没成功**，手动跑时成功了。

---

## 二、排查过程（3 个坑连环）

### 2.1 坑 1：recovery PATH 里没 awk

**03-zram.sh 原代码**（上一任写的）：
```sh
SZ=$(( $(grep MemTotal /proc/meminfo | awk '{print $2}') * 1024 / 2 ))
echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null
```

**问题**：recovery 的 PATH 里**没 awk**：
```
$ which awk
awk: not found

$ ls /system/bin/awk /sbin/awk
ls: /system/bin/awk: No such file or directory
ls: /sbin/awk: No such file or directory
```

**结果**：`awk '{print $2}'` 失败 → `$()` 返回空 → `$(( * 1024 / 2 ))` 语法错误 → SZ=空 → disksize=0 → mkswap 失败 → swapon 失败

**为什么手动跑成功**：手动跑时可能继承了 adb shell 的 PATH（含 `/system/xbin/awk`？）或用了 busybox awk。但钩子 runner 的 PATH 只有 `/sbin:/system/bin`（无 awk）。

---

### 2.2 坑 2：shell 32 位整数溢出

**尝试修法 1**（用纯 shell，不 awk）：
```sh
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')
SZ=$(( KB * 1024 / 2 ))
```

**问题**：shell 32 位整数溢出：
```
KB = 2855084
SZ = 2855084 * 1024 / 2 = 1461795328

但 32 位有符号整数最大值 = 2^31 - 1 = 2147483647
1461795328 < 2147483647，应该没溢出啊？
```

**实际测**：
```
$ KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')
$ echo $(( KB * 1024 / 2 ))
-685680640   ← 负数！
```

**原因**：`KB * 1024` = `2855084 * 1024` = `2923606016` > `2^31 = 2147483648` → **中间结果溢出**（即使最终结果 < 2^31，中间计算溢出了）。

**修法**：分步算，避免中间结果溢出：
```sh
SZ=$(( KB * 512 ))   # 2855084 * 512 = 1461805056 < 2^31 ✅
```

---

### 2.3 坑 3：/proc/meminfo 带单位

**尝试修法 2**（分步算）：
```sh
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')
SZ=$(( KB * 512 ))
```

**问题**：`grep MemTotal /proc/meminfo` 输出：
```
MemTotal:        2855084 kB   ← 带 kB 单位！
```

`tr -d ' '` 只删空格，没删 kB：
```
$ KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')
$ echo $KB
2855084kB   ← 带单位
$ echo $(( KB * 512 ))
bad number '2855084kB'   ← 语法错误
```

**修法**：`tr -d 'kB'` 去单位：
```sh
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
# 或
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')
KB=${KB%kB}   # 去后缀
```

---

## 三、最终修法（3 个坑全解决）

```sh
#!/system/bin/sh
# 03-zram.sh — zram swap (内存一半, 自包含, 幂等)
# 坑1: recovery PATH里没awk
# 坑2: shell32位溢出(>2G算错)
# 坑3: MemTotal带单位"2855084 kB"(要tr -d kB)

# 已在用则跳过 (幂等)
if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi

# 建节点 (内建 zram, major 254)
[ -e /dev/zram0 ] || mknod /dev/zram0 b 254 0 2>/dev/null

# disksize = MemTotal(kb) * 512 (分步算64位安全, 去kB单位)
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
SZ=$(( KB * 512 ))
[ "$SZ" -le 0 ] && SZ=1461795328  # fallback 1.4G

echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null

# 启用 swap (优先级 100)
mkswap /dev/zram0 >/dev/null 2>&1
swapon -p 100 /dev/zram0 >/dev/null 2>&1

exit 0
```

**验证**：
```
$ cat /proc/swaps
/dev/zram0  partition  1427540  0  100

$ cat /sys/block/zram0/disksize
1461805056
```

**zram swap 生效**（1427540 kB = 1.4G，priority 100）✅

---

## 四、上一任没考虑的问题（总结）

### 4.1 没考虑 recovery PATH 限制

**上一任假设**：PATH 里有 `awk`（normal 环境有）
**实际**：recovery PATH 只有 `/sbin:/system/bin`，**没 awk**

**教训**：**写 recovery 钩子脚本，先确认 PATH 里有什么命令**（`which awk`/`ls /system/bin/awk`）。

### 4.2 没考虑 shell 32 位溢出

**上一任假设**：`$(( KB * 1024 / 2 ))` 能正常算（数学上没错）
**实际**：shell 32 位整数，中间结果 `KB * 1024 = 2923606016 > 2^31` → 溢出变负数

**教训**：**shell 算术是 32 位，大于 2G 的数要分步算**（先 `* 512` 而不是 `* 1024 / 2`）。

### 4.3 没考虑 /proc 输出带单位

**上一任假设**：`grep MemTotal` 返回纯数字（`2855084`）
**实际**：`/proc/meminfo` 格式是 `MemTotal:  2855084 kB`（**带单位**）

**教训**：**`/proc` 输出通常带单位，要 `tr -d 'kB'` 去单位**。

### 4.4 没考虑幂等性（重启后再跑）

**上一任代码**（无幂等检查）：
```sh
SZ=$(( $(grep MemTotal /proc/meminfo | awk '{print $2}') * 1024 / 2 ))
echo "$SZ" > /sys/block/zram0/disksize
mkswap /dev/zram0
swapon -p 100 /dev/zram0
```

**问题**：重启后 zram0 已存在（disksize 已设），再 `echo disksize` 会失败（device busy）→ 但因为 SZ 算错了（坑 1/2/3），反而没触发这个错误。

**修法**：加幂等检查（已在用就跳过）：
```sh
if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi
```

### 4.5 没测试重启后自动跑

**上一任**：可能只测了手动跑（`sh 03-zram.sh`），没测重启后自动跑（钩子 runner 环境）

**教训**：**钩子脚本必须测重启后自动跑**（PATH、环境变量、权限都不同）。

---

## 五、为什么"直接加是生效的"

**用户说**："**直接加是生效的**"

**解释**：
- **直接加**（手动在 adb shell 里跑命令）→ 用的是 adb shell 的 PATH（可能含 busybox/awk）+ 手动算（人脑算，不会溢出）→ 成功
- **钩子自动跑**（`03-zram.sh`）→ 用的是 runner 的 PATH（`/sbin:/system/bin`，没 awk）+ shell 算（32 位溢出 + 带单位）→ 失败

**本质**：**手动跑和钩子自动跑的环境不同**（PATH、环境变量、权限），钩子脚本必须考虑 runner 环境。

---

## 六、zram 启动链完整流程（修好后的）

```
开机 → TWRP init 跑 init.rc
        └─ service twrphook /sbin/twrp-hook.sh
             └─ /sbin/twrp-hook.sh (runner)
                  ├─ 等 /data 挂载 (60s)
                  ├─ 挂 /system (ro)
                  ├─ export PATH=/sbin:/system/bin
                  ├─ export LD_LIBRARY_PATH=/sbin:/system/lib
                  └─ for f in /data/local/tmp/twrp-hooks/*.sh:
                       ├─ 01-fuelgauged.sh  (电池守护，& 后台)
                       ├─ 02-wifi.sh         (WiFi 拉起，wpa_cli reassociate 一次)
                       ├─ 03-zram.sh         (zram swap，64 位安全算法)  ← 修好了
                       ├─ 20-chroot-mount.sh (挂 Debian chroot)
                       ├─ 30-chroot-services.sh (起 supervisord)
                       ├─ 40-adbd-tcp.sh     (无线 adb)
                       └─ 50-static-ip.sh    (配静态 IP + 路由)
```

**zram 在 03 位置**（wifi 之后，chroot 之前），确保 wifi 拉起时不占用 zram（避免循环依赖）。

---

## 七、经验总结（写 recovery 钩子脚本的 5 条铁律）

### 7.1 先确认 PATH 里有什么命令

```sh
# 在钩子脚本开头加诊断
which awk || echo "no awk"
which busybox || echo "no busybox"
ls /system/bin/ | grep -E "awk|sed|grep"
```

### 7.2 shell 算术分步算（避免 32 位溢出）

```sh
# ❌ 错（中间结果溢出）
SZ=$(( KB * 1024 / 2 ))

# ✅ 对（分步算，不溢出）
SZ=$(( KB * 512 ))
```

### 7.3 /proc 输出去单位

```sh
# ❌ 错（带单位）
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ')

# ✅ 对（去单位）
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
# 或
KB=${KB%kB}
```

### 7.4 加幂等检查（重启后再跑不报错）

```sh
# ✅ 对（已在用就跳过）
if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi
```

### 7.5 必须测重启后自动跑（不能只测手动跑）

```bash
# 手动测
adb shell sh /data/local/tmp/twrp-hooks/03-zram.sh

# 重启后自动跑测（关键！）
adb shell reboot recovery
# 等 120 秒
adb shell cat /proc/swaps   # 看有没有 zram
```

---

## 八、对比：上一任的代码 vs 修好的代码

### 8.1 上一任的代码（3 个坑）

```sh
#!/system/bin/sh
# 没幂等检查
# 用 awk（recovery 没有）
# 没去 kB 单位
# 32 位溢出

SZ=$(( $(grep MemTotal /proc/meminfo | awk '{print $2}') * 1024 / 2 ))
echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null
mkswap /dev/zram0 >/dev/null 2>&1 && swapon -p 100 /dev/zram0 >/dev/null 2>&1
```

**结果**：disksize=0 → mkswap 失败 → swapon 失败 → zram 没激活

### 8.2 修好的代码（3 个坑全解决）

```sh
#!/system/bin/sh
# 加幂等检查
if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi

[ -e /dev/zram0 ] || mknod /dev/zram0 b 254 0 2>/dev/null

# 去 kB 单位 + 分步算（64 位安全）
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
SZ=$(( KB * 512 ))
[ "$SZ" -le 0 ] && SZ=1461795328

echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null
mkswap /dev/zram0 >/dev/null 2>&1
swapon -p 100 /dev/zram0 >/dev/null 2>&1
```

**结果**：disksize=1461805056 → mkswap 成功 → swapon 成功 → **zram 激活（1427540 kB，priority 100）** ✅

---

## 九、一句话总结

**上一任没考虑 3 个坑**：
1. **recovery PATH 里没 awk**（只测了手动跑，没测钩子自动跑）
2. **shell 32 位溢出**（`KB * 1024` 中间结果 > 2^31）
3. **/proc/meminfo 带单位**（`2855084 kB`，没 `tr -d 'kB'`）

**修法**：去 awk（用纯 shell）+ 分步算（`* 512` 而不是 `* 1024 / 2`）+ 去单位（`tr -d 'kB'`）+ 加幂等检查。

**教训**：**写 recovery 钩子脚本，要考虑 PATH 限制 + shell 32 位溢出 + /proc 输出带单位，必须测重启后自动跑**。

---

*本篇是 zram 启动链踩坑。背景与路线见篇1，TWRP 镜像修补技术见篇2，MTK 驱动逆向见篇3，hybrid 全自动钩子实战见篇4。*
