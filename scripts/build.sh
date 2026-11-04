#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-arm}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

# 校验工具链文件存在
TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchain-arm.cmake"
if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "error: toolchain file not found: $TOOLCHAIN_FILE" >&2
    exit 1
fi

# BUILD_DIR 是相对路径时拼到 ROOT_DIR 下；绝对路径则直接用
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

# 兼容没有 nproc 的系统（macOS 等）
JOBS="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" -j"$JOBS"