#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-host}"
ctest --test-dir "$build" --output-on-failure -R 'camera_queue|output_pipeline|behavior|runtime_monitor|rkaiq_lifecycle|isp_readiness|isp_health_monitor|isp_discovery' --repeat until-fail:100
