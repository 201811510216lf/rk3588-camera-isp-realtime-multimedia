#!/usr/bin/env python3
"""Analyze a packed NV12 sample without inventing exposure/timestamp data."""
from __future__ import annotations

import argparse
import csv
import math
import pathlib
import statistics
import sys


def read_y(path: pathlib.Path, width: int, height: int) -> list[int]:
    expected = width * height * 3 // 2
    data = path.read_bytes()
    if len(data) < expected:
        raise ValueError(f"NV12 file is shorter than expected: {len(data)} < {expected}")
    return list(data[: width * height])


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * q
    low = math.floor(index)
    high = math.ceil(index)
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def timestamp_deltas(path: pathlib.Path) -> list[float]:
    values: list[float] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            raw = row.get("timestamp_ns") or row.get("capture_timestamp_ns")
            if raw is None:
                continue
            values.append(float(raw))
    return [(b - a) / 1e6 for a, b in zip(values, values[1:]) if b >= a]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nv12", required=True, type=pathlib.Path)
    parser.add_argument("--width", required=True, type=int)
    parser.add_argument("--height", required=True, type=int)
    parser.add_argument("--timestamps", type=pathlib.Path)
    args = parser.parse_args()
    if args.width <= 0 or args.height <= 0 or args.width % 2 or args.height % 2:
        parser.error("width and height must be positive even values")
    try:
        y = read_y(args.nv12, args.width, args.height)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"nv12={args.nv12}")
    print(f"width={args.width} height={args.height}")
    print(f"y_mean={statistics.fmean(y):.4f}")
    print(f"y_stddev={statistics.pstdev(y):.4f}")
    print(f"y_min={min(y)} y_max={max(y)}")
    print("exposure_us=NOT_MEASURED")
    print("gain=NOT_MEASURED")
    if args.timestamps:
        try:
            deltas = timestamp_deltas(args.timestamps)
        except (OSError, ValueError, csv.Error) as exc:
            print(f"ERROR: timestamp CSV: {exc}", file=sys.stderr)
            return 1
        print(f"timestamp_samples={len(deltas) + 1 if deltas else 0}")
        print(f"delta_ms_p50={percentile(deltas, 0.50) if deltas else 'NOT_MEASURED'}")
        print(f"delta_ms_p95={percentile(deltas, 0.95) if deltas else 'NOT_MEASURED'}")
    else:
        print("timestamp_samples=NOT_MEASURED")
        print("delta_ms_p50=NOT_MEASURED")
        print("delta_ms_p95=NOT_MEASURED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

