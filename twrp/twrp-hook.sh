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
