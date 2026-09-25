#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

cmake -B cmake_build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build cmake_build

mkdir -p build/x64/linux/release
cp cmake_build/bin/glaze build/x64/linux/release/$glaze_bin_name
