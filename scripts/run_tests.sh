#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-host}"
cmake -S "$root" -B "$build" -DBUILD_RK_DEMO=OFF -DBUILD_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "$build" --parallel "${JOBS:-4}"
ctest --test-dir "$build" --output-on-failure
