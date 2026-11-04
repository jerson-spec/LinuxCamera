#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

exec env \
    BUILD_DIR=build-arm-dbg \
    BUILD_TYPE=Debug \
    "$SCRIPT_DIR/build.sh"