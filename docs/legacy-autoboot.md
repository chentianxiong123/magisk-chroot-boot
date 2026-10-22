# 历史方案：AutoBoot.apk

旧版用一个小系统 APK (`AutoBoot.apk`) 实现开机自启，现已被 Magisk 模块方案取代。
本文保留其细节，便于对照、排查遗留物或回滚。**当前的启动链路不需要它。**

## 它是什么

- 包名：`com.meizu.autoboot`
- 位置：`/system/app/AutoBoot/AutoBoot.apk`（系统应用，versionName 1.0）
- 组件：只有一个 `MainActivity` 和一个 `BootReceiver`
- 行为：`BOOT_COMPLETED` 后读取 `/data/autoboot.conf`，用 `/system/xbin/su` 逐行执行
- 反编译 `classes.dex` 能看到的关键字符串：`/data/autoboot.conf`、`/system/xbin/su`
- 没有常驻 Service，也不存自己的配置；数据目录里只有 `cache/`

APK 很小（约 12 KB，dex 约 4 KB），本质是一个「开机以 root 跑一个配置文件的命令」的壳。

## 为什么当年要做成系统应用

1. Flyme 的 ADB 安装受限，`adb install` 报 `INSTALL_FAILED_USER_RESTRICTED`，
   改为放进 `/system/app/` 由系统扫描安装。
2. `adb install` 的普通应用默认 `stopped=true`，收不到 `BOOT_COMPLETED`；
   系统应用 `stopped=false`，可以正常收到广播。
3. Flyme 的 su 需要一次手动授权；首次点「允许」后，之后开机才能静默执行。
   （系统应用 + 首次授权是当年能跑通的关键组合。）

## 旧的启动链路

```text
开机 → AutoBoot.apk (BOOT_COMPLETED) → 约 5s → /data/autoboot.conf
  ├─ sh /data/autoboot/android.conf
  │    ├─ setprop service.adb.tcp.port + stop/start adbd   → adb over TCP
  │    └─ sh /data/autoboot/chroot.sh                      → 挂载 chroot
  └─ chroot → /usr/local/bin/autoboot-hook.sh
       └─ /etc/autoboot.d/*.conf                           → chroot 内命令（如 sshd）
```

| 文件 | 作用 |
|---|---|
| `/system/app/AutoBoot/AutoBoot.apk` | 开机自启 APK（系统应用） |
| `/data/autoboot.conf` | 入口配置，APK 读它 |
| `/data/autoboot/android.conf` | Android 侧命令 |
| `/data/autoboot/chroot.sh` | 挂载 chroot 并触发内部钩子 |
| `/data/debian/usr/local/bin/autoboot-hook.sh` | chroot 侧钩子 |
| `/data/debian/etc/autoboot.d/*.conf` | chroot 侧命令 |

## 当年调 su 的正确姿势

Flyme 的 su 不支持 `-c`，正确写法是：

```java
Process p = Runtime.getRuntime().exec(new String[]{"/system/xbin/su", "0"});
OutputStream os = p.getOutputStream();
os.write((cmd + "\n").getBytes());
os.write("exit\n".getBytes());
os.flush();
os.close();
p.waitFor();
```

等价于 shell 里的 `echo "cmd" | su 0`。

## 为什么被 Magisk 模块取代

- Magisk 的 `late_start` 阶段 `service.sh` **本身就是 root**，不用 APK，也不用改 `/system`。
- `setsid` 解决「模块脚本退出后进程组被清理」的问题。
- 模块可版本管理、幂等安装、备份/回滚，比「改系统分区 + 一个配置约定」更好维护。
- 不再依赖 Flyme su 的授权弹窗行为。

## 当前状态与清理

现在设备上 `/data/autoboot.conf` **已不存在**，所以 AutoBoot 开机是空转（读完文件发现不存在，什么也不做）。

安全做法是冻结而不是物理删除（可逆、无 OTA/verity 风险）：

```bash
# 冻结
adb shell su -c 'pm disable-user --user 0 com.meizu.autoboot'
# 恢复
adb shell su -c 'pm enable com.meizu.autoboot'
```

验证：

```bash
adb shell 'pm list packages -d | grep autoboot'   # 出现在禁用列表
adb shell 'dumpsys package com.meizu.autoboot | grep -E "enabled=|hidden="'
# enabled=3 表示 DISABLED_USER，hidden=false 表示不是隐藏
```

不推荐物理删除：要 remount `/system` 再删 `AutoBoot.apk`，收益只有 12 KB，
还会增加 OTA / dm-verity 的不确定性。

## 回滚到旧方案

1. 恢复 `/data/autoboot.conf` 及其引用的脚本；
2. `pm enable com.meizu.autoboot`；
3. 确认 `/system/xbin/su` 可用且已授权；
4. 不要再同时启用 Magisk 的 `chroot-mount` / `chroot-services`，否则会重复挂载/重复拉起。
