#!/usr/bin/env bash
set -u

# Read-only RK3588 camera/ISP probe. It never writes DTS, IQ files, controls,
# formats, or stream state. Run it on the target board and save its stdout.

run_optional() {
    local tool="$1"
    shift
    if command -v "$tool" >/dev/null 2>&1; then
        echo "+ $tool $*"
        "$tool" "$@" || echo "[probe] $tool returned non-zero ($?)"
    else
        echo "[probe] optional tool not installed: $tool"
    fi
}

echo "=== kernel and board ==="
uname -a || true
if [[ -r /proc/device-tree/model ]]; then
    printf 'model: '
    tr '\0' '\n' </proc/device-tree/model
else
    echo "model: /proc/device-tree/model unavailable"
fi

echo "=== device nodes ==="
ls -l /dev/media* /dev/video* 2>/dev/null || echo "no /dev/media* or /dev/video* nodes"

echo "=== V4L2 devices ==="
run_optional v4l2-ctl --list-devices

if command -v v4l2-ctl >/dev/null 2>&1; then
    shopt -s nullglob
    video_nodes=(/dev/video*)
    for node in "${video_nodes[@]}"; do
        echo "=== $node --all ==="
        v4l2-ctl -d "$node" --all || true
        echo "=== $node --list-formats-ext ==="
        v4l2-ctl -d "$node" --list-formats-ext || true
    done
fi

echo "=== media graph ==="
if command -v media-ctl >/dev/null 2>&1; then
    shopt -s nullglob
    media_nodes=(/dev/media*)
    for node in "${media_nodes[@]}"; do
        echo "=== $node -p ==="
        media-ctl -p -d "$node" || true
    done
else
    echo "[probe] optional tool not installed: media-ctl"
fi

echo "=== camera kernel messages (read-only) ==="
if command -v dmesg >/dev/null 2>&1; then
    dmesg --color=never 2>/dev/null | grep -Ei 'rkisp|rkcif|csi|mipi|sensor|camera|isp|aiq' || \
        echo "no matching camera messages or dmesg permission denied"
else
    echo "[probe] optional tool not installed: dmesg"
fi

echo "=== rkAIQ/runtime hints ==="
if command -v pgrep >/dev/null 2>&1; then
    pgrep -a -f 'rkaiq|rkisp|camera' || echo "no matching rkAIQ process"
fi
for path in /usr/lib /usr/lib64 /oem/usr/lib /system/lib; do
    if [[ -d "$path" ]]; then
        find "$path" -maxdepth 2 -type f \( -iname '*rkaiq*' -o -iname '*rkisp*' \) -print 2>/dev/null
    fi
done

echo "=== end of read-only probe ==="
