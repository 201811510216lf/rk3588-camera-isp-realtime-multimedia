#!/usr/bin/env python3
"""Encode a Software ISP NV12 dump with the host ffmpeg binary.

This is an explicit host utility. It does not claim MPP or a successful RTMP
server connection; RTMP errors are returned by ffmpeg and preserved in the
report.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--width", type=int, required=True)
    p.add_argument("--height", type=int, required=True)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--rtmp-url")
    p.add_argument("--report", type=Path)
    a = p.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    result = {"input": str(a.input), "output": str(a.output), "ffmpeg": ffmpeg or "NOT_FOUND", "backend": "FFmpeg_HOST", "rtmp": "NOT_REQUESTED"}
    if not ffmpeg:
        result["status"] = "FFMPEG_NOT_AVAILABLE"
        print(json.dumps(result, indent=2)); return 2
    if not a.input.is_file():
        result["status"] = "INPUT_NOT_FOUND"
        print(json.dumps(result, indent=2)); return 2
    cmd = [ffmpeg, "-y", "-f", "rawvideo", "-pix_fmt", "nv12", "-s", f"{a.width}x{a.height}", "-r", str(a.fps), "-i", str(a.input), "-c:v", "libx264", "-pix_fmt", "yuv420p", str(a.output)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    result["returncode"] = proc.returncode
    result["status"] = "PASS" if proc.returncode == 0 else "ENCODE_FAILED"
    result["stderr_tail"] = proc.stderr[-2000:]
    if a.rtmp_url:
        rtmp_cmd = [ffmpeg, "-re", "-f", "rawvideo", "-pix_fmt", "nv12", "-s", f"{a.width}x{a.height}", "-r", str(a.fps), "-i", str(a.input), "-c:v", "libx264", "-f", "flv", a.rtmp_url]
        rtmp = subprocess.run(rtmp_cmd, capture_output=True, text=True)
        result["rtmp"] = {"returncode": rtmp.returncode, "status": "PASS" if rtmp.returncode == 0 else "SERVER_UNAVAILABLE_OR_FAILED", "stderr_tail": rtmp.stderr[-2000:]}
    if a.report:
        a.report.parent.mkdir(parents=True, exist_ok=True); a.report.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2)); return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
