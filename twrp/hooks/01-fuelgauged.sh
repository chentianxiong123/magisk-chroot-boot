#!/system/bin/sh
# 01-fuelgauged.sh — hybrid recovery 电量守护 (自包含, 显式路径, 不依赖环境)
# 确保 /system 挂载 + /vendor 链接, 用绝对路径启动 fuelgauged

SYS=/system
VENDOR_LIB=/system/vendor/lib
SYS_LIB=/system/lib

# 1. 确保 /system 挂载 (hybrid TWRP 可能已挂)
if ! grep -q " /system " /proc/mounts 2>/dev/null; then
    mount -o rw /dev/block/mmcblk0p29 /system 2>/dev/null
fi

# 2. 确保 /vendor 软链接
[ -L /vendor ] || ln -sf $SYS/vendor /vendor 2>/dev/null

# 3. 启动 fuelgauged (绝对路径 + LD_LIBRARY_PATH 只给 fuelgauged 自己)
#    注意: 不要 export LD_LIBRARY_PATH (会搞崩64位工具如pidof)
#    坑: setsid & 启动 fuelgauged 会失败(进程0), 简单 & 才能存活(进程1)
if [ -x /system/vendor/bin/fuelgauged ]; then
    LD_LIBRARY_PATH=$VENDOR_LIB:$SYS_LIB /system/vendor/bin/fuelgauged >/dev/null 2>&1 < /dev/null &
fi

exit 0
