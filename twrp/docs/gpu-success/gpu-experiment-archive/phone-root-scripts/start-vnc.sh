#!/bin/bash
# x11vnc 启动脚本（nohup 后台，保持 x11vnc 不被杀）
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export DISPLAY=:0
export HOME=/root

# 杀旧 x11vnc
pkill -9 x11vnc 2>/dev/null
sleep 1

# 启动 x11vnc（-nopw -noshm 无密码，-forever 不退出，监听所有接口）
nohup x11vnc -display :0 \
    -nopw -noshm \
    -forever \
    -listen 0.0.0.0 \
    -rfbport 5900 \
    -noxdamage \
    > /tmp/x11vnc.log 2>&1 &

echo "x11vnc 启动，PID=$!"
sleep 3

# 验证
if ps | grep -v grep | grep -q x11vnc; then
    echo "x11vnc 在跑"
    netstat -tln 2>/dev/null | grep 5900 && echo "5900 监听 OK"
else
    echo "x11vnc 没起来，看日志 /tmp/x11vnc.log"
fi
