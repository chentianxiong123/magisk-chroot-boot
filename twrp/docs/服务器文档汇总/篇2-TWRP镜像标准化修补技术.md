# 篇2：TWRP 镜像标准化修补技术 —— bootimg 格式、注入脚本与验证

> 时间：2026-11-02
> 产物：`twrp-hooked.img`（sha1 `24e5324b02d6e562`，18,780,160 B）
> 基座：干净原版 TWRP（`twrp.img`，18,814,976 B，Jan 2024）
> 本篇聚焦：**如何解包 TWRP、注入钩子、重打包、验证**（纯技术，不含 hybrid 合成）

---

## 一、目标

给 TWRP 加一个**开机钩子**：开机时自动执行 `/data/local/tmp/twrp-hooks/*.sh`（按文件名顺序）。以后所有开机脚本丢进该目录即可，**无需再改镜像、无需重刷**。

## 二、原理（两层）

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

---

## 三、bootimg 格式（v0，魅蓝 Note5 用这个）

### 3.1 头部结构（**1632 字节**，不是 1728）

```
偏移 0    : magic "ANDROID!" (8 字节)
偏移 8    : 10 × u32（kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, 
                       second_size, second_addr, tags_addr, page_size, 
                       hdr_ver, os_version）= 40 字节
偏移 48   : name[16]
偏移 64   : cmdline[512]
偏移 576  : id[32]  ← 前 20 字节 = SHA1(kernel || ramdisk || second)
偏移 608  : extra_cmdline[1024]  ← **v0 从 608 起，不是 704**
总长      : 1632 字节
```

### 3.2 数据段偏移计算

```python
def align(n, a): return n + (-n % a)

kernel_offset = page_size              # 通常 2048 或 0x800
ramdisk_offset = kernel_offset + align(kernel_size, page_size)
second_offset = ramdisk_offset + align(ramdisk_size, page_size)
```

**魅蓝 Note5 TWRP 实测参数**：
- kernel_addr = 0x40080000
- ramdisk_addr = 0x45000000
- tags_addr = 0x44000000
- page_size = 0x800（2048）
- hdr_ver = 0（v0）
- cmdline = `bootopt=64S3,32N2,64N2 buildvariant=eng`

---

## 四、标准化修补过程

### 4.1 步骤 1 —— 解包 boot image（v0）

解析头部，提取 kernel + ramdisk + second：

```python
import struct, gzip, hashlib

d = open(IN_IMG, 'rb').read()
assert d[0:8] == b'ANDROID!', "not a boot image"

(ks, ka, rs, ra, ss, sa, tags, ps, hv, osv) = struct.unpack_from('<10I', d, 8)
name = d[48:64].split(b'\0')[0]
cmdline = d[64:576].split(b'\0')[0]
id_field = d[576:608]
extra = d[608:1632]

ko = ps
ro = ko + align(ks, ps)
so = ro + align(rs, ps)

kernel = d[ko:ko+ks]
ramdisk = d[ro:ro+rs]
second = d[so:so+ss] if ss else b''
```

### 4.2 步骤 2 —— 解压 ramdisk

```python
raw = gzip.decompress(ramdisk) if ramdisk[:2] == b'\x1f\x8b' else ramdisk
```

### 4.3 步骤 3 —— 解析 cpio（newc 格式）

cpio newc 头：`070701` + 13 × 8 字节 ASCII hex 字段（110 字节头）+ name + 4 对齐 + data + 4 对齐

```python
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
        entries.append({'name': nm, 'ino': ino, 'mode': mode, 'data': content, ...})
        off = (dstart + fsize + 3) & ~3
    return entries
```

### 4.4 步骤 4 —— 注入钩子 + 补 init.rc

**注入 ①**：`/sbin/twrp-hook.sh`（0755）= runner 内容

**注入 ②**：init.rc 追加 service 块（若还没有 twrphook）：

```python
SERVICE = (
    b"\n# TWRP boot hook: runs /data/local/tmp/twrp-hooks/*.sh at boot\n"
    b"service twrphook /sbin/twrp-hook.sh\n"
    b"    class default\n"
    b"    oneshot\n"
    b"    seclabel u:r:recovery:s0\n"
)

for e in entries:
    if e['name'] == 'sbin/twrp-hook.sh':
        e['data'] = hook_data; e['mode'] = 0o100755; hook_added = True
    if e['name'] == 'init.rc':
        if b'twrphook' not in e['data']:
            e['data'] = e['data'] + SERVICE; init_patched = True
```

### 4.5 步骤 5 —— 重建 cpio

```python
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
```

**关键**：
- cpio trailer 必须 `TRAILER!!!`
- `cpio_entry({})` 要默认 mode（否则 KeyError）

### 4.6 步骤 6 —— 重打包 boot image

```python
new_ramdisk = gzip.compress(new_cpio, 9)

digest = hashlib.sha1(kernel + new_ramdisk + second).digest()
new_id = digest + b'\0' * (32 - len(digest))

hdr = b'ANDROID!' + struct.pack('<10I', ks, ka, len(new_ramdisk), ra, ss, sa, tags, ps, hv, osv)
hdr += name + b'\0' * (16 - len(name))
hdr += cmdline + b'\0' * (512 - len(cmdline))
hdr += new_id
hdr += extra
assert len(hdr) == 1632

out = hdr + b'\0' * (ps - len(hdr) % ps)
out += kernel + b'\0' * (align(len(kernel), ps) - len(kernel))
out += new_ramdisk + b'\0' * (align(len(new_ramdisk), ps) - len(new_ramdisk))
open(OUT_IMG, 'wb').write(out)
```

**关键**：`id[0..4] = SHA1(kernel || ramdisk || second)`（20 字节），其余 0 —— bootloader 校验能过。

---

## 五、完整脚本

### 5.1 `repack_twrp_hook.py`（解包+注入+重打包，114 行）

见 `/home/a1/桌面/文档/hybrid钩子/repack_hybrid_hook_v0.py`（v0 适配版）

### 5.2 `verify.py`（验证脚本）

逐项比对，**全 OK 才算过**：

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

### 5.3 `twrp-hook.sh`（runner，进镜像 `/sbin/`）

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
    sh "$f" 2>&1 | grep -v "^Aborted" >> "$LOG"
done

echo "=== twrp-hook done $(date) ===" >> "$LOG"
```

**关键**：`sh "$f" 2>&1 | grep -v "^Aborted" >> "$LOG"` —— 过滤 wpa_cli segfault 刷屏。

---

## 六、踩过的坑（全部记录）

### 6.1 v0 boot 头是 1632 字节

`extra_cmdline[1024]` 从偏移 **608** 起，不是 704（v1 才是 704）。

**魅蓝 Note5 是 v0**，现有 `repack_twrp_hook.py` 是 v1（1728B）会失败，要用 `repack_hybrid_hook_v0.py`（1632B 适配）。

### 6.2 cpio trailer 必须 `TRAILER!!!`

`cpio_entry({})` 要默认 mode（否则 KeyError）。

### 6.3 id = SHA1(kernel||ramdisk||second)

20 字节填 `id[0..4]`，其余 0 —— bootloader 校验能过。

### 6.4 项目盘（Z:）是只读

`R` 属性，改文件先 `attrib -R`；工作放本地可写目录。

### 6.5 Magisk 不能用于此

Magisk 检测 `/sbin/recovery` 就 `RecoveryInit` abort（还原 init），不在 TWRP 里跑模块。所以纯 init.rc 钩子。

### 6.6 别用 `on boot` 的 `exec`

同步阻塞 init，用 `service`+`class default`（异步）。

---

## 七、验证清单（逐项比对）

```python
# verify.py 核心检查
1. 头部字段（kernel_addr/ramdisk_addr/page/hdr_ver/cmdline）
2. 内核逐字节（k_orig == k_new）
3. 新 ramdisk（init.rc 含 service twrphook）
4. SHA1 id 计算
5. 大小（产物 vs 原版）
```

**全 OK 才可用**，任何一个 BAD 都不能刷入。

---

## 八、部署

```bash
# 刷入 recovery 分区
adb push hybrid-hooked.img /data/local/tmp/
adb shell su -c "dd if=/data/local/tmp/hybrid-hooked.img of=/dev/block/mmcblk0p1; sync"

# 进 TWRP 后，丢脚本：
adb shell mkdir -p /data/local/tmp/twrp-hooks
adb push 你的脚本.sh /data/local/tmp/twrp-hooks/01-xxx.sh
adb shell chmod 755 /data/local/tmp/twrp-hooks/*.sh

# 下次开机自动按名序跑；日志：
adb shell cat /data/local/tmp/twrp-hook.log
```

---

## 九、回滚

```bash
# 原版 TWRP 备份
dd if=/mnt/shared/新建文件夹/twrp.img of=/dev/block/mmcblk0p1
```

---

## 十、一句话总结

**标准化修补 = 解包 bootimg（v0 1632B 头）→ 解压 ramdisk → 解析 cpio → 注入 runner + 补 init.rc → 重建 cpio + 重打包 → 验证（全 OK）→ 部署（只刷 recovery p1）。**

**关键**：
- v0 头 1632B（不是 1728B）
- cpio trailer 必须 `TRAILER!!!`
- id = SHA1(kernel||ramdisk||second) 20 字节
- Magisk 不能用于 recovery（会 abort）
- 用 `service`+`class default`（异步），不用 `exec`（同步阻塞）
- runner 加 `grep -v "^Aborted"` 过滤 segfault 刷屏

---

*本篇是 TWRP 镜像修补技术。背景与路线见篇1，MTK 驱动逆向见篇3，hybrid 全自动钩子实战见篇4。*
