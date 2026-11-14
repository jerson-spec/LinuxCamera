#!/bin/sh
#
# soak_test.sh —— 长跑测试（板上运行）
#
# 用法：
#   ./soak_test.sh [小时数]        默认 8
#
# 它做三件事：
#   - 每 60 秒记录一次 RSS / fd / 文件数 / 剩余空间 / 温度 / 最后一行日志
#   - 每 10 分钟触发一次拍照（kill -USR1）
#   - 每 30 分钟触发一次分辨率切换（kill -USR2）
#
# 程序要先在后台跑起来：
#   cd /root/LinuxCamera
#   nohup ./test_camera_preview >/dev/null 2>&1 &
#   nohup ./soak_test.sh 1 >/dev/null 2>&1 &
#
# 结果在 /root/LinuxCamera/soak/ 下，跑完看 SUMMARY 一行。

APP=test_camera_preview
BASE=/root/LinuxCamera
IMG_DIR=$BASE/images
CAM_LOG=$BASE/camera.log
OUT_DIR=$BASE/soak

SAMPLE_INTERVAL=60
SNAP_INTERVAL=600
SWITCH_INTERVAL=1800

HOURS=${1:-8}
DURATION=$((HOURS * 3600))

mkdir -p "$OUT_DIR" "$IMG_DIR"

PID=$(pidof $APP 2>/dev/null | awk '{print $1}')
if [ -z "$PID" ]; then
    echo "错误：找不到 $APP 进程，先用 nohup 把它跑起来"
    exit 1
fi

STAMP=$(date +%Y%m%d_%H%M%S)
LOG=$OUT_DIR/soak_${STAMP}_${HOURS}h.log
SEGMENT=$OUT_DIR/segment_${STAMP}.log

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG"
}

# 从 /proc/PID/status 取一个字段，例如 VmRSS / VmHWM / Threads
read_status() {
    awk -v k="$1" '$1 == k":" {print $2}' "/proc/$PID/status" 2>/dev/null
}

sample() {
    RSS=$(read_status VmRSS)
    HWM=$(read_status VmHWM)
    THR=$(read_status Threads)
    FDS=$(ls "/proc/$PID/fd" 2>/dev/null | wc -l)
    FREE=$(free | awk '/Mem:/ {print $4}')
    ROOTFREE=$(df -k / | awk 'NR==2 {print $4}')
    IMG_N=$(ls "$IMG_DIR" 2>/dev/null | wc -l)
    IMG_K=$(du -sk "$IMG_DIR" 2>/dev/null | awk '{print $1}')
    LOG_B=$(wc -c < "$CAM_LOG" 2>/dev/null)
    TEMP=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
    LAST=$(tail -n 1 "$CAM_LOG" 2>/dev/null)

    log "SAMPLE rss=${RSS}kB hwm=${HWM}kB thr=${THR} fd=${FDS} free=${FREE}kB rootfree=${ROOTFREE}kB img=${IMG_N}/${IMG_K}kB clog=${LOG_B}B temp=${TEMP} | ${LAST}"
}

# 记录起始状态
IMG_BEFORE=$(ls "$IMG_DIR" 2>/dev/null | wc -l)
START_BYTES=$(wc -c < "$CAM_LOG" 2>/dev/null)
[ -z "$START_BYTES" ] && START_BYTES=0

log "=== soak start: ${HOURS}h pid=$PID ==="
log "log file : $LOG"
log "before   : images=$IMG_BEFORE camera.log=${START_BYTES}B"
sample

START=$(date +%s)
LAST_SAMPLE=$START
LAST_SNAP=$START
LAST_SWITCH=$START
SNAP_N=0
SWITCH_N=0

while :; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - START))

    if [ "$ELAPSED" -ge "$DURATION" ]; then
        break
    fi

    if ! kill -0 "$PID" 2>/dev/null; then
        log "!!! $APP (pid $PID) 已经退出，elapsed=${ELAPSED}s"
        break
    fi

    if [ $((NOW - LAST_SNAP)) -ge "$SNAP_INTERVAL" ]; then
        kill -USR1 "$PID" 2>/dev/null
        SNAP_N=$((SNAP_N + 1))
        log "ACTION snapshot #$SNAP_N"
        LAST_SNAP=$NOW
    fi

    if [ $((NOW - LAST_SWITCH)) -ge "$SWITCH_INTERVAL" ]; then
        kill -USR2 "$PID" 2>/dev/null
        SWITCH_N=$((SWITCH_N + 1))
        log "ACTION res switch #$SWITCH_N"
        LAST_SWITCH=$NOW
    fi

    if [ $((NOW - LAST_SAMPLE)) -ge "$SAMPLE_INTERVAL" ]; then
        sample
        LAST_SAMPLE=$NOW
    fi

    sleep 5
done

END=$(date +%s)
IMG_AFTER=$(ls "$IMG_DIR" 2>/dev/null | wc -l)

log "=== soak end: elapsed=$((END - START))s snapshots=$SNAP_N switches=$SWITCH_N ==="
sample

# 只统计本次长跑期间新增的日志，避免上次运行的数据混进来
if [ -f "$CAM_LOG" ]; then
    tail -c +$((START_BYTES + 1)) "$CAM_LOG" > "$SEGMENT" 2>/dev/null
    ERR=$(grep -c '\[ERROR\]' "$SEGMENT" 2>/dev/null)
    WARN=$(grep -c '\[WARN\]' "$SEGMENT" 2>/dev/null)
    TOUT=$(grep -c 'poll timeout' "$SEGMENT" 2>/dev/null)
    FAIL=$(grep -c 'failed' "$SEGMENT" 2>/dev/null)

    log "SUMMARY images: $IMG_BEFORE -> $IMG_AFTER (+$((IMG_AFTER - IMG_BEFORE)), expected +$SNAP_N)"
    log "SUMMARY errors=$ERR warnings=$WARN poll_timeouts=$TOUT failed_lines=$FAIL"
    log "SUMMARY segment log: $SEGMENT"
fi

log "提示：按 Ctrl-C 结束程序后，确认最后一行是 DQbuf == Qbuf == Frames"
