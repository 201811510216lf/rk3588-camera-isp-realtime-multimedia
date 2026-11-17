#!/usr/bin/env bash
set -euo pipefail

# This explicitly requested capture operation may configure the selected format
# when width AND height are supplied. The separate isp_probe.sh remains read-only.
usage() {
    echo "Usage: $0 --device PATH --out-dir DIR [--frames N] [--width N --height N] [--timeout-sec N]" >&2
    echo "Use only a previously validated ISP processed NV12 output node." >&2
    exit 2
}
device=""
out_dir=""
frames=120
width=""
height=""
timeout_sec=60
while [[ $# -gt 0 ]]; do
    case "$1" in
        --device) [[ $# -ge 2 ]] || usage; device="$2"; shift 2 ;;
        --out-dir) [[ $# -ge 2 ]] || usage; out_dir="$2"; shift 2 ;;
        --frames) [[ $# -ge 2 ]] || usage; frames="$2"; shift 2 ;;
        --width) [[ $# -ge 2 ]] || usage; width="$2"; shift 2 ;;
        --height) [[ $# -ge 2 ]] || usage; height="$2"; shift 2 ;;
        --timeout-sec) [[ $# -ge 2 ]] || usage; timeout_sec="$2"; shift 2 ;;
        *) usage ;;
    esac
done
[[ -n "$device" && -n "$out_dir" ]] || usage
[[ "$frames" =~ ^[1-9][0-9]{0,6}$ && "$timeout_sec" =~ ^[1-9][0-9]{0,5}$ ]] || usage
if [[ -n "$width" || -n "$height" ]]; then
    [[ "$width" =~ ^[1-9][0-9]{0,4}$ && "$height" =~ ^[1-9][0-9]{0,4}$ ]] || usage
    (( width % 2 == 0 && height % 2 == 0 )) || usage
fi
command -v v4l2-ctl >/dev/null 2>&1 || { echo "v4l2-ctl is required" >&2; exit 1; }
command -v timeout >/dev/null 2>&1 || { echo "timeout is required for bounded capture" >&2; exit 1; }
[[ -c "$device" ]] || { echo "not a V4L2 character device: $device" >&2; exit 1; }
mkdir -p -- "$out_dir"
capture_dir="$(mktemp -d "$out_dir/capture-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
info="$capture_dir/device-info.txt"
raw="$capture_dir/capture.nv12"
metadata="$capture_dir/capture-metadata.txt"
{
    echo "source=LIVE_DEVICE_INSPECTION"
    echo "device=$device"
    echo "observation_wallclock=$(date --iso-8601=ns)"
    echo "mapping_validation=REQUIRED_FROM_MEDIA_GRAPH_NOT_INFERRED_HERE"
    timeout --kill-after=2 10 v4l2-ctl -d "$device" --all
    timeout --kill-after=2 10 v4l2-ctl -d "$device" --list-formats-ext
} > "$info"
if [[ -n "$width" ]]; then
    timeout --kill-after=2 10 v4l2-ctl -d "$device" --set-fmt-video=width="$width",height="$height",pixelformat=NV12
fi
timeout --kill-after=2 10 v4l2-ctl -d "$device" --get-fmt-video >> "$info"
{
    echo "source=V4L2_CTL_CAPTURE_REQUEST"
    echo "device=$device"
    echo "raw_file=$raw"
    echo "frames_requested=$frames"
    echo "capture_timeout_sec=$timeout_sec"
    echo "layout=USE_NEGOTIATED_STRIDE_STORAGE_HEIGHT_AND_PLANE_SIZE_FROM_DEVICE_INFO"
    echo "timestamp_source=NOT_MEASURED; collect source timestamp/sequence/clock-domain from application DQBUF metadata"
    echo "iq_loaded=NOT_MEASURED"
    echo "capture_complete=false"
} > "$metadata"
if timeout --kill-after=5 "$timeout_sec" v4l2-ctl -d "$device" --stream-mmap --stream-count="$frames" --stream-to="$raw"; then
    echo "capture_complete=true" >> "$metadata"
else
    code=$?
    echo "capture_exit_code=$code" >> "$metadata"
    echo "Capture failed or timed out; partial evidence retained at $capture_dir" >&2
    exit "$code"
fi
echo "capture_end_wallclock=$(date --iso-8601=ns)" >> "$metadata"
echo "Capture and negotiated-layout evidence: $capture_dir"
echo "Raw file analysis remains OFFLINE_FILE_ANALYSIS; no AE/AWB/IQ conclusion is inferred."
