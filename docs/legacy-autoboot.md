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

## 实现原理（反编译推断）

`classes.dex` 里的字符串和类结构显示它只做三件事：

1. `BootReceiver.onReceive()` 收到 `BOOT_COMPLETED` 后起一个线程，先 `Thread.sleep()` 等几秒，
   给系统/存储留出就绪时间；
2. 用 `File` / `FileReader` / `BufferedReader` 打开 `/data/autoboot.conf`，`readLine()` 逐行读，
   `trim()` 后跳过空行/注释（`startsWith` / `equals` 判断）；
3. 对每行命令调用 `Runtime.exec(new String[]{"/system/xbin/su", "0"})`，把命令和 `exit`
   写进 `getOutputStream()`，`flush()` / `close()`，`waitFor()` 等它结束，再处理下一行。

`MainActivity` 是同一套逻辑的手动触发入口，也用于首次运行时弹出 Flyme 的 su 授权框。
它没有常驻 Service、没有定时任务，也不联网。

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

## 当前状态：已卸载（2026-10-22）

设备上 `/data/autoboot.conf` **已不存在**，AutoBoot 开机是空转（读完文件发现不存在，什么都不做），
因此已将其卸载：

```bash
# 先冻结确认无影响
adb shell su -c 'pm disable-user --user 0 com.meizu.autoboot'
# 确认空转后，对当前用户卸载
adb shell su -c 'pm uninstall --user 0 com.meizu.autoboot'
# -> Success
```

结果：

| 检查 | 结果 |
|---|---|
| `pm list packages \| grep autoboot` | 无输出（user 0 已不可见） |
| `pm list packages -u \| grep autoboot` | 仍在（说明只是对 user 0 卸载） |
| `pm path com.meizu.autoboot` | 空 |

注意：`pm uninstall --user 0` 只是「对当前用户卸载」，`/system/app/AutoBoot/AutoBoot.apk`
**仍然保留**，所以没有 OTA / dm-verity 风险，也随时可以恢复。没有物理删除系统分区文件。

### 恢复

```bash
adb shell su -c 'pm install-existing --user 0 com.meizu.autoboot'
# 若固件不支持 install-existing，退回系统应用状态可用：
adb shell su -c 'pm enable com.meizu.autoboot'
```

彻底物理删除（不推荐，收益仅约 12 KB，且会增加 OTA / verity 不确定性）：

```bash
adb shell su -c 'mount -o rw,remount /system && rm -rf /system/app/AutoBoot'
```

## 回滚到旧方案

1. `pm install-existing --user 0 com.meizu.autoboot`（或 `pm enable`）重新挂上 APK；
2. 恢复 `/data/autoboot.conf` 及其引用的脚本；
3. 确认 `/system/xbin/su` 可用且已授权；
4. 不要再同时启用 Magisk 的 `chroot-mount` / `chroot-services`，否则会重复挂载/重复拉起。
