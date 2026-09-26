#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

./scripts/build/release_build_linux_x64.sh

echo "running.."
./build/$glaze_build_id/linux/release/$glaze_bin_name
echo "..quit"
