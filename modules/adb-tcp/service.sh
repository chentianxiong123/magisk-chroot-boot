#!/system/bin/sh
MODDIR=${0%/*}
log() { echo "[adb-tcp] $1" | tee -a /data/local/tmp/adb-tcp.log; }

while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done
sleep 2

PORT=5555
log "enabling adb over TCP on port $PORT"
setprop service.adb.tcp.port "$PORT"
stop adbd
start adbd
sleep 2
log "adbd restarted, service.adb.tcp.port=$(getprop service.adb.tcp.port)"
