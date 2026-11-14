#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-arm}"
BOARD_DIR="${BOARD_DIR:-/root/LinuxCamera}"

BINARIES=(
    test_Get_images
    test_Draw_region
    test_Trans_YUYV
    test_camera_preview
)

adb get-state >/dev/null
adb shell mkdir -p "$BOARD_DIR/config"

for bin in "${BINARIES[@]}"; do
    adb push "$ROOT_DIR/$BUILD_DIR/$bin" "$BOARD_DIR/"
done

adb push "$ROOT_DIR/config/." "$BOARD_DIR/config/"
adb push "$ROOT_DIR/scripts/setup_env.sh" "$BOARD_DIR/scripts/setup_env.sh"
adb push "$ROOT_DIR/scripts/soak_test.sh" "$BOARD_DIR/scripts/soak_test.sh"

adb shell "chmod +x \
    '$BOARD_DIR/test_Get_images' \
    '$BOARD_DIR/test_Draw_region' \
    '$BOARD_DIR/test_Trans_YUYV'"
