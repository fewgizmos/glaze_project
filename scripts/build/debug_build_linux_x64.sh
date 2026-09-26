#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

cmake -B .cmake_build/debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build .cmake_build/debug

mkdir -p build/$glaze_build_id/linux/debug
cp .cmake_build/debug/bin/glaze_exec build/$glaze_build_id/linux/debug/$glaze_bin_name

echo "executable: build/$glaze_build_id/linux/debug/$glaze_bin_name"
