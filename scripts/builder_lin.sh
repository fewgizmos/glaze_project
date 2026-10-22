#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

build_type="release"
run_after=0

usage() {
    echo "Usage: $0 [options]"
    echo
    echo "Builds the project for Linux (release by default)."
    echo
    echo "Options:"
    echo "  -d, --debug  build in debug mode instead of release"
    echo "  -r, --run    run the executable after a successful build"
    echo "  -h, --help   show this help and exit"
    echo
    echo "Short options can be combined, e.g. -dr"
}

bad_arg() {
    echo "Unknown argument: $1" >&2
    echo "Run '$0 --help' for usage." >&2
    exit 1
}

for arg in "$@"; do
    case "$arg" in
        --debug) build_type="debug" ;;
        --run) run_after=1 ;;
        --help) usage; exit 0 ;;
        --*) bad_arg "$arg" ;;
        -?*)
            for ((i = 1; i < ${#arg}; i++)); do
                case "${arg:i:1}" in
                    d) build_type="debug" ;;
                    r) run_after=1 ;;
                    h) usage; exit 0 ;;
                    *) bad_arg "-${arg:i:1}" ;;
                esac
            done
            ;;
        *) bad_arg "$arg" ;;
    esac
done

if [[ "$build_type" == "debug" ]]; then
    cmake_build_type="Debug"
else
    cmake_build_type="Release"
fi

cmake -B .cmake_build/$build_type -S . -DCMAKE_BUILD_TYPE=$cmake_build_type
cmake --build .cmake_build/$build_type

out_dir="build/$glaze_build_id/linux/$build_type"
mkdir -p "$out_dir"
cp .cmake_build/$build_type/bin/glaze_executable "$out_dir/$glaze_bin_name"

echo "executable: $out_dir/$glaze_bin_name"

if [[ "$run_after" -eq 1 ]]; then
    echo "running.."
    "./$out_dir/$glaze_bin_name"
    echo "..quit"
fi
