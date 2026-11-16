#!/usr/bin/env bash
set -euo pipefail

# Evidence collector for board-side AE/AWB experiments. It never claims a
# value that the identified rkAIQ backend did not return. Until a real BSP
# backend is installed, exposure/gain/brightness remain NOT_MEASURED.
usage() { echo "Usage: $0 --cli PATH --camera-id N --output CSV [--samples N --interval-sec N]" >&2; exit 2; }
cli=""; camera_id="0"; output=""; samples=60; interval=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cli) cli="$2"; shift 2;;
    --camera-id) camera_id="$2"; shift 2;;
    --output) output="$2"; shift 2;;
    --samples) samples="$2"; shift 2;;
    --interval-sec) interval="$2"; shift 2;;
    *) usage;;
  esac
done
[[ -n "$cli" && -n "$output" ]] || usage
[[ -x "$cli" || -f "$cli" ]] || { echo "CLI not found: $cli" >&2; exit 1; }
[[ "$samples" =~ ^[1-9][0-9]*$ ]] || { echo "samples must be positive" >&2; exit 2; }
printf 'capture_ts_ns,camera_id,daemon,runtime,iq_match,exposure_us,gain,wb_mode,brightness\n' > "$output"
for ((i=0; i<samples; i++)); do
  ts="$(date +%s%N)"
  status="$($cli --isp-status --camera-id "$camera_id" 2>/dev/null || true)"
  daemon="$(printf '%s\n' "$status" | awk -F= '/^daemon=/{print $2; exit}')"
  runtime="$(printf '%s\n' "$status" | awk -F= '/^runtime=/{print $2; exit}')"
  iq="$(printf '%s\n' "$status" | awk -F= '/^iq_match=/{print $2; exit}')"
  printf '%s,%s,%s,%s,%s,NOT_MEASURED,NOT_MEASURED,NOT_MEASURED,NOT_MEASURED\n' "$ts" "$camera_id" "${daemon:-none}" "${runtime:-none}" "${iq:-UNKNOWN}" >> "$output"
  sleep "$interval"
done
