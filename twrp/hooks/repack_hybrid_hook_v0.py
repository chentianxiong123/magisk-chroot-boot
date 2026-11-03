#!/usr/bin/env python3
# repack_hybrid_hook_v0.py — 给 hybrid.img (v0头,1632B) 注入 twrp-hook + 重打包
# 用法: python3 repack_hybrid_hook_v0.py <in_hybrid.img> <twrp-hook.sh> <out.img>
# 适配 v0 头 (id=5×u32, 头1632B, extra_cmdline 从608起) —— 这台 TWRP/hybrid 是 v0

import struct, gzip, sys, hashlib

IN_IMG, HOOK_SH, OUT_IMG = sys.argv[1], sys.argv[2], sys.argv[3]

# ---------- 1. 解析 v0 boot image 头 (1632B) ----------
d = open(IN_IMG, 'rb').read()
assert d[0:8] == b'ANDROID!', "not a boot image"
# v0: 10×u32 (ks,ka,rs,ra,ss,sa,tags,ps,hv,osv) at 8
(ks, ka, rs, ra, ss, sa, tags, ps, hv, osv) = struct.unpack_from('<10I', d, 8)
name = d[48:64].split(b'\0')[0]
cmdline = d[64:576].split(b'\0')[0]
tail = d[576:1632]  # id(5×u32=20B) + 其余0 + extra_cmdline —— 整块保留,不解析

def align(n, a): return n + (-n % a)

ko = ps
ro = ko + align(ks, ps)
so = ro + align(rs, ps)
kernel = d[ko:ko+ks]
ramdisk = d[ro:ro+rs]
second = d[so:so+ss] if ss else b''
print(f"kernel={ks} ramdisk={rs} page={ps} hv={hv}")
print(f"kernel@{ko} ramdisk@{ro} second@{so} tail={len(tail)}B")

# ---------- 2. 解压 ramdisk → cpio ----------
raw = gzip.decompress(ramdisk) if ramdisk[:2] == b'\x1f\x8b' else ramdisk

# ---------- 3. 解析 cpio (newc) ----------
def parse_cpio(data):
    off, entries = 0, []
    while off + 110 <= len(data):
        if data[off:off+6] not in (b'070701', b'070702'):
            break
        f = [int(data[off+6+i*8:off+6+(i+1)*8], 16) for i in range(13)]
        (ino, mode, uid, gid, nlink, mtime, fsize, dmaj, dmin, rmaj, rmin, nsz, ck) = f
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
    b"\n# hybrid boot hook: runs /data/local/tmp/twrp-hooks/*.sh at boot\n"
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
assert hook_added and init_patched, "注入失败(找不到init.rc?)"

# ---------- 5. 重建 cpio (newc) ----------
def cpio_entry(e, trailer=False):
    name_b = e['name'].encode() if not trailer else b'TRAILER!!!'
    nsz = len(name_b) + 1
    hdr = b'070701' + b''.join(b'%08X' % v for v in (
        e.get('ino', 1), e.get('mode', 0), e.get('uid', 0), e.get('gid', 0),
        1 if trailer else e.get('nlink', 1), e.get('mtime', 0), len(e.get('data', b'')),
        e.get('dmaj', 0), e.get('dmin', 0), e.get('rmaj', 0), e.get('rmin', 0),
        nsz, 0))
    out = hdr + name_b + b'\0'
    out += b'\0' * ((4 - len(out) % 4) % 4)
    out += e.get('data', b'')
    out += b'\0' * ((4 - len(out) % 4) % 4)
    return out

new_cpio = b''.join(cpio_entry(e) for e in new_entries) + cpio_entry({}, trailer=True)
new_ramdisk = gzip.compress(new_cpio, 9)
print(f"新 cpio: {len(new_cpio)} → gzip: {len(new_ramdisk)}")

# ---------- 6. 重打包 v0 boot image ----------
# id = SHA1(kernel || ramdisk || second) 前20B, 填 tail 开头(保留 tail 其余)
digest = hashlib.sha1(kernel + new_ramdisk + second).digest()
new_tail = digest + tail[20:]  # 前20B新id, 其余原tail保留

hdr = b'ANDROID!' + struct.pack('<10I', ks, ka, len(new_ramdisk), ra, ss, sa, tags, ps, hv, osv)
hdr += name + b'\0' * (16 - len(name))
hdr += cmdline + b'\0' * (512 - len(cmdline))
hdr += new_tail
assert len(hdr) == 1632, f"header {len(hdr)} (v0应为1632)"

out = hdr + b'\0' * (ps - len(hdr) % ps)
out += kernel + b'\0' * (align(len(kernel), ps) - len(kernel))
out += new_ramdisk + b'\0' * (align(len(new_ramdisk), ps) - len(new_ramdisk))
open(OUT_IMG, 'wb').write(out)
print(f"写出: {OUT_IMG} ({len(out)} B, ramdisk={len(new_ramdisk)})")

# ---------- 7. 自校验 ----------
print("\n=== 自校验 ===")
ok = lambda b: "OK" if b else "BAD"
h2 = open(OUT_IMG, 'rb').read()
print(" 头部一致(除ramdisk_size/id):", ok(h2[:8]==b'ANDROID!' and struct.unpack_from('<I',h2,8)[0]==ks and struct.unpack_from('<I',h2,16)[0]==len(new_ramdisk)))
print(" 内核一致:", ok(h2[ko:ko+ks] == kernel))
rd2 = h2[ro:ro+len(new_ramdisk)]
raw2 = gzip.decompress(rd2) if rd2[:2] == b'\x1f\x8b' else rd2
ir_ok = b'service twrphook /sbin/twrp-hook.sh' in raw2
hook_ok = b'/data/local/tmp/twrp-hooks' in raw2
print(" init.rc含service twrphook:", ok(ir_ok))
print(" sbin/twrp-hook.sh含钩子目录:", ok(hook_ok))
print(" id=SHA1(k||rd||second)前20:", ok(h2[576:596] == digest))
print(f" 大小: {len(out)}B (原 {len(d)}B)")
