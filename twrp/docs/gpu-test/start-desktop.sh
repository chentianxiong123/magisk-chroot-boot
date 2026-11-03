#!/bin/bash
# 最轻桌面启动脚本（Xorg fbdev + jwm + xterm）
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export DISPLAY=:0
export HOME=/root

# 等 fb0
while [ ! -e /dev/graphics/fb0 ]; do sleep 1; done

# Xorg 后台
Xorg :0 -config /etc/X11/xorg.conf > /tmp/xorg.log 2>&1 &
XPID=$!

# 等 X 起来
sleep 5
if ! kill -0 $XPID 2>/dev/null; then
    echo "Xorg 起不来，看日志 /tmp/xorg.log" >&2
    exit 1
fi

# jwm 后台
jwm > /tmp/jwm.log 2>&1 &
JWMPID=$!

# xterm 后台（测试窗口）
sleep 2
xterm -geometry 80x24+50+50 > /tmp/xterm.log 2>&1 &

echo "Xorg=$XPID jwm=$JWMPID xterm=$!"
exit 0
