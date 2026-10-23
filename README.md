# magisk-chroot-boot

用两个 Magisk 模块，让 Android 开机后自动挂载 Debian chroot，并拉起
[supervisord](http://supervisord.org/) 托管 chroot 内的服务。

- `chroot-mount`：等开机完成，挂 `proc/sys/dev/devpts`，准备 tmpfs 和运行目录
- `chroot-services`：用 `setsid` 脱离 Magisk 进程组启动 supervisord
- `adb-tcp`：可选，开机后开启 adb over TCP（5555），与 chroot 完全独立

supervisord 负责服务本身的启动顺序、崩溃重拉、统一启停和日志轮转；模块只做挂载和引导，
所以增删服务不需要改模块。

## 特性

- 开机全自动，无需 init.d、Tasker 或额外 app
- 自动处理 Android chroot 网络权限（`inet`/`net_raw`、`_apt`、开机 DNS）
- adb over TCP 独立成模块，可单独启用/删除，不影响 chroot
- `kill -9` 后由 supervisor 自动拉起
- 模块职责单一，服务定义集中在一个 supervisor conf
- 幂等安装，可重复执行
- 自带状态 / 日志 / 健康检查 / 备份 / 打包脚本
- 真实服务配置与密钥留在本地 `config/`（gitignore），仓库只含占位示例

## 启动链路

```text
Android 开机
└─ magiskd
   └─ late_start：执行 /data/adb/modules/*/service.sh
      ├─ chroot-mount
      │    ├─ 等 sys.boot_completed=1 且 /data/debian 存在
      │    ├─ mount proc / sys / dev / devpts
      │    ├─ 重建 /dev/ptmx
      │    ├─ tmpfs：/tmp、/var/log、apt lists/cache
      │    └─ 建 /run/sshd、/var/log/supervisor，写 resolv.conf
      │
      ├─ chroot-services
      │    ├─ 等 boot_completed 且 /data/debian/proc/version
      │    └─ setsid chroot → supervisord
      │
      └─ adb-tcp（可选，独立模块）
           └─ setprop service.adb.tcp.port 5555 + stop/start adbd
```

`setsid` 是关键：Magisk 在模块 `service.sh` 退出时会清理该进程组，普通 `nohup &`
会被一起杀掉。

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Android 7.0+（已在 SDK 24 + Magisk v23 验证） |
| Root | Magisk v23+，boot ramdisk 已 patch |
| chroot | Debian 位于 `/data/debian` |
| 主机 | Linux，`adb` 可用，设备已授权 |
| 设备内 | `supervisor` 已安装（`apt install supervisor`） |

## Android 网络权限（重要）

Android 内核启用了 `CONFIG_ANDROID_PARANOID_NETWORK`：**即使 root**，AF_INET/raw socket
也只放行 GID `3003(inet)` / `3004(net_raw)` 的进程。典型症状：

- chroot 里 `ping` 报 `socket: 权限不够`，或能 ping IP 但不能解析域名
- `apt-get update` 报 `Temporary failure resolving ...`（apt 下载进程以 `_apt` 运行，GID 65534）

`install.sh` 部署时会自动处理：

- `/data/debian/etc/group` 补 `inet:x:3003:root`、`net_raw:x:3004:root`
- `_apt` 的 GID 改为 3003
- 写 `/etc/apt/apt.conf.d/99sandbox`（`APT::Sandbox::User "root";`）
- 开机时由 `chroot-mount` 写 `resolv.conf`（等 `net.dns1`，兜底默认网关 / `223.5.5.5`）

> 改组后需要**新开 SSH 会话**（或 `su - root`）才生效。`ping localhost` / `127.0.0.1`
> 100% loss 是 Android 系统本身的行为（系统自带 ping 也一样），不是 DNS 问题；回环请用 TCP 验证。

详细排查见 [docs/troubleshooting.md](docs/troubleshooting.md)。

## 快速开始

```bash
git clone <this-repo> && cd magisk-chroot-boot

# 可选：放自己的服务定义（不会被提交）
mkdir -p config
cp templates/service.conf.example config/services.conf
$EDITOR config/services.conf

./install.sh

adb reboot
sleep 80
./scripts/healthcheck.sh
./scripts/status.sh
```

`install.sh` 的行为：

1. 检查 adb、root、`/data/debian`；
2. 把 `modules/*` 推到 `/data/adb/modules/`（含可选的 `adb-tcp`）；
3. 服务配置按优先级选择：
   - 本地有 `config/services.conf` → 用它；
   - 设备上已有 `/data/debian/etc/supervisor/conf.d/chroot.conf` → 保留不动；
   - 都没有 → 安装 `supervisor/chroot.conf.tpl` 里的示例；
4. 修权限并清理临时文件。

## 常用命令

```bash
./scripts/status.sh                 # Magisk 版本 + supervisor 状态
./scripts/healthcheck.sh            # supervisor 健康检查（可加端口）
PORTS="22 8080" ./scripts/healthcheck.sh
./scripts/logs.sh <name> 100        # 某个服务最近 100 行
./scripts/logs.sh supervisord       # supervisor 主日志
./scripts/backup.sh                 # 备份模块与配置
./build.sh                          # 打包成 Magisk 可安装 zip
```

设备内直接操作：

```bash
CTL="/data/debian/bin/busybox chroot /data/debian /usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf"
$CTL status
$CTL restart <name>
$CTL stop all
```

## 目录结构

```text
.
├── install.sh / uninstall.sh
├── build.sh                       # 生成 dist/*.zip
├── modules/
│   ├── chroot-mount/              # 挂载模块
│   ├── chroot-services/           # supervisord 引导模块
│   └── adb-tcp/                   # 可选：adb over TCP
├── meta/META-INF/                 # Magisk 安装器模板
├── supervisor/chroot.conf.tpl     # 示例服务配置
├── scripts/                       # status / logs / healthcheck / backup / device-install
├── templates/service.conf.example # 新增服务的模板
├── config/                        # 本地真实配置与密钥（gitignore）
└── docs/                          # 架构、运维、排错、Magisk 修复
```

## 文档

- [docs/architecture.md](docs/architecture.md) — 启动链路、职责边界、路径
- [docs/operations.md](docs/operations.md) — 加服务、升级、备份、回滚
- [docs/troubleshooting.md](docs/troubleshooting.md) — 已知坑与排查表
- [docs/magisk-v23-repair.md](docs/magisk-v23-repair.md) — Magisk 安装残缺时的修复方法
- [docs/legacy-autoboot.md](docs/legacy-autoboot.md) — 旧版 AutoBoot.apk 方案细节（已弃用）

## 安全

- 仓库不含真实服务配置；`config/` 整个目录被 gitignore。
- `adb tcp 5555` 仍使用 adb 密钥认证（`ro.build.type=user` 的机器无法像调试版那样关闭），
  但仍建议只在可信内网开启；不需要就禁用模块：
  `touch /data/adb/modules/adb-tcp/disable` 后重启，或直接删除该模块目录。
- 服务自己的密钥放在 chroot 内，不要写进模块或本仓库。

## License

MIT
