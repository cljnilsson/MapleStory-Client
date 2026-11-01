#!/usr/bin/env bash
# Rebuild HeavenClient into build/ (incremental).
# build/ also holds the .nx assets and Settings, so this script never deletes it.
# Usage: ./build.sh [--deps] [--release|--debug] [--reconfigure]
#   --deps         run ./build-deps.sh first (only needed once, or after deps change)
#   --release      configure with CMAKE_BUILD_TYPE=Release
#   --debug        configure with CMAKE_BUILD_TYPE=Debug
#   --reconfigure  rerun cmake configure even if build/Makefile exists
set -euo pipefail
cd "$(dirname "$0")"

deps=0 reconfigure=0 build_type=""
for arg in "$@"; do
    case "$arg" in
        --deps) deps=1 ;;
        --release) build_type=Release; reconfigure=1 ;;
        --debug) build_type=Debug; reconfigure=1 ;;
        --reconfigure) reconfigure=1 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

[[ $deps == 1 ]] && ./build-deps.sh

if [[ $reconfigure == 1 || ! -f build/Makefile ]]; then
    cmake -S . -B build ${build_type:+-DCMAKE_BUILD_TYPE=$build_type}
fi
cmake --build build -j"$(nproc)"
echo "Built $(pwd)/build/HeavenClient"
