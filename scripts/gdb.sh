#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-arm-dbg}"
TARGET="${1:-test_Trans_YUYV}"

SDK_ROOT="${SDK_ROOT:-$HOME/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot}"

GDB="${GDB:-$(find "$SDK_ROOT" -type f -name '*-gdb' 2>/dev/null | head -n 1)}"

if [[ -z "${GDB:-}" ]]; then
    echo "cross gdb not found under: $SDK_ROOT" >&2
    exit 1
fi

echo "GDB: $GDB"
echo "On board run:"
echo "  cd /root/LinuxCamera && gdbserver :1234 ./$TARGET"
echo

adb forward tcp:1234 tcp:1234

exec "$GDB" "$ROOT_DIR/$BUILD_DIR/$TARGET" \
    -ex "set pagination off" \
    -ex "set confirm off" \
    -ex "target remote 127.0.0.1:1234"