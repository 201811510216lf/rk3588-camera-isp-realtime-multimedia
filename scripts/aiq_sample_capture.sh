#!/usr/bin/env bash
set -euo pipefail

# Read-only status sampling, not camera capture. Python handles JSON/CSV quoting,
# per-call deadlines, exclusive output creation and explicit failed samples.
root="$(cd "$(dirname "$0")/.." && pwd)"
exec "${PYTHON:-python3}" "$root/scripts/sample_aiq_status.py" "$@"
