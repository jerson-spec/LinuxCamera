#!/bin/sh
PID=$(pidof test_camera_preview)
[ -z "$PID" ] && { echo "not running"; exit 1; }

HZ=$(getconf CLK_TCK)      # 一般是 100
T1=$(awk '{print $14+$15}' /proc/$PID/stat)

while :; do
    sleep 1
    T2=$(awk '{print $14+$15}' /proc/$PID/stat)
    RSS=$(awk '/VmRSS/{print $2}' /proc/$PID/status)
    HWM=$(awk '/VmHWM/{print $2}' /proc/$PID/status)
    FD=$(ls /proc/$PID/fd 2>/dev/null | wc -l)
    CPU=$(( (T2 - T1) * 100 / HZ ))
    echo "$(date +%T) cpu=${CPU}% rss=${RSS}kB hwm=${HWM}kB fd=${FD}"
    T1=$T2
done