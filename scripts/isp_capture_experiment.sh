#!/usr/bin/env bash
set -euo pipefail

# Board-side capture experiment. It uses v4l2-ctl for read-only inspection,
# then performs one explicit bounded stream capture. It never writes Sensor
# registers or IQ files, and the output is raw NV12 rather than MP4.
usage() {
    echo "Usage: $0 --device /dev/videoX --out-dir DIR [--frames N] [--width N --height N]" >&2
    exit 2
}

device=""
out_dir=""
frames=120
width=""
height=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --device) [[ $# -ge 2 ]] || usage; device="$2"; shift 2 ;;
        --out-dir) [[ $# -ge 2 ]] || usage; out_dir="$2"; shift 2 ;;
        --frames) [[ $# -ge 2 ]] || usage; frames="$2"; shift 2 ;;
        --width) [[ $# -ge 2 ]] || usage; width="$2"; shift 2 ;;
        --height) [[ $# -ge 2 ]] || usage; height="$2"; shift 2 ;;
        *) usage ;;
    esac
done
[[ -n "$device" && -n "$out_dir" ]] || usage
command -v v4l2-ctl >/dev/null 2>&1 || { echo "v4l2-ctl is required" >&2; exit 1; }
[[ -e "$device" ]] || { echo "device does not exist: $device" >&2; exit 1; }
[[ "$frames" =~ ^[1-9][0-9]*$ ]] || { echo "frames must be a positive integer" >&2; exit 2; }
mkdir -p "$out_dir"
stamp="$(date +%Y%m%d_%H%M%S)"
info="$out_dir/${stamp}_$(basename "$device")_info.txt"
raw="$out_dir/${stamp}_$(basename "$device").nv12"
csv="$out_dir/${stamp}_$(basename "$device")_capture.txt"

{
    echo "device=$device"
    echo "captured_at=$(date --iso-8601=seconds)"
    v4l2-ctl -d "$device" --all
    v4l2-ctl -d "$device" --list-formats-ext
} | tee "$info"

if [[ -n "$width" && -n "$height" ]]; then
    v4l2-ctl -d "$device" --set-fmt-video=width="$width",height="$height",pixelformat=NV12
fi

v4l2-ctl -d "$device" --stream-mmap --stream-count="$frames" --stream-to="$raw"
{
    echo "capture_end=$(date --iso-8601=seconds)"
    echo "raw_file=$raw"
    echo "frames_requested=$frames"
    echo "timestamp_source=V4L2 DQBUF metadata must be collected by the application; raw v4l2-ctl output is not exposure timing"
} | tee "$csv"
echo "Wrote $info"
echo "Wrote $raw"
echo "Wrote $csv"

