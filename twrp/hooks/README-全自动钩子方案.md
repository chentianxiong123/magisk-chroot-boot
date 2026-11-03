# hybrid recovery 全自动钩子方案（最终版，稳定）

**2026-11-02** — 魅蓝 Note5，hybrid recovery（normal 内核 + TWRP 调度）+ 开机钩子

## ✅ 最终状态（全自动化，重启即可用）

| 项 | 自动启动 | 验证 |
|---|---|---|
| fuelgauged（电池） | ✅ | capacity=100 |
| wifi（wpa_supplicant） | ✅ | LOWER_UP + COMPLETED |
| chroot 文件系统 | ✅ | /data/debian/proc 挂好 |
| supervisord（cpa/freellmapi/sshd/batt） | ✅ | supervisorctl 全 RUNNING |
| sshd | ✅ | ssh meilan-phone1 |
| 静态 IP + 路由 | ✅ | 192.168.31.213/24 + default via .1 |
| 无线 adb | ✅ | adb connect 192.168.31.213:5555 |
| 省电调度（cpu4-7 睡死） | ✅ | |
| **日志干净** | ✅ | 0 Aborted（runner 过滤） |

## 钩子目录（持久化）

`/data/local/tmp/twrp-hooks/`，按文件名顺序执行：

| 文件 | 作用 |
|---|---|
| 01-fuelgauged.sh | 启动电池守护（`&` 后台） |
| 02-wifi.sh | wmt+固件+wpa_supplicant+**reassociate 一次** |
| 20-chroot-mount.sh | 挂 Debian chroot |
| 30-chroot-services.sh | 起 supervisord（照面具原文，去等框架循环） |
| 40-adbd-tcp.sh | 开无线 adb（setprop 5555 + kill adbd） |
| 50-static-ip.sh | 轮询 `ip link` 等 LOWER_UP，配 IP+路由 |
| **twrp-hook.sh** | runner（在 `/sbin/`，已加 `grep -v Aborted` 过滤） |

## 关键修复（这次踩的坑）

1. **wpa_cli 在 hybrid 会 segfault**（stderr 刷 "Aborted"）：
   - 钩子里只跑一次 reassociate（不循环）
   - runner 加 `grep -v "^Aborted"` 过滤
2. **wpa_supplicant 不会自动关联**：必须 wpa_cli reassociate 一次触发
3. **IP 配置拆到独立钩子**（50），不依赖 wpa_cli
4. **64 位二进制要显式设 lib64**（runner 默认只设 32 位库）

## 使用

```bash
ssh meilan-phone1                       # SSH 登录
adb connect 192.168.31.213:5555        # 无线 adb
```

## 改钩子

```bash
adb push xxx.sh /data/local/tmp/twrp-hooks/
adb shell chmod 755 /data/local/tmp/twrp-hooks/*.sh
adb shell reboot recovery
```

## 回滚

recovery 分区 `mmcblk0p1`（32MB），boot 没动。钩子脚本备份在 `/home/a1/桌面/文档/hybrid钩子/`。

## 踩坑总结

见 `2026-11-02-安卓Recovery钩子脚本规范-实测踩坑总结.md`
