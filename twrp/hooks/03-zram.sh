#!/system/bin/sh
# 03-zram.sh — zram swap (内存一半, 自包含, 幂等)
# 坑1: recovery PATH里没awk
# 坑2: shell32位溢出(>2G算错)
# 坑3: MemTotal带单位"2855084 kB"(要tr -d kB)

# 已在用则跳过 (幂等)
if grep -q zram0 /proc/swaps 2>/dev/null; then
    exit 0
fi

# 建节点 (内建 zram, major 254)
[ -e /dev/zram0 ] || mknod /dev/zram0 b 254 0 2>/dev/null

# disksize = MemTotal(kb) * 512 (分步算64位安全, 去kB单位)
KB=$(grep MemTotal /proc/meminfo | cut -d: -f2 | tr -d ' ' | tr -d 'kB')
SZ=$(( KB * 512 ))
[ "$SZ" -le 0 ] && SZ=1461795328  # fallback 1.4G

echo "$SZ" > /sys/block/zram0/disksize 2>/dev/null

# 启用 swap (优先级 100)
mkswap /dev/zram0 >/dev/null 2>&1
swapon -p 100 /dev/zram0 >/dev/null 2>&1

exit 0
