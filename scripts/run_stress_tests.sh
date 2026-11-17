#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-host}"
ctest --test-dir "$build" --output-on-failure -R 'camera_queue|output_pipeline|behavior|runtime_monitor' --repeat until-fail:100
