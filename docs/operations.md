# 运维手册

## 首次部署

```bash
mkdir -p config
cp templates/service.conf.example config/services.conf
$EDITOR config/services.conf     # 填自己的服务

# 可选：chroot 里跑 raw TUN（EasyTier/WireGuard 等），固定网络一次性路由
cp modules/chroot-network/routes.conf.example config/routes.conf
$EDITOR config/routes.conf

# 可选：需要 watchdog 或 ICMP 修复时才用
cp modules/chroot-netfix/netfix.conf.example config/netfix.conf
$EDITOR config/netfix.conf

# 可选：固定 IP
cp modules/static-ip/static-ip.conf.example config/static-ip.conf
$EDITOR config/static-ip.conf

# 可选：开机后 stop / freeze 名单
cp modules/postboot-tune/postboot-tune.conf.example config/postboot-tune.conf
$EDITOR config/postboot-tune.conf

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

## 一次性策略路由（chroot-network，推荐固定网络）

适用：chroot 内跑 raw TUN，TUN 网段能进但回包走 `wlan0`/`rmnet`；网络固定、
不需要切 Wi-Fi/移动数据。

配置 `/data/adb/chroot-routes.conf`（本地 `config/routes.conf`，gitignored）：

```sh
RULE_CIDRS="10.0.0.0/24"   # 需要走 main 表的 TUN 网段，可多个
RULE_PREF=9000
```

`chroot-network` 在开机写完 DNS 后加一次 `ip rule`，5s 后重试一次，然后退出；
没有 watchdog、没有 10s 唤醒。验证：

```bash
adb shell 'su -c "ip rule show | grep 9000"'
adb shell 'su -c "tail -20 /data/local/tmp/chroot-network.log"'
```

切网/Doze 后规则被刷掉就不会自动补，这种情况用下面的 `chroot-netfix`。

## chroot-netfix（可选，watchdog/ICMP）

适用：需要切网/Doze 后自动补规则，或者 TCP 正常但 ping 不回。

配置 `/data/adb/chroot-netfix.conf`（本地 `config/netfix.conf`，gitignored）：

```sh
RULE_CIDRS="10.0.0.0/24"   # 需要走 main 表的 TUN 网段，可多个
RULE_PREF=9000
ICMP_ECHO_REPLY=1          # ROM 省电防火墙丢内核 ICMP 时打开
IPTABLES_CHAIN=fw_OUTPUT
```

改完执行 `./install.sh` 会重启 watchdog；也可以等 10s，watchdog 每轮都会重新
读取配置。验证：

```bash
adb shell 'su -c "ip rule show | grep 9000"'
adb shell 'su -c "iptables -S fw_OUTPUT | head -3"'
adb shell 'su -c "tail -20 /data/local/tmp/chroot-netfix.log"'
```

不需要就删掉配置里的 CIDR/把 `ICMP_ECHO_REPLY=0`，或禁用模块。注意：watchdog
只是不再**新加**规则；已经加上的规则要重启，或手动删：

```bash
ip rule del to <CIDR> lookup main pref 9000
iptables -D fw_OUTPUT -p icmp -m icmp --icmp-type 0 -j RETURN
```

## 固定 IP（static-ip）

适用：ROM 的 WLAN 设置界面不可用，没法在 GUI 里改静态 IP。

配置 `/data/adb/static-ip.conf`（本地 `config/static-ip.conf`，gitignored）：

```sh
STATIC_IP=192.168.31.213
PREFIX=24
IFACE=wlan0
SUBNET=192.168.31.   # 用来识别要替换的 DHCP 地址
```

模块在开机后等 DHCP 地址出现，设 `promote_secondaries=1`，加静态地址，再删掉其他
IPv4。验证：

```bash
adb shell 'su -c "ip -4 addr show wlan0"'
adb shell 'su -c "tail -20 /data/local/tmp/static-ip.log"'
```

限制：只执行一次。WiFi 断线重连或框架重启后地址会变回 DHCP，需要重启，或手动
`adb shell 'su -c "sh /data/adb/modules/static-ip/service.sh"'`。

## 开机后清理/冻结（postboot-tune）

配置 `/data/adb/postboot-tune.conf`（本地 `config/postboot-tune.conf`，gitignored）：

```sh
WAIT_BOOT=40
IFACE=wlan0
STATIC_IP=192.168.31.213
WAIT_STATIC=150
STOP_PKGS="com.android.settings com.android.providers.downloads"
FREEZE_PROCS="com.mediatek.atci.service com.meizu.wifiadmin"
FREEZE_NEEDS_NET="com.meizu.wifiadmin"
```

- `STOP_PKGS` 用 `am force-stop`：非 persistent 的按需应用杀掉不会回来。
- `FREEZE_PROCS` 用 `kill -STOP`：只停 CPU，**不释放内存**；`kill -CONT <pid>` 解冻。
- `FREEZE_NEEDS_NET` 里的进程在 `STATIC_IP` 没出现时跳过，避免 WiFi 还没连上就冻掉管理进程。

验证与撤销：

```bash
adb shell 'su -c "ps | grep \" T \""'
adb shell 'su -c "tail -20 /data/local/tmp/postboot-tune.log"'
adb shell 'su -c "kill -CONT <pid>"'
```

冻结不是系统级状态：进程被杀/重启后失效，模块不会补冻（没有 watchdog）。

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

1. 修改 `modules/*/service.sh`、`watchdog.sh` 或 `module.prop`，把 `version` / `versionCode` 加一；
2. `./install.sh`；
3. 重启验证。

也可以用 `./build.sh` 生成 `dist/*.zip`，通过 Magisk app 的「从本地安装」安装。

## 备份与恢复

```bash
./scripts/backup.sh    # 生成 backups/chroot-backup-<时间>.tgz（模块 + supervisor + routes/netfix 配置）
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
./uninstall.sh            # 停 supervisord + 清 netfix 规则 + 删模块（含可选的 adb-tcp）
./uninstall.sh --purge    # 额外删除 supervisor 配置、chroot-routes.conf、chroot-netfix.conf
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
