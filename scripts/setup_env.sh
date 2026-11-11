#!/bin/bash
# ============================================================
# setup_env.sh - CPU 调频 + tty0 输出 + 时区配置
# 用法: sudo ./setup_env.sh
# ============================================================

set -u

# ---------- 颜色与日志 ----------
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
log_info() { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ---------- 必须 root ----------
if [ "$(id -u)" -ne 0 ]; then
    log_err "请使用 root 权限运行: sudo $0"
    exit 1
fi

CPUFREQ_DIR="/sys/devices/system/cpu/cpu0/cpufreq"

# ============================================================
# 1. 查看可用的 CPU 调频策略
# ============================================================
if [ -r "${CPUFREQ_DIR}/scaling_available_governors" ]; then
    log_info "可用调频策略: $(cat ${CPUFREQ_DIR}/scaling_available_governors)"
else
    log_warn "${CPUFREQ_DIR}/scaling_available_governors 不可读（可能不支持 cpufreq）"
fi

# ============================================================
# 2. 查看当前频率
# ============================================================
if [ -r "${CPUFREQ_DIR}/scaling_cur_freq" ]; then
    log_info "当前 CPU0 频率: $(cat ${CPUFREQ_DIR}/scaling_cur_freq) kHz"
fi

# ============================================================
# 3. 设置所有 CPU 为 performance
# ============================================================
SET_OK=0
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -w "$gov" ] || continue
    echo performance > "$gov" 2>/dev/null && SET_OK=$((SET_OK+1))
done

if [ "$SET_OK" -gt 0 ]; then
    log_info "已将 ${SET_OK} 个 CPU 核的调频策略设置为 performance"
else
    log_err "没有可写的 scaling_governor（内核可能未开启 cpufreq 或为虚拟机）"
fi

# ============================================================
# 4. 向 /dev/tty0 写入控制序列
#    原命令: echo -e "\033[9;15]" > /dev/tty0
# ============================================================
if [ -c /dev/tty0 ] && [ -w /dev/tty0 ]; then
    # 用 printf 更可靠，避免不同 shell 对 echo -e 支持差异
    printf '\033[9;15]' > /dev/tty0 2>/dev/null \
        && log_info "已向 /dev/tty0 输出控制序列" \
        || log_warn "写入 /dev/tty0 失败"
else
    log_warn "/dev/tty0 不存在或不可写（SSH 远程环境属正常）"
fi

# ============================================================
# 5. 设置时区为 CST-8 (UTC+8)
# ============================================================
export TZ=CST-8
echo "CST-8" > /etc/TZ
log_info "已设置 TZ=CST-8 并写入 /etc/TZ"

# 若系统使用 glibc 时区数据库，可再软链一份保证 date 生效
if [ -e /usr/share/zoneinfo/Asia/Shanghai ]; then
    ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime
    log_info "已将 /etc/localtime 指向 Asia/Shanghai"
fi

log_info "全部完成 ✅  当前时间: $(date '+%Y-%m-%d %H:%M:%S %Z')"