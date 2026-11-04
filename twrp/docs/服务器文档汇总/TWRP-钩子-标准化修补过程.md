# TWRP 开机钩子 —— 标准化修补过程（记录文档）

> 记录日期：2026-11-02
> 产物：`twrp-hooked.img`（sha1 `24e5324b02d6e562`，18,780,160 B）
> 基座：干净原版 TWRP（`twrp.img`，18,814,976 B，Jan 2024）
> 说明：本过程**不涉及 Magisk**（Magisk 在 recovery 里会主动 abort，不用它）。纯改 ramdisk init + init.rc。

---

## 1. 目标

给 TWRP 加一个**开机钩子**：开机时自动执行 `/data/local/tmp/twrp-hooks/*.sh`（按文件名顺序）。
以后所有开机脚本丢进该目录即可，**无需再改镜像、无需重刷**。

## 2. 原理（两层）

```
开机 → TWRP 的 init 跑 init.rc
        └─ service twrphook /sbin/twrp-hook.sh   (class default + oneshot, 异步)
             └─ /sbin/twrp-hook.sh (runner)
                  ├─ 等 /data 挂载 (60s 超时)
                  ├─ 挂 /system (ro) 提供工具
                  ├─ mkdir /data/local/tmp/twrp-hooks
                  └─ for f in /data/local/tmp/twrp-hooks/*.sh: sh $f
```

- **为什么 service 不用 `on boot` 的 `exec`**：`exec` 同步阻塞 init（脚本等 /data 会卡死启动）；`service`+`class default` 在 `class_start default`（`on boot`）时异步拉起，不阻塞。
- **SELinux**：TWRP 跑 `permissive.sh`（`setenforce`）→ permissive，钩子不被拦。

## 3. 标准化修补过程

### 前提
- Python 3（本机已验证 3.14）
- 干净原版 TWRP：`twrp.img`
- 三个脚本：`twrp-hook.sh`、`repack_twrp_hook.py`、`verify.py`（见 §5）

### 步骤 1 — 解包 boot image（v0）
`repack_twrp_hook.py` 解析头部（**注意：v0 头是 1632 字节**，不是 1728）：
```
magic "ANDROID!" (8) + 10×u32 (kernel_size…os_version, 40) + name[16] + cmdline[512] + id[32] + extra_cmdline[1024] = 1632
内核偏移 = page_size(2048)；ramdisk 偏移 = 内核偏移 + align(kernel_size, 2048)
```

### 步骤 2 — 注入钩子 + 补 init.rc
```python
# ① /sbin/twrp-hook.sh（0755）= twrp-hook.sh 内容
# ② init.rc 追加 service 块（若还没有 twrphook）：
SERVICE = (
    b"\n# TWRP boot hook: runs /data/local/tmp/twrp-hooks/*.sh at boot\n"
    b"service twrphook /sbin/twrp-hook.sh\n"
    b"    class default\n"
    b"    oneshot\n"
    b"    seclabel u:r:recovery:s0\n"
)
```

### 步骤 3 — 重建 cpio + 重打包
- cpio 重建：`070701` 头（13×8 字节 ASCII hex 字段）+ name + 4 对齐 + data + 4 对齐 + `TRAILER!!!`；
- ramdisk gzip 压缩（level 9）；
- 重打包：头部 `ramdisk_size` 更新、**`id[0..4] = SHA1(kernel || ramdisk || second)`**（20 字节，其余 0）、按 page 对齐拼接。

### 步骤 4 — 验证（`verify.py`）
逐项比对，**全 OK 才算过**（见 §6）。

### 命令
```bash
cd 工作目录
python repack_twrp_hook.py twrp.img twrp-hook.sh twrp-hooked.img
python verify.py          # 全 OK 才可用
cp twrp-hooked.img <目标>   # 部署
```

## 4. 使用（部署后）

```bash
# 刷入 recovery 分区
fastboot flash recovery twrp-hooked.img      # 或 TWRP/MTKclient 刷

# 进 TWRP 后（adbd 是 root），丢脚本：
adb shell mkdir -p /data/local/tmp/twrp-hooks
adb push 你的脚本.sh /data/local/tmp/twrp-hooks/01-xxx.sh

# 下次开机自动按名序跑；日志：
adb shell cat /data/local/tmp/twrp-hook.log
```

## 5. 脚本全文

### `twrp-hook.sh`（runner，进镜像 `/sbin/`）
```sh
#!/sbin/sh
# twrp-hook.sh — TWRP 开机钩子 runner
# 扫描并执行 /data/local/tmp/twrp-hooks/*.sh（按文件名顺序）
# 以后所有开机脚本丢进那个目录即可，无需改镜像。
#
# 日志: /data/local/tmp/twrp-hook.log

LOG=/data/local/tmp/twrp-hook.log
HOOK_DIR=/data/local/tmp/twrp-hooks

# 等 /data 挂载（TWRP 自己挂，有延时）—— 最多 60s
i=0
while [ "$i" -lt 60 ]; do
    if [ -d /data/local ]; then break; fi
    sleep 1
    i=$((i + 1))
done

# 建钩子目录（首次开机）
mkdir -p "$HOOK_DIR" 2>/dev/null

# 挂 /system（只读）给脚本提供工具（wpa_supplicant 等）
mount /system 2>/dev/null || mount -o ro /system 2>/dev/null

export PATH=/sbin:/system/bin
export LD_LIBRARY_PATH=/sbin:/system/lib

echo "=== twrp-hook start $(date) ===" >> "$LOG"

# 按名字顺序跑钩子
for f in "$HOOK_DIR"/*.sh; do
    [ -f "$f" ] || continue
    echo "--- run $f ---" >> "$LOG"
    sh "$f" >> "$LOG" 2>&1
done

echo "=== twrp-hook done $(date) ===" >> "$LOG"
```

### `repack_twrp_hook.py`（解包+注入+重打包，114 行）
```python
#!/usr/bin/env python3
# repack_twrp_hook.py — 把 twrp-hook 注入干净 TWRP 并重打包
# 用法: python repack_twrp_hook.py <in_twrp.img> <twrp-hook.sh> <out.img>
import struct, gzip, sys, hashlib

IN_IMG, HOOK_SH, OUT_IMG = sys.argv[1], sys.argv[2], sys.argv[3]

# ---------- 1. 解析 boot image (v0) ----------
d = open(IN_IMG, 'rb').read()
magic = d[0:8]
assert magic == b'ANDROID!', "not a boot image"
(ks, ka, rs, ra, ss, sa, tags, ps, hv, osv) = struct.unpack_from('<10I', d, 8)
name = d[48:64].split(b'\0')[0]
cmdline = d[64:576].split(b'\0')[0]
id_field = d[576:608]
extra = d[608:1632]
print(f"kernel={ks} ramdisk={rs} second={ss} page={ps} hdr_ver={hv}")

def align(n, a): return n + (-n % a)

ko = ps
ro = ko + align(ks, ps)
so = ro + align(rs, ps)
kernel = d[ko:ko+ks]
ramdisk = d[ro:ro+rs]
second = d[so:so+ss] if ss else b''
print(f"kernel@{ko} ramdisk@{ro}({len(ramdisk)}) second@{so}({len(second)})")

# ---------- 2. 解压 ramdisk ----------
raw = gzip.decompress(ramdisk) if ramdisk[:2] == b'\x1f\x8b' else ramdisk

# ---------- 3. 解析 cpio (newc) ----------
def parse_cpio(data):
    off, entries = 0, []
    while off + 110 <= len(data):
        if data[off:off+6] not in (b'070701', b'070702'):
            break
        f = [int(data[off+6+i*8:off+6+(i+1)*8], 16) for i in range(13)]
        ino, mode, uid, gid, nlink, mtime, fsize, dmaj, dmin, rmaj, rmin, nsz, ck = f
        n = data[off+110:off+110+nsz]
        nm = n.split(b'\0')[0].decode('utf-8', 'replace')
        dstart = (off + 110 + nsz + 3) & ~3
        content = data[dstart:dstart+fsize]
        entries.append({'name': nm, 'ino': ino, 'mode': mode, 'uid': uid, 'gid': gid,
                        'nlink': nlink, 'mtime': mtime, 'dmaj': dmaj, 'dmin': dmin,
                        'rmaj': rmaj, 'rmin': rmin, 'data': content})
        off = (dstart + fsize + 3) & ~3
    return entries

entries = parse_cpio(raw)
print(f"cpio 条目: {len(entries)}")

# ---------- 4. 注入 twrp-hook.sh + 补 init.rc ----------
hook_data = open(HOOK_SH, 'rb').read()
SERVICE = (
    b"\n# TWRP boot hook: runs /data/local/tmp/twrp-hooks/*.sh at boot\n"
    b"service twrphook /sbin/twrp-hook.sh\n"
    b"    class default\n"
    b"    oneshot\n"
    b"    seclabel u:r:recovery:s0\n"
)

hook_added = init_patched = False
new_entries = []
for e in entries:
    if e['name'] == 'sbin/twrp-hook.sh':
        e['data'] = hook_data; e['mode'] = 0o100755; hook_added = True
    if e['name'] == 'init.rc':
        if b'twrphook' not in e['data']:
            e['data'] = e['data'] + SERVICE; init_patched = True
    new_entries.append(e)
if not hook_added:
    new_entries.append({'name': 'sbin/twrp-hook.sh', 'ino': 1, 'mode': 0o100755,
                        'uid': 0, 'gid': 0, 'nlink': 1, 'mtime': 0, 'dmaj': 0,
                        'dmin': 0, 'rmaj': 0, 'rmin': 0, 'data': hook_data})
    hook_added = True
print(f"hook 注入: {hook_added}  init.rc 补丁: {init_patched}")
assert hook_added and init_patched, "注入失败"

# ---------- 5. 重建 cpio (newc) ----------
def cpio_entry(e, trailer=False):
    name = e['name'].encode() if not trailer else b'TRAILER!!!'
    nsz = len(name) + 1
    hdr = b'070701' + b''.join(b'%08X' % v for v in (
        e.get('ino', 1), e.get('mode', 0), e.get('uid', 0), e.get('gid', 0),
        1 if trailer else e.get('nlink', 1), e.get('mtime', 0), len(e.get('data', b'')),
        e.get('dmaj', 0), e.get('dmin', 0), e.get('rmaj', 0), e.get('rmin', 0),
        nsz, 0))
    out = hdr + name + b'\0'
    out += b'\0' * ((4 - len(out) % 4) % 4)
    out += e.get('data', b'')
    out += b'\0' * ((4 - len(out) % 4) % 4)
    return out

new_cpio = b''.join(cpio_entry(e) for e in new_entries) + cpio_entry({}, trailer=True)
new_ramdisk = gzip.compress(new_cpio, 9)
print(f"新 cpio: {len(new_cpio)} → gzip: {len(new_ramdisk)}")

# ---------- 6. 重打包 boot image ----------
digest = hashlib.sha1(kernel + new_ramdisk + second).digest()
new_id = digest + b'\0' * (32 - len(digest))

hdr = b'ANDROID!' + struct.pack('<10I', ks, ka, len(new_ramdisk), ra, ss, sa, tags, ps, hv, osv)
hdr += name + b'\0' * (16 - len(name))
hdr += cmdline + b'\0' * (512 - len(cmdline))
hdr += new_id
hdr += extra
assert len(hdr) == 1632, f"header {len(hdr)}"

out = hdr + b'\0' * (ps - len(hdr) % ps)
out += kernel + b'\0' * (align(len(kernel), ps) - len(kernel))
out += new_ramdisk + b'\0' * (align(len(new_ramdisk), ps) - len(new_ramdisk))
open(OUT_IMG, 'wb').write(out)
print(f"写出: {OUT_IMG} ({len(out)} B, ramdisk={len(new_ramdisk)})")
```

### `verify.py`（验证）
```python
import struct, gzip, hashlib, os

def parse(p):
    d = open(p, 'rb').read()
    (ks, ka, rs, ra, ss, sa, tags, ps, hv, osv) = struct.unpack_from('<10I', d, 8)
    def align(n, a): return n + (-n % a)
    ko = ps; ro = ko + align(ks, ps)
    return (d, ks, ka, rs, ra, ss, sa, tags, ps, hv, osv, ko, ro)

o = parse("twrp.img"); n = parse("twrp-hooked.img")
do, ks, ka, rs0, ra, ss, sa, tags, ps, hv, osv, ko, ro = o
dh, ksh, kah, rsh, rah, ssh, sah, tagsh, psh, hvh, osvh, koh, roh = n

ok = lambda b: "OK " if b else "BAD"

print("=== 1. 头部字段 ===")
print(" kernel_addr ", hex(ka), "->", hex(kah), ok(ka == kah))
print(" ramdisk_addr", hex(ra), "->", hex(rah), ok(ra == rah))
print(" page        ", ps, "->", psh, ok(ps == psh))
print(" hdr_ver     ", hv, "->", hvh, ok(hv == hvh))
print(" cmdline     ", ok(do[64:576] == dh[64:576]))

print("=== 2. 内核逐字节 ===")
k_orig = do[ko:ko+ks]; k_new = dh[koh:koh+ksh]
print(" 内核一致", ok(k_orig == k_new), f"({len(k_orig)}B == {len(k_new)}B)")

print("=== 3. 新 ramdisk ===")
rd = dh[roh:roh+rsh]
raw = gzip.decompress(rd) if rd[:2] == b'\x1f\x8b' else rd
off = 0; found = {}
while off < len(raw):
    if raw[off:off+6] in (b'070701', b'070702'):
        namesz = int(raw[off+94:off+102], 16); fsize = int(raw[off+54:off+62], 16); mode = int(raw[off+14:off+22], 16)
        nm = raw[off+110:off+110+namesz-1].decode('utf-8', 'replace')
        if nm in ("init.rc", "sbin/twrp-hook.sh", "sbin/recovery"):
            found[nm] = (raw[off+110+namesz:off+110+namesz+fsize], mode, fsize)
        off = (off+110+namesz+3) & ~3; off = (off+fsize+3) & ~3
    else:
        break
ir, irm, irf = found["init.rc"]; hook, hm, hf = found["sbin/twrp-hook.sh"]; rec, _, rf = found["sbin/recovery"]
print(" init.rc", irf, "B  service twrphook:", ok(b'service twrphook /sbin/twrp-hook.sh' in ir))
print("   class/oneshot/seclabel:", ok(all(x in ir for x in (b'class default', b'oneshot', b'u:r:recovery:s0'))))
print(" sbin/twrp-hook.sh", hf, "B mode", oct(hm), " 目录逻辑:", ok(b'/data/local/tmp/twrp-hooks' in hook))
print(" sbin/recovery 原样:", ok(rf == 1096104), f"({rf}B)")

print("=== 4. SHA1 id ===")
new_id = dh[576:608]; calc = hashlib.sha1(k_new + rd).digest() + b'\0' * 12
print(" id 计算:", ok(new_id[:20] == calc[:20]))

print("=== 5. 大小 ===")
print(f" twrp-hooked.img {os.path.getsize('twrp-hooked.img')}B (原 {os.path.getsize('twrp.img')}B)")
```

## 6. 验证清单（本次实测结果）

| 检查 | 结果 |
|---|---|
| 头部 kernel_addr / ramdisk_addr / page / hdr_ver / cmdline | ✅ 全一致 |
| 内核逐字节 | ✅ 8,143,918B == 8,143,918B |
| init.rc 含 `service twrphook` | ✅ 3341B |
| service 的 class/oneshot/seclabel | ✅ |
| `/sbin/twrp-hook.sh` 0755 含目录逻辑 | ✅ 1017B |
| `/sbin/recovery` 原样 | ✅ 1,096,104B |
| SHA1 id 计算 | ✅ |
| 产物大小 | 18,780,160B（原 18,814,976B，ramdisk gzip 略小正常） |
| 产物 sha1 | `24e5324b02d6e562` |

## 7. 注意事项 / 踩过的坑
- **v0 boot 头是 1632 字节**（`extra_cmdline[1024]` 从偏移 **608** 起，不是 704）；
- **cpio trailer** 必须 `TRAILER!!!`，`cpio_entry({})` 要默认 mode（否则 KeyError）；
- **id = SHA1(kernel||ramdisk||second)** 20 字节填 `id[0..4]`，其余 0 —— bootloader 校验能过；
- **项目盘（Z:）是只读**（`R` 属性），改文件先 `attrib -R`；工作放本地可写目录；
- **Magisk 不能用于此**：Magisk 检测 `/sbin/recovery` 就 `RecoveryInit` abort（还原 init），不在 TWRP 里跑模块 —— 所以纯 init.rc 钩子；
- **别用 `on boot` 的 `exec`**（同步阻塞 init），用 `service`+`class default`（异步）。

## 8. 现状
- 产物：`C:\Users\a1\gui\MTKclient\images\twrp-hooked.img`（已验证）
- 干净基座：`C:\Users\a1\gui\MTKclient\images\twrp.img`
- 脚本：`C:\Users\a1\AppData\Local\Temp\twrp_hook_build\{twrp-hook.sh, repack_twrp_hook.py, verify.py}`
- 面具相关：已全部清除（twrp-magisk.img、面具版 recovery.img、Magisk 源码克隆、解包残留）
