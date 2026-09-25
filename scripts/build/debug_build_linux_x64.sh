#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

cmake -B cmake_build_debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake_build_debug

mkdir -p build/x64/linux/debug
cp cmake_build_debug/bin/glaze build/x64/linux/debug/$glaze_bin_name
