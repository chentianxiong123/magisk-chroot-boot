# Magisk v23 安装残缺的修复记录

## 现象

Magisk app 显示 v23.0 已安装，boot ramdisk 也确实 patch 了（`/init` 1.25MB 的 magiskinit），
开机 logcat 有：

```text
Magisk 23.0(23000) daemon started
* Device API level: 24
** post-fs-data mode running
* Loading modules
```

但随后 magiskd 消失：

```text
$ su -c id
No daemon is currently running!
$ magisk --sqlite "select * from modules"
No daemon is currently running!
```

## 为什么判断是人为造成的

看 `/data/adb` 的时间戳：

| 路径 | 创建时间 |
|---|---|
| `/data/adb/post-fs-data.d`、`service.d` | 15:08 |
| `/data/adb/modules` | 15:42 |
| `/data/adb/magisk.db` | 15:46 |
| `/data/adb/magisk/` | 15:51 |
| 设备重启 | 15:53 |

Magisk 初始化是一瞬间写全的，不可能几分钟长一个目录。而 `/data/adb/magisk` 里只有：

- `busybox` 170176 字节 —— 真 busybox 是 1.4MB，这个尺寸正好等于 `magisk64`，是改名冒充的；
- `util_functions.sh` 502 字节 —— 真的是 22KB，这个是手写残废版。

缺的：`magisk32`、`magisk64`、`magiskboot`、`magiskinit`、`magiskpolicy`、`stub.apk`。

`magisk.db` 也是空文件（手工创建），所以 `--sqlite` 报 `read (4 != 0) failed with 0: Success`。
删掉让 daemon 重建后 schema 为：

```text
policies / settings / strings / hidelist
```

**v23 没有 `modules` 表**，模块启用状态靠目录里的 `disable` 文件。任何 `SELECT ... FROM modules`
的输出都是编的。

daemon 启动日志还报过：

```text
E : fopen: /data/adb/magisk/.magisk/config failed with 2: No such file or directory
```

## 修复：从已安装 APK 提取二进制

Magisk APK 里的二进制以 JNI lib 形式打包：

```bash
adb pull /data/app/com.topjohnwu.magisk-1/base.apk magisk_v23.apk
unzip -o -q magisk_v23.apk 'lib/armeabi-v7a/*' 'assets/*'
```

坑：设备是 arm64 用户态（`ro.product.cpu.abi=arm64-v8a`），但 APK 的 `lib/arm64-v8a/`
里只有一个 0 字节的 `libstub.so`，真二进制都在 `lib/armeabi-v7a/`。用 `/sbin/magisk64`
的大小（170176）比对确认。

映射：

| APK 文件 | 目标（`/data/adb/magisk/`） |
|---|---|
| `lib/armeabi-v7a/libmagisk32.so` | `magisk32` |
| `lib/armeabi-v7a/libmagisk64.so` | `magisk64` |
| `lib/armeabi-v7a/libmagiskinit.so` | `magiskinit`，另复制一份为 `magiskpolicy` |
| `lib/armeabi-v7a/libmagiskboot.so` | `magiskboot` |
| `lib/armeabi-v7a/libbusybox.so` | `busybox` |
| `assets/util_functions.sh` | `util_functions.sh` |
| `assets/boot_patch.sh`、`addon.d.sh`、`uninstaller.sh`、`chromeos/` | 同名放入 |

权限：二进制 755、脚本 644、`root:root`，最后 `restorecon -R /data/adb/magisk`。

## 重建数据库与配置

```bash
# 停掉残留 daemon（如有）
for p in /proc/[0-9]*; do
  e=$(readlink $p/exe 2>/dev/null)
  case "$e" in */magisk64|*/magisk32) kill -9 ${p#/proc/} 2>/dev/null;; esac
done

rm -f /data/adb/magisk.db
setsid /data/adb/magisk/magisk64 --daemon

mkdir -p /data/adb/magisk/.magisk
cp -f /sbin/.magisk/config /data/adb/magisk/.magisk/config
chown -R root:root /data/adb/magisk/.magisk
chmod 600 /data/adb/magisk/.magisk/config
```

验证：

```text
$ /data/adb/magisk/magisk64 -V
23000
$ su -c id
uid=0(root) gid=0(root) context=u:r:magisk:s0
$ magisk --sqlite "select name from sqlite_master"
name=policies
name=settings
name=strings
name=hidelist
```

## 注意

- 手工启动 daemon 一定要 `setsid`，否则 adb shell 退出时会被一起带走。
- 如果 magiskd 在 boot_complete 后仍然消失，检查 `/data/adb/magisk` 是否完整、
  `/cache/magisk.log` 与 `logcat | grep Magisk`，以及设备是否有安全软件杀进程。
- 部分系统自带的 厂商自带的 `su` 备份（setuid root + SELinux Permissive）是 Magisk 挂掉时的救命通道。
