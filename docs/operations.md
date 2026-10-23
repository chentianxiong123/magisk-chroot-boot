# 运维手册

## 首次部署

```bash
mkdir -p config
cp templates/service.conf.example config/services.conf
$EDITOR config/services.conf     # 填自己的服务

./install.sh
adb reboot
sleep 80
./scripts/healthcheck.sh
```

前提：设备已 root（`su -c id` 返回 uid=0）、`/data/debian` 存在、chroot 内已装 supervisor。
不需要 adb TCP 时，安装后禁用 `adb-tcp` 模块即可。
不需要开机自动写 DNS 时，禁用 `chroot-network` 模块即可（自己维护 `/etc/resolv.conf`）。

不提供 `config/services.conf` 时，`install.sh` 会安装 `supervisor/chroot.conf.tpl`
里的示例；设备上已有配置则不会覆盖。

## 日常检查

```bash
./scripts/status.sh                # Magisk 版本 + supervisor 状态
./scripts/healthcheck.sh           # supervisor 状态
PORTS="22 8080" ./scripts/healthcheck.sh   # 另外检查端口
./scripts/logs.sh <name> 200       # 某服务最近 200 行
./scripts/logs.sh supervisord      # supervisor 主日志
```

设备内等价命令：

```bash
CTL="/data/debian/bin/busybox chroot /data/debian /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf"
$CTL status
$CTL restart <name>
$CTL stop all
$CTL start all
$CTL tail -f <name>
```

确认 supervisord 继承了 Android 网络组（服务会继承）：

```bash
grep ^Groups /proc/$(cat /data/debian/run/supervisord.pid)/status
# 期望：Groups: 0 3003 3004
```

## 加一个新服务

1. 参考 `templates/service.conf.example`，把配置加到：
   - 本地 `config/services.conf`（推荐，gitignore），或
   - 设备上的 `/data/debian/etc/supervisor/conf.d/chroot.conf`；
2. 设备上生效：

   ```bash
   $CTL reread
   $CTL update
   ```

3. 检查：

   ```bash
   $CTL status
   ./scripts/healthcheck.sh
   ```

注意：

- 进程必须**前台运行**（不要自己 daemonize），否则 supervisor 会判定退出；
- 需要环境变量就写 `environment=K="v"`，不要依赖 Android 的 PATH；
- 端口不要和现有服务冲突。

## 改服务参数 / 升级配置

```bash
$EDITOR config/services.conf
./install.sh
adb shell "su -c '$CTL reread && $CTL update'"
```

只重启单个服务：

```bash
adb shell "su -c '$CTL restart <name>'"
```

## 升级模块

1. 修改 `modules/*/service.sh` 或 `module.prop`，把 `version` / `versionCode` 加一；
2. `./install.sh`；
3. 重启验证。

也可以用 `./build.sh` 生成 `dist/*.zip`，通过 Magisk app 的「从本地安装」安装。

## 备份与恢复

```bash
./scripts/backup.sh    # 生成 backups/chroot-backup-<时间>.tgz（模块 + supervisor 配置）
```

恢复：

```bash
adb push backups/chroot-backup-YYYYmmdd-HHMMSS.tgz /data/local/tmp/
adb shell 'su -c "cd / && tar xzf /data/local/tmp/chroot-backup-YYYYmmdd-HHMMSS.tgz"'
adb reboot
```

备份**不含**各服务自己的密钥配置，那些需要单独备份。

## 卸载

```bash
./uninstall.sh            # 停 supervisord + 删模块（含可选的 adb-tcp）
./uninstall.sh --purge    # 额外删除 supervisor 配置
```

## 让日志持久化

默认 `/var/log` 是 tmpfs（重启清空）。若想保留：

1. 编辑 `modules/chroot-mount/service.sh`，删掉挂 `/data/debian/var/log` 的 tmpfs 行；
2. 在 chroot 内给 supervisor 配 logrotate，或调小 `stdout_logfile_maxbytes`；
3. 重新部署并重启。

## 安全

- `adb tcp 5555` 仍使用 adb 密钥认证（user build 无法像调试版那样免认证），但公网仍应关闭：
  `touch /data/adb/modules/adb-tcp/disable` 后重启，或直接删除 `adb-tcp` 模块。
- 服务自己的管理端口尽量只监听 `127.0.0.1`，或用防火墙限制。
- `config/` 整个目录不进仓库；真实密钥放在 chroot 内并 `chmod 600`。
