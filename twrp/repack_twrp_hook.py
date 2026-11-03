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
id_field = d[576:704]
extra = d[704:1728]
print(f"kernel={ks} ramdisk={rs} second={ss} page={ps} hdr_ver={hv}")

def align(n, a): return n + (-n % a)

# 偏移
ko = ps
ro = ko + align(ks, ps)
so = ro + align(rs, ps)
kernel = d[ko:ko+ks]
ramdisk = d[ro:ro+rs]
second = d[so:so+ss] if ss else b''
print(f"kernel@{ko} ramdisk@{ro}({len(ramdisk)}) second@{so}({len(second)})")

# ---------- 2. 解压 ramdisk → cpio ----------
raw = gzip.decompress(ramdisk) if ramdisk[:2] == b'\x1f\x8b' else ramdisk
open('/tmp/_ramdisk.cpio', 'wb').write(raw)

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
        e.get('ino', 1), e['mode'], e.get('uid', 0), e.get('gid', 0),
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
# id = SHA1(kernel || ramdisk || second)
digest = hashlib.sha1(kernel + new_ramdisk + second).digest()
new_id = digest + b'\0' * (32 - len(digest))

hdr = b'ANDROID!' + struct.pack('<10I', ks, ka, len(new_ramdisk), ra, ss, sa, tags, ps, hv, osv)
hdr += name + b'\0' * (16 - len(name))
hdr += cmdline + b'\0' * (512 - len(cmdline))
hdr += new_id
hdr += extra
assert len(hdr) == 1728, f"header {len(hdr)}"

# 按 page 对齐组装
out = hdr + b'\0' * (ps - len(hdr) % ps)
out += kernel + b'\0' * (align(len(kernel), ps) - len(kernel))
out += new_ramdisk + b'\0' * (align(len(new_ramdisk), ps) - len(new_ramdisk))
open(OUT_IMG, 'wb').write(out)
print(f"写出: {OUT_IMG} ({len(out)} B, ramdisk={len(new_ramdisk)})")
