# 排错表

按「症状 → 原因 → 处理」查。

| 症状 | 原因 | 处理 |
|---|---|---|
| `su -c id` 报 `No daemon is currently running!` | magiskd 没起或已退出 | 看 `/cache/magisk.log`；`setsid /data/adb/magisk/magisk64 --daemon`；检查 `/data/adb/magisk` 是否完整 |
| `magisk --sqlite` 报 `read (4 != 0) failed` | `magisk.db` 损坏（空文件/非 SQLite） | 停 daemon，`rm /data/adb/magisk.db`，重启 daemon 自动重建 |
| 想按 app 里的模块列表核对状态 | **Magisk v23 没有 `modules` 表** | 看 `/data/adb/modules/<id>` 和目录里的 `disable` 文件，不要查数据库 |
| 模块脚本开机不执行 | `/data/adb/magisk` 缺 `magisk64/busybox/util_functions.sh` 等 | 从 APK 补全（见 `magisk-v23-repair.md`） |
| 模块执行了但服务起完就没 | Magisk 在 `service.sh` 退出时清理进程组，`nohup &` 挡不住 | 用 `setsid` 启动长期进程（模块已处理） |
| 脚本里 `mkdir: not found`、`mount: not found` | chroot 继承了 Android 的 PATH（`/system/bin...`） | 显式 `PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin; export PATH` |
| supervisord 启动即退出，日志目录不存在 | `/var/log` 被 tmpfs 覆盖，挂载前建好的 `/var/log/supervisor` 被盖掉 | 挂完 tmpfs 后 `mkdir -p /var/log/supervisor`（模块 1 已处理） |
| 服务报找不到 `git`、`ssh` 等命令 | supervisord 继承了 Android 的 PATH | 在 `/etc/supervisor/supervisord.conf` 的 `[supervisord]` 加 `environment=PATH="..."` |
| supervisor 状态 `BACKOFF (Exited too quickly)` | 子进程前台启动失败 | `./scripts/logs.sh <name>` 看具体报错；确认命令带前台参数（如 `sshd -D`） |
| 重启后 `/var/log/supervisor/*.log` 全没了 | `/var/log` 是 tmpfs，设计如此 | 需要留存就改持久化（见 operations.md） |
| sshd 起不来 / 连上立刻断 | `/dev/ptmx` 属主不对（Android bind 过来是 radio:radio） | 模块里重建 ptmx：`mknod c 5 2` + `chown root:root` + `chmod 666`，devpts `gid=5,mode=620` |
| 端口被旧进程占着，supervisor 拉不起来 | 手动启动过、或老的 nohup 进程还在 | `pkill -f <进程名>`，或先 `supervisorctl stop all` 再清残留 |
| 改了配置不生效 | supervisor 没重载 | `supervisorctl reread && supervisorctl update`，单个服务用 `restart <name>` |
| 开机后 adb 断了 | `adb-tcp` 模块重启了 adbd | 等 10~20 秒；USB 直连兜底 |
| 不想开 adb 5555 | `adb-tcp` 模块默认开启 | `touch /data/adb/modules/adb-tcp/disable` 后重启 |
| 想确认模块是否被 Magisk 加载 | 模块目录缺 `service.sh` 执行位或权限不对 | `ls -l /data/adb/modules/*/service.sh` 应为 755 root:root |
| `install.sh` 推送后解包失败 | Android 自带 tar 不支持 gzip | 脚本已改用 `/data/adb/magisk/busybox tar xzf` |
| 开机后服务全没起，但模块已启用 | 等开机完成超时 / `/data/debian` 未挂载 | 看 `/data/local/tmp/services.log`；确认 `/data/debian/proc/version` 可读 |
| Magisk app 里看不到模块 | v23 的模块列表来自目录扫描，app 缓存可能没刷新 | 重启 app / 重启设备；不影响实际执行 |

## 快速定位三步

```bash
# 1. Magisk 活着吗
adb shell 'su -c "/data/adb/magisk/magisk64 -V"'

# 2. 模块跑了吗
adb shell 'su -c "cat /data/local/tmp/chroot-mount.log; tail -20 /data/local/tmp/services.log"'

# 3. 服务状态与端口
./scripts/status.sh
PORTS="22 8080" ./scripts/healthcheck.sh
```

## 日志在哪

| 日志 | 路径 | 说明 |
|---|---|---|
| 模块 1 | `/data/local/tmp/chroot-mount.log` | 挂载过程，跨重启保留 |
| 模块 2 | `/data/local/tmp/services.log` | supervisord 启动/重试，跨重启保留 |
| adb 模块 | `/data/local/tmp/adb-tcp.log` | adb TCP 开启记录 |
| supervisord | `/data/debian/var/log/supervisor/supervisord.log` | 重启清空 |
| 各服务 | `/data/debian/var/log/supervisor/<name>.log` | 重启清空 |
| Magisk | `/cache/magisk.log` | magiskd 自己的日志 |
| 开机日志 | `adb logcat -d \| grep Magisk` | 需要重启后立刻抓 |
