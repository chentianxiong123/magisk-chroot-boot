# 案例：chroot raw TUN 的 TCP 回包与 ICMP 丢失

日期：2026-10-23

环境（脱敏）：

- 服务器：Android 7 + Magisk + Debian chroot，chroot 内跑 EasyTier raw TUN
- 客户端：Android 13，未 root，EasyTier VpnService
- 测试端：Linux 宿主机
- 目标：客户端通过 TUN 访问 chroot 内的 TCP 服务

## 现象

- EasyTier peer 已连接（p2p，低延迟，0% loss）
- 客户端访问 chroot 内 TCP 服务端口不通
- 宿主机 `ping` 服务器 LAN IP 100% loss，但 TCP 正常

## 1. TCP 为什么不通

在服务器上查回包路由：

```bash
ip route get <TUN_PEER>
# <TUN_PEER> via <LAN_GW> dev wlan0  src <LAN_IP>
```

原因：

- chroot 里的 raw TUN 把 `<TUN_CIDR>` 路由写进 `main` 表
- Android netd 的策略路由先查 `wlan0`/`rmnet` 表，`main` 表默认轮不到
- SYN 能从 `tun0` 进来，但 SYN-ACK 查到的却是默认网关，从 `wlan0` 出去
- 客户端等的是隧道里的回包，握手失败

修复：

```bash
ip rule add to <TUN_CIDR> lookup main pref 9000
ip route get <TUN_PEER>
# <TUN_PEER> dev tun0  src <TUN_LOCAL_IP>
```

这条规则只匹配 `<TUN_CIDR>`，不影响 LAN、公网和其他服务。

## 2. ICMP 为什么不通

先排除内核和宿主机：

```bash
cat /proc/sys/net/ipv4/icmp_echo_ignore_all   # 0
cat /proc/sys/net/ipv4/conf/all/rp_filter     # 0
```

宿主机防火墙是 `ACCEPT`，ARP 正确，反向 ping（服务器 → 宿主机）也正常。

关键证据是服务器自己的 ICMP 计数：

```bash
grep ^Icmp: /proc/net/snmp
# ping 前：InEchos=61  OutEchoReps=61
# ping 后：InEchos=64  OutEchoReps=64
```

`InEchos +3`、`OutEchoReps +3`：内核收到了 3 个 echo request，也生成了 3 个
echo reply。但宿主机 nft prerouting 抓 ICMP，计数是 0 —— 回包根本没到宿主机。

继续看服务器 OUTPUT：

```text
-P OUTPUT ACCEPT
-A OUTPUT -j oem_out
-A OUTPUT -j firewall
-A OUTPUT -j fw_OUTPUT
...
```

```text
-N fw_OUTPUT
-A fw_OUTPUT -j fw_powersave
-A fw_OUTPUT -j fw_standby
-A fw_OUTPUT -j fw_dozable
```

```text
-N fw_powersave
-A fw_powersave -p tcp -m tcp --tcp-flags RST RST -j RETURN
-A fw_powersave -m owner --uid-owner 0-9999 -j RETURN
-A fw_powersave -m owner --uid-owner 1000 -j RETURN
...
-A fw_powersave -j DROP
```

根因：这些省电/Doze 链按 `--uid-owner` 白名单放行，最后 `-j DROP`。
**内核生成的 ICMP echo reply 没有 socket owner**，不匹配任何 UID 规则，直接
被 DROP；TCP 回包有 socket owner，所以能过。

修复：

```bash
iptables -I fw_OUTPUT 1 -p icmp --icmp-type echo-reply -j RETURN
```

验证：

```bash
ping -c3 <LAN_IP>
# 3 packets transmitted, 3 received, 0% packet loss
```

## 3. 中间方案：chroot-netfix + 10s watchdog

把两条规则做成可选模块 `chroot-netfix`：

- `service.sh` 等 `sys.boot_completed=1`，用 `setsid` 拉起 `watchdog.sh`
- watchdog 每 10s 重新读 `/data/adb/chroot-netfix.conf`
- 幂等检查/重加 `ip rule` 和 `iptables` 规则
- 配置在本地 `config/netfix.conf`（gitignored），仓库只有示例

重启后日志：

```text
[netfix] watchdog started pid=...
[netfix] added ip rule: to <TUN_CIDR> lookup main pref 9000
[netfix] inserted ICMP echo-reply RETURN at top of fw_OUTPUT
```

验证通过：回包走 `tun0`、ping 3/3、服务全部 RUNNING。

## 4. 为什么最后禁用 watchdog

10s 轮询能用，但不是没有成本：

- 一天最多 8640 次唤醒（深度休眠时 `sleep` 会被冻结，实际少一些）
- 每轮 spawn `ip`、`grep`、`iptables`、`head` 等短进程
- 有 ICMP 规则时每 10s 调一次 `iptables -S`，会碰 xtables 锁
- CPU 时间很小，对常驻跑 EasyTier 的机器占比很低，但唤醒不是零成本

这台服务器固定 Wi-Fi、不切网，ping 也不重要，所以没必要一直轮询。

## 5. 最终方案：一次性 ip rule

- 禁用 `chroot-netfix`：`touch /data/adb/modules/chroot-netfix/disable`
- 不再加 ICMP 规则，ping 保持不通（有意）
- 由 `chroot-network` 在开机写完 DNS 后加一次 `ip rule`
- 配置 `/data/adb/chroot-routes.conf`（本地 `config/routes.conf`，gitignored）：

```sh
RULE_CIDRS="<TUN_CIDR>"
RULE_PREF=9000
```

`chroot-network` 的逻辑：

1. 等 `sys.boot_completed=1` 和 `net.dns1`
2. 写 `/etc/resolv.conf`
3. 按 `chroot-routes.conf` 加一次 `ip rule`
4. 5s 后重试一次（等 netd 稳定）
5. 退出，不常驻

重启后验证（脱敏）：

```text
[chroot-network] route rule added: <TUN_CIDR> -> main (pref 9000)
9000: from all to <TUN_CIDR> lookup main
<TUN_PEER> dev tun0
fw_OUTPUT: no icmp rule
supervisor: 6 programs RUNNING
宿主机 ping <LAN_IP>: 100% loss（预期）
LAN TCP 管理端口: 正常
```

`chroot-netfix.log` 重启后没有新条目，说明模块确实没被加载；`chroot-network.log`
里能看到一次性路由规则。客户端 TUN 服务端口在最终重启后没有复测（客户端 App
当时没运行），但在启用 `chroot-netfix` 的阶段已经验证过。

## 6. 什么时候才需要 watchdog

- 经常切 Wi-Fi / 移动数据
- Doze/省电状态变化会刷掉规则
- 需要 ICMP 修复
- 不能接受规则丢失后手动补

这些情况再启用 `chroot-netfix`，或者把间隔从 10s 调到 60s/300s；更优雅的是
用 `ip monitor rule route` 做事件驱动，但 iptables 没有可靠 monitor，仍需兜底。

## 经验

- Android 的 `ip rule` 和普通 Linux 不一样：先看 `ip rule`，再看
  `ip route show table all`，不要只看 `ip route`
- `/proc/net/snmp` 的 `InEchos`/`OutEchoReps` 能区分“没收到 / 没回 / 回了但路上丢了”
- `--uid-owner` 白名单对内核生成的包（ICMP reply、部分 RST）不友好
- 不要用 `ping` 当唯一健康检查：ROM 防火墙可能只丢 ICMP，TCP 是好的
- 固定网络用一次性规则，动态网络才上 watchdog；轮询不是免费的
