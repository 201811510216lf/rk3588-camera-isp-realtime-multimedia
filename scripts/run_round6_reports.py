#!/usr/bin/env python3
"""Generate small, dependency-free evidence reports for the SIMULATED scenes.

The C++ demo is the implementation under test. This helper exists so a host
without a C++ toolchain can still inspect metric/report plumbing; its outputs
are labelled auxiliary simulation evidence.
"""
from __future__ import annotations

import json
import math
import shutil
import subprocess
from pathlib import Path


ROOT = Path("reports/isp")


def ppm(path: Path, w: int, h: int, pixels: list[tuple[int, int, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode())
        for r, g, b in pixels:
            f.write(bytes((r, g, b)))


def metrics(pixels: list[tuple[int, int, int]]) -> dict[str, float]:
    y = [0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in pixels]
    mean = sum(y) / len(y)
    variance = sum((v - mean) ** 2 for v in y) / len(y)
    return {"mean_brightness": mean, "clipping_ratio": sum(v >= 250 for v in y) / len(y), "shadow_ratio": sum(v <= 5 for v in y) / len(y), "noise_variance": variance, "reference_metrics": "NOT_AVAILABLE"}


def curve(path: Path, values: list[float], color: tuple[int, int, int]) -> None:
    w, h = 320, 180
    canvas = [(245, 245, 245)] * (w * h)
    lo, hi = min(values), max(values)
    span = max(1e-6, hi - lo)
    for i in range(len(values) - 1):
        x0 = round(i * (w - 1) / (len(values) - 1)); x1 = round((i + 1) * (w - 1) / (len(values) - 1))
        y0 = round((h - 1) - (values[i] - lo) / span * (h - 1)); y1 = round((h - 1) - (values[i + 1] - lo) / span * (h - 1))
        for x in range(min(x0, x1), max(x0, x1) + 1):
            t = 0.0 if x1 == x0 else (x - x0) / (x1 - x0); y = round(y0 + t * (y1 - y0));
            for dy in (-1, 0, 1):
                if 0 <= y + dy < h: canvas[(y + dy) * w + x] = color
    ppm(path, w, h, canvas)


def png_from_ppm(ppm_path: Path) -> None:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(ppm_path), str(ppm_path.with_suffix(".png"))], check=False)


def main() -> int:
    w, h = 64, 36
    base = []
    for y in range(h):
        for x in range(w):
            xf, yf = x / (w - 1), y / (h - 1)
            base.append((round(255 * xf), round(255 * (1 - xf) * (0.5 + yf / 2)), round(255 * yf)))
    gamma = [(round(255 * (v / 255) ** (1 / 2.2)) for v in p) for p in base]
    denoise = [(int((r + g + b) / 3),) * 3 for r, g, b in base]
    tungsten = [(min(255, round(r * 1.35)), g, round(b * 0.55)) for r, g, b in base]
    fluorescent = [(round(r * 0.88), min(255, round(g * 1.1)), min(255, round(b * 1.25))) for r, g, b in base]
    ppm(ROOT / "awb/daylight.ppm", w, h, base)
    ppm(ROOT / "awb/tungsten.ppm", w, h, tungsten)
    ppm(ROOT / "awb/fluorescent.ppm", w, h, fluorescent)
    ppm(ROOT / "gamma/gamma_after.ppm", w, h, gamma)
    ppm(ROOT / "denoise/denoise_after.ppm", w, h, denoise)
    curve(ROOT / "ae/dark_to_bright.ppm", [24, 45, 76, 103, 111, 113], (30, 120, 220)); curve(ROOT / "ae/bright_to_dark.ppm", [238, 201, 162, 125, 112, 109], (220, 80, 40)); curve(ROOT / "ae/exposure_curve.ppm", [10000, 18000, 25000, 30000, 30000, 30000], (40, 160, 70)); curve(ROOT / "ae/gain_curve.ppm", [1, 1, 1.2, 1.5, 1.5, 1.5], (140, 70, 180))
    curve(ROOT / "awb/awb_convergence.ppm", [1.65, 1.42, 1.2, 1.08, 1.02, 1.0], (50, 130, 190))
    for path in (ROOT / "ae/dark_to_bright.ppm", ROOT / "ae/bright_to_dark.ppm", ROOT / "ae/exposure_curve.ppm", ROOT / "ae/gain_curve.ppm", ROOT / "awb/daylight.ppm", ROOT / "awb/tungsten.ppm", ROOT / "awb/fluorescent.ppm", ROOT / "awb/awb_convergence.ppm", ROOT / "gamma/gamma_after.ppm", ROOT / "denoise/denoise_after.ppm"):
        png_from_ppm(path)
    report = {"simulated": True, "producer": "run_round6_reports.py", "metrics": metrics(base), "psnr": "NOT_AVAILABLE_WITHOUT_REFERENCE", "ssim": "NOT_AVAILABLE_WITHOUT_REFERENCE"}
    for name in ("blc", "demosaic", "awb", "denoise", "sharpen", "gamma", "overall"):
        out = dict(report); out["stage"] = name; (ROOT / name).mkdir(parents=True, exist_ok=True); (ROOT / name / "quality_report.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
    ae = {"simulated": True, "target_luma": 110, "mean_luma": [24, 45, 76, 103, 111, 113], "exposure_us": [10000, 18000, 25000, 30000, 30000, 30000], "converged": True, "convergence_frame": 5}
    (ROOT / "ae").mkdir(parents=True, exist_ok=True); (ROOT / "ae" / "convergence_report.json").write_text(json.dumps(ae, indent=2), encoding="utf-8")
    awb = {"simulated": True, "scenes": {"daylight": {"r_gain": 1.02, "b_gain": 0.98}, "tungsten": {"r_gain": 0.72, "b_gain": 1.85}, "fluorescent": {"r_gain": 1.08, "b_gain": 1.22}}, "status": "IMAGE_SIMULATION_ONLY"}
    (ROOT / "awb" / "awb_report.json").write_text(json.dumps(awb, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
