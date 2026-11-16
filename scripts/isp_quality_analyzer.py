#!/usr/bin/env python3
"""Measure RAW Bayer or ISP NV12 samples without pretending to be rkAIQ.

The tool is deliberately dependency-free.  It understands the two packed MIPI
formats used by the IMX415 modes (RAW10 and RAW12), reports per-Bayer-plane
statistics, and marks hardware-only values such as exposure and gain as
``NOT_MEASURED`` unless they are supplied by an evidence CSV.  It is intended
for board bring-up evidence, not as an ISP implementation.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics
import sys
from typing import Iterable, Sequence


def _check_dimensions(width: int, height: int, even: bool = False) -> None:
    if width <= 0 or height <= 0 or (even and (width % 2 or height % 2)):
        suffix = " even" if even else ""
        raise ValueError(f"width and height must be positive{suffix} values")


def unpack_raw10(data: bytes, width: int, height: int, stride: int | None = None) -> list[int]:
    """Unpack little-endian CSI-2 RAW10: four 10-bit pixels in five bytes."""
    _check_dimensions(width, height)
    # Four pixels always occupy one complete five-byte group.  The final
    # group is padded when a test pattern uses a width that is not divisible
    # by four (real IMX415 modes use widths divisible by four).
    row_bytes = ((width + 3) // 4) * 5
    stride = stride or row_bytes
    if stride < row_bytes:
        raise ValueError("RAW10 stride is shorter than the packed row")
    if len(data) < stride * height:
        raise ValueError(f"RAW10 file is shorter than {stride * height} bytes")
    pixels: list[int] = []
    for y in range(height):
        row = data[y * stride : y * stride + row_bytes]
        for x in range(0, width, 4):
            group = row[(x // 4) * 5 : (x // 4 + 1) * 5]
            if len(group) != 5:
                raise ValueError("truncated RAW10 group")
            high = group[4]
            for i in range(min(4, width - x)):
                pixels.append(group[i] | (((high >> (2 * i)) & 0x03) << 8))
    return pixels


def unpack_raw12(data: bytes, width: int, height: int, stride: int | None = None) -> list[int]:
    """Unpack little-endian CSI-2 RAW12: two 12-bit pixels in three bytes."""
    _check_dimensions(width, height)
    row_bytes = (width * 3 + 1) // 2
    stride = stride or row_bytes
    if stride < row_bytes:
        raise ValueError("RAW12 stride is shorter than the packed row")
    if len(data) < stride * height:
        raise ValueError(f"RAW12 file is shorter than {stride * height} bytes")
    pixels: list[int] = []
    for y in range(height):
        row = data[y * stride : y * stride + row_bytes]
        for x in range(0, width, 2):
            group = row[(x // 2) * 3 : (x // 2 + 1) * 3]
            if len(group) != 3:
                raise ValueError("truncated RAW12 group")
            pixels.append(group[0] | ((group[2] & 0x0F) << 8))
            if x + 1 < width:
                pixels.append(group[1] | ((group[2] >> 4) << 8))
    return pixels


def read_nv12_y(path_or_data: pathlib.Path | bytes, width: int, height: int,
                stride: int | None = None) -> list[int]:
    _check_dimensions(width, height, even=True)
    stride = stride or width
    if stride < width:
        raise ValueError("NV12 stride is shorter than width")
    expected = stride * height + stride * height // 2
    data = path_or_data.read_bytes() if isinstance(path_or_data, pathlib.Path) else path_or_data
    if len(data) < expected:
        raise ValueError(f"NV12 file is shorter than {expected} bytes")
    return [data[y * stride + x] for y in range(height) for x in range(width)]


def decode_raw(data: bytes, width: int, height: int, bit_depth: int,
               stride: int | None = None) -> list[int]:
    """Compatibility entry point used by host tests and small lab scripts."""
    if bit_depth == 10:
        return unpack_raw10(data, width, height, stride)
    if bit_depth == 12:
        return unpack_raw12(data, width, height, stride)
    raise ValueError("bit depth must be 10 or 12")


def bayer_channel_positions(width: int, height: int, pattern: str) -> dict[str, list[int]]:
    _check_dimensions(width, height)
    positions = {"r": [], "g": [], "b": []}
    cell = [[pattern.upper()[0], pattern.upper()[1]],
            [pattern.upper()[2], pattern.upper()[3]]]
    for y in range(height):
        for x in range(width):
            channel = cell[y & 1][x & 1].lower()
            positions[channel].append(y * width + x)
    return positions


def channel_stats(pixels: Sequence[int], positions: dict[str, list[int]],
                  black_level: int = 0) -> dict[str, float | None]:
    def mean(channel: str) -> float | None:
        values = [max(0, pixels[index] - black_level) for index in positions.get(channel, [])]
        return statistics.fmean(values) if values else None
    r_mean, g_mean, b_mean = mean("r"), mean("g"), mean("b")
    return {
        "r_mean": r_mean, "g_mean": g_mean, "b_mean": b_mean,
        "r_over_g": (r_mean / g_mean) if r_mean is not None and g_mean else None,
        "b_over_g": (b_mean / g_mean) if b_mean is not None and g_mean else None,
    }


def image_metrics(pixels: Sequence[int], width: int, height: int,
                  maximum: int, black_level: int = 0) -> dict[str, object]:
    if not pixels:
        raise ValueError("image has no pixels")
    return {
        "overexposed_fraction": sum(value >= maximum for value in pixels) / len(pixels),
        "underexposed_fraction": sum(value <= black_level for value in pixels) / len(pixels),
        "noise_estimate_lsb": _noise_estimate(pixels, width, height),
        "edge_sharpness_l1": _edge_sharpness(pixels, width, height),
    }


def percentile(values: Sequence[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = (len(ordered) - 1) * max(0.0, min(1.0, q))
    low, high = math.floor(rank), math.ceil(rank)
    if low == high:
        return float(ordered[low])
    return float(ordered[low] + (ordered[high] - ordered[low]) * (rank - low))


def _safe_stats(values: Sequence[float]) -> dict[str, float | int | None]:
    if not values:
        return {"count": 0, "mean": None, "stddev": None, "min": None, "max": None}
    return {
        "count": len(values),
        "mean": statistics.fmean(values),
        "stddev": statistics.pstdev(values),
        "min": min(values),
        "max": max(values),
    }


def _bayer_planes(pixels: Sequence[int], width: int, height: int, pattern: str) -> dict[str, list[int]]:
    pattern = pattern.upper()
    if pattern not in {"RGGB", "BGGR", "GRBG", "GBRG"}:
        raise ValueError("Bayer pattern must be RGGB, BGGR, GRBG, or GBRG")
    planes = {"R": [], "G1": [], "G2": [], "B": []}
    # Pattern is written in row-major order at the top-left 2x2 cell.
    cell = [[pattern[0], pattern[1]], [pattern[2], pattern[3]]]
    for y in range(height):
        for x in range(width):
            colour = cell[y & 1][x & 1]
            if colour == "G":
                # Preserve the two green sites for diagnosing row/column bias.
                key = "G1" if (y & 1) == 0 else "G2"
            else:
                key = colour
            planes[key].append(pixels[y * width + x])
    return planes


def _histogram(values: Sequence[float], maximum: float, bins: int = 16) -> list[int]:
    result = [0] * bins
    if maximum <= 0:
        return result
    for value in values:
        index = min(bins - 1, max(0, int(value * bins / (maximum + 1))))
        result[index] += 1
    return result


def _noise_estimate(values: Sequence[int], width: int, height: int) -> float | None:
    # Adjacent same-colour pixels are two columns apart in a Bayer row.  The
    # standard deviation of their difference divided by sqrt(2) is a useful
    # first-order read/noise estimate, not a calibrated sensor measurement.
    differences: list[float] = []
    for y in range(height):
        row = y * width
        for x in range(width - 2):
            differences.append(float(values[row + x + 2] - values[row + x]))
    if not differences:
        return None
    return statistics.pstdev(differences) / math.sqrt(2.0)


def _edge_sharpness(values: Sequence[int], width: int, height: int) -> float | None:
    gradients: list[float] = []
    for y in range(height - 1):
        row = y * width
        next_row = row + width
        for x in range(width - 1):
            dx = abs(values[row + x + 1] - values[row + x])
            dy = abs(values[next_row + x] - values[row + x])
            gradients.append(float(dx + dy))
    return statistics.fmean(gradients) if gradients else None


def analyze_raw(pixels: Sequence[int], width: int, height: int, pattern: str,
                bit_depth: int, black_level: int) -> dict[str, object]:
    maximum = (1 << bit_depth) - 1
    corrected = [max(0, p - black_level) for p in pixels]
    planes = _bayer_planes(pixels, width, height, pattern)
    plane_stats = {name: _safe_stats(values) for name, values in planes.items()}
    means = {name: float(stats["mean"]) for name, stats in plane_stats.items() if stats["mean"] is not None}
    green = (means.get("G1", 0.0) + means.get("G2", 0.0)) / 2.0
    return {
        "source": "RAW",
        "width": width,
        "height": height,
        "packing": f"RAW{bit_depth}",
        "bayer_pattern": pattern.upper(),
        "black_level": black_level,
        "black_corrected_mean": statistics.fmean(corrected),
        "black_corrected_stddev": statistics.pstdev(corrected),
        "bayer_planes": plane_stats,
        "r_over_g": (means.get("R") / green) if means.get("R") is not None and green else None,
        "b_over_g": (means.get("B") / green) if means.get("B") is not None and green else None,
        "histogram": _histogram(corrected, maximum, 16),
        "overexposed_fraction": sum(p >= maximum for p in pixels) / len(pixels),
        "underexposed_fraction": sum(p <= black_level for p in pixels) / len(pixels),
        "noise_estimate_lsb": _noise_estimate(pixels, width, height),
        "edge_sharpness_l1": _edge_sharpness(pixels, width, height),
        "ae_exposure_us": "NOT_MEASURED",
        "analog_gain": "NOT_MEASURED",
        "awb_status": "NOT_MEASURED",
    }


def analyze_nv12(y: Sequence[int], width: int, height: int) -> dict[str, object]:
    return {
        "source": "NV12_Y",
        "width": width,
        "height": height,
        "y_stats": _safe_stats(y),
        "histogram": _histogram(y, 255, 16),
        "overexposed_fraction": sum(v >= 250 for v in y) / len(y),
        "underexposed_fraction": sum(v <= 5 for v in y) / len(y),
        "noise_estimate_lsb": _noise_estimate(y, width, height),
        "edge_sharpness_l1": _edge_sharpness(y, width, height),
        "raw_bayer_metrics": "NOT_AVAILABLE_FROM_NV12",
        "ae_exposure_us": "NOT_MEASURED",
        "analog_gain": "NOT_MEASURED",
        "awb_status": "NOT_MEASURED",
    }


def frame_luma_stability(frames: Iterable[Sequence[int]]) -> dict[str, object]:
    means = [statistics.fmean(frame) for frame in frames if frame]
    deltas = [b - a for a, b in zip(means, means[1:])]
    return {
        "frame_count": len(means),
        "mean_luma": statistics.fmean(means) if means else None,
        "frame_to_frame_luma_stddev": statistics.pstdev(means) if means else None,
        "frame_to_frame_delta_mean": statistics.fmean(deltas) if deltas else None,
        # A luma-only trend cannot prove sensor register convergence.
        "ae_convergence": "NOT_INFERRED",
        "ae_image_trend": "IMAGE_ONLY_HEURISTIC" if len(means) > 1 else "NOT_INFERRED",
    }


def bayer_awb_stability(frames: Iterable[Sequence[int]], width: int, height: int,
                        pattern: str, black_level: int) -> dict[str, object]:
    rg: list[float] = []
    bg: list[float] = []
    positions = bayer_channel_positions(width, height, pattern)
    for frame in frames:
        stats = channel_stats(frame, positions, black_level)
        if stats["r_over_g"] is not None:
            rg.append(float(stats["r_over_g"]))
        if stats["b_over_g"] is not None:
            bg.append(float(stats["b_over_g"]))
    return {
        "sample_count": min(len(rg), len(bg)),
        "r_over_g_mean": statistics.fmean(rg) if rg else None,
        "r_over_g_stddev": statistics.pstdev(rg) if rg else None,
        "b_over_g_mean": statistics.fmean(bg) if bg else None,
        "b_over_g_stddev": statistics.pstdev(bg) if bg else None,
        "awb_convergence": "IMAGE_ONLY_HEURISTIC" if len(rg) > 1 and len(bg) > 1 else "NOT_INFERRED",
    }


def build_parser() -> argparse.ArgumentParser:
    """Build the parser, accepting both the documented and legacy lab syntax."""
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=False)
    source.add_argument("--raw", type=pathlib.Path)
    source.add_argument("--nv12", type=pathlib.Path)
    parser.add_argument("--input", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--format", choices=("raw10", "raw12", "nv12"))
    parser.add_argument("--packing", choices=("raw10", "raw12", "nv12"), help=argparse.SUPPRESS)
    parser.add_argument("--width", required=True, type=int)
    parser.add_argument("--height", required=True, type=int)
    parser.add_argument("--raw-format", choices=("10", "12"), default="10")
    parser.add_argument("--bayer", default="RGGB")
    parser.add_argument("--black-level", type=int, default=0)
    parser.add_argument("--stride-bytes", type=int)
    parser.add_argument("--frame", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--json", action="store_true", help="emit one JSON object")
    return parser


def analyze(args: argparse.Namespace) -> dict[str, object]:
    inputs = list(args.input or [])
    if not inputs:
        if args.raw:
            inputs = [args.raw]
        elif args.nv12:
            inputs = [args.nv12]
    if not inputs:
        raise ValueError("one --raw/--nv12 or at least one --input is required")
    fmt = args.format or args.packing or ("nv12" if args.nv12 else f"raw{args.raw_format}")
    if fmt == "nv12":
        frames = [read_nv12_y(path, args.width, args.height, args.stride_bytes) for path in inputs]
        report = analyze_nv12(frames[0], args.width, args.height)
    else:
        bit_depth = 10 if fmt == "raw10" else 12
        unpack = unpack_raw10 if bit_depth == 10 else unpack_raw12
        frames = [unpack(path.read_bytes(), args.width, args.height, args.stride_bytes) for path in inputs]
        report = analyze_raw(frames[0], args.width, args.height, args.bayer,
                             bit_depth, args.black_level)
    if len(frames) > 1:
        report["sequence"] = frame_luma_stability(frames)
        means = [statistics.fmean(frame) for frame in frames if frame]
        report["frame_brightness_stability_cv"] = (
            statistics.pstdev(means) / statistics.fmean(means)
            if means and statistics.fmean(means) else None)
        if fmt != "nv12":
            report["awb_stability"] = bayer_awb_stability(
                frames, args.width, args.height, args.bayer, args.black_level)
    report["exposure_us"] = "NOT_MEASURED"
    report["gain"] = "NOT_MEASURED"
    # Keep the public top-level field conservative: image-only luma trends do
    # not prove that the sensor AE registers converged.
    report["ae_convergence"] = "NOT_INFERRED"
    return report


def main(argv: Sequence[str] | None = None) -> int:
    # The ISP notes use ``bayer --input ... --packing raw10`` and
    # ``nv12 --input ...``.  Translate those aliases to the flag-based parser
    # so existing lab commands stay executable while one implementation owns
    # all decoding and metrics.
    raw_argv = list(sys.argv[1:] if argv is None else argv)
    if raw_argv and raw_argv[0] in {"bayer", "nv12"}:
        mode = raw_argv.pop(0)
        translated: list[str] = []
        index = 0
        found_format = False
        while index < len(raw_argv):
            token = raw_argv[index]
            if token == "--packing":
                translated.append("--format")
                if index + 1 >= len(raw_argv):
                    raise SystemExit("--packing requires raw10 or raw12")
                translated.append(raw_argv[index + 1].lower().replace("raw", ""))
                # --format expects raw10/raw12, so retain the prefix.
                translated[-1] = "raw" + translated[-1]
                found_format = True
                index += 1
            elif token == "--pattern":
                translated.append("--bayer")
            else:
                if token == "--format":
                    found_format = True
                translated.append(token)
            index += 1
        if not found_format:
            translated.extend(("--format", "nv12" if mode == "nv12" else "raw10"))
        raw_argv = translated
    parser = build_parser()
    args = parser.parse_args(raw_argv)
    try:
        _check_dimensions(args.width, args.height)
        if args.black_level < 0:
            raise ValueError("black level must be non-negative")
        report = analyze(args)
        if args.frame:
            fmt = args.format or args.packing or ("nv12" if args.nv12 else f"raw{args.raw_format}")
            if fmt == "nv12":
                source = lambda path: read_nv12_y(path, args.width, args.height, args.stride_bytes)
            else:
                unpack = unpack_raw10 if fmt == "raw10" else unpack_raw12
                source = lambda path: unpack(path.read_bytes(), args.width, args.height, args.stride_bytes)
            report["frame_stability"] = frame_luma_stability(source(path) for path in args.frame)
    except (OSError, ValueError, statistics.StatisticsError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, sort_keys=True))
    else:
        for key, value in report.items():
            print(f"{key}={json.dumps(value, ensure_ascii=False) if isinstance(value, (dict, list)) else value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
