# 架构

## 启动链路

```text
Android 开机
└─ /init（magiskinit，boot ramdisk 已 patch）
   └─ magiskd
      └─ late_start：遍历 /data/adb/modules/*/service.sh
         ├─ chroot-mount
         │    ├─ 等 sys.boot_completed=1 且 /data/debian 存在
         │    ├─ mount proc / sys / dev（bind）/ devpts
         │    ├─ 重建 /dev/ptmx（root:root 666）
         │    ├─ devpts remount gid=5,mode=620
         │    ├─ tmpfs：/tmp、/var/log(64M)、apt lists/cache(128M)
         │    ├─ mkdir /run/sshd、/var/log/supervisor
         │    └─ 写 /etc/resolv.conf
         │
         ├─ chroot-services
         │    ├─ 等 boot_completed 且 /data/debian/proc/version
         │    ├─ setsid chroot → supervisord -c /etc/supervisor/supervisord.conf
         │    └─ 轮询 supervisorctl pid，最多 3 次
         │
         └─ adb-tcp（可选，独立模块）
              ├─ 等 boot_completed
              └─ setprop service.adb.tcp.port 5555 + stop/start adbd
```

关键点：`setsid` 必须有。Magisk 在模块 `service.sh` 退出时会清理该进程组，
supervisord 必须脱离出去才能长期存活。

## 两层职责

| 层 | 负责 | 不负责 |
|---|---|---|
| Magisk 模块 | 挂载、tmpfs、目录准备、拉起 supervisord；adb 由独立模块负责 | 业务进程的启停/重启/日志 |
| supervisord | 服务启动顺序、崩溃重拉、日志轮转、统一启停 | 文件系统挂载、adb |

这样划分的好处：加服务只改 supervisor 配置；模块保持极简，出错面小。

## 服务定义

配置文件：`/data/debian/etc/supervisor/conf.d/chroot.conf`

仓库只提供 `supervisor/chroot.conf.tpl` 作为示例，真实服务建议放在本地
`config/services.conf`（gitignore），由 `install.sh` 推送。

单个 program 的要点：

```ini
[program:myservice]
command=/opt/myservice/bin/myservice --flag
directory=/opt/myservice
priority=50
startsecs=5
autostart=true
autorestart=true
stdout_logfile=/var/log/supervisor/myservice.log
stdout_logfile_maxbytes=2MB
stdout_logfile_backups=1
redirect_stderr=true
```

- `command` 必须是**前台**命令（如 `sshd -D`、`gunicorn` 不加 `--daemon`），
  自己 daemonize 的进程会被 supervisor 判定为退出。
- `priority` 决定启动/停止顺序，越小越先。
- `environment=` 可补 PATH 等变量；chroot 会继承 Android 的 PATH，必要时显式覆盖。

## 路径

| 路径（Android） | chroot 内 | 作用 |
|---|---|---|
| `/data/adb/modules/chroot-mount` | — | 挂载模块 |
| `/data/adb/modules/chroot-services` | — | 引导模块 |
| `/data/adb/modules/adb-tcp` | — | 可选：adb over TCP 模块 |
| `/data/debian` | `/` | Debian 根 |
| `/data/debian/etc/supervisor/conf.d/chroot.conf` | `/etc/supervisor/conf.d/chroot.conf` | 服务定义 |
| `/data/debian/var/log` | `/var/log` | tmpfs 64M |
| `/data/debian/var/log/supervisor` | `/var/log/supervisor` | 各服务日志 |
| `/data/debian/tmp` | `/tmp` | tmpfs |
| `/data/debian/var/lib/apt/lists` | — | tmpfs 128M |
| `/data/debian/var/cache/apt` | — | tmpfs 128M |
| `/data/local/tmp/chroot-mount.log` | — | 模块 1 日志（跨重启保留） |
| `/data/local/tmp/services.log` | — | 模块 2 日志（跨重启保留） |

`/var/log`、`/tmp` 是内存盘，**重启即清空**，这是有意设计（保护闪存）。
需要长期日志就把对应 mount 去掉或改挂到持久目录。

## 故障域

| 故障 | 影响 | 恢复 |
|---|---|---|
| magiskd 挂掉 | 已运行的 supervisord 与服务不受影响（setsid）；重启后无人拉起 | 重启设备，或手动 `setsid /data/adb/magisk/magisk64 --daemon` |
| Magisk 模块被禁用 | 重启后无挂载、无服务 | 重新启用模块并重启 |
| supervisord 挂掉 | 服务失去管理，已起的进程仍活；不会自动拉回 | 手动 `setsid chroot ... supervisord` 或重启 |
| 单个服务崩溃 | 只有该服务受影响，supervisor 自动重拉 | 自动 |
| `/data/debian` 丢失 | 挂载与所有服务失败 | 恢复 chroot |
| adb 5555 被占用/关闭 | 远程 adb 断 | USB 直连或重启 adbd |
