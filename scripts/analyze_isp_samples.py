#!/usr/bin/env python3
"""Stride-aware offline NV12 diagnostics; never hardware IQ calibration."""
from __future__ import annotations
import argparse
import csv
from collections import deque
import json
import math
from pathlib import Path
import sys


class Moments:
    def __init__(self):
        self.count, self.mean, self.m2 = 0, 0.0, 0.0

    def add(self, value):
        self.count += 1
        delta = value - self.mean
        self.mean += delta / self.count
        self.m2 += delta * (value - self.mean)

    @property
    def stddev(self):
        return math.sqrt(max(0.0, self.m2 / self.count)) if self.count else None


def percentile(values, q):
    if not values:
        return None
    values = sorted(values)
    at = (len(values) - 1) * q
    low, high = math.floor(at), math.ceil(at)
    return values[low] + (values[high] - values[low]) * (at - low)


def layout(width, height, *, stride_y=None, stride_uv=None, storage_height=None,
           uv_offset=None, frame_size=None, pixel_format="NV12", uv_frame_size=None):
    if width <= 0 or height <= 0 or width % 2 or height % 2 or width * height > 16_777_216:
        raise ValueError("dimensions must be positive even values, at most 16M active pixels")
    if pixel_format not in ("NV12", "NV12M"):
        raise ValueError("only NV12 or explicitly separated NV12M planes are supported")
    stride_y = width if stride_y is None else stride_y
    stride_uv = width if stride_uv is None else stride_uv
    storage_height = height if storage_height is None else storage_height
    if stride_y < width or stride_uv < width or storage_height < height or storage_height % 2:
        raise ValueError("stride/storage height cannot truncate active image rows")
    y_storage, uv_storage = stride_y * storage_height, stride_uv * (storage_height // 2)
    y_active_end = (height - 1) * stride_y + width
    uv_active_end = (height // 2 - 1) * stride_uv + width
    if pixel_format == "NV12":
        uv_offset = y_storage if uv_offset is None else uv_offset
        if uv_offset < y_storage:
            raise ValueError("UV offset overlaps Y storage")
        minimum = uv_offset + uv_active_end
        frame_size = uv_offset + uv_storage if frame_size is None else frame_size
        if frame_size < minimum or uv_frame_size is not None:
            raise ValueError("single-plane frame size is too small or UV size supplied")
    else:
        if uv_offset is not None:
            raise ValueError("NV12M UV is a separate file, not a single-plane offset")
        frame_size = y_storage if frame_size is None else frame_size
        uv_frame_size = uv_storage if uv_frame_size is None else uv_frame_size
        if frame_size < y_active_end or uv_frame_size < uv_active_end:
            raise ValueError("NV12M plane storage sizes are too small")
    if max(frame_size, uv_frame_size or 0) > 512 * 1024 * 1024:
        raise ValueError("one stored frame/plane exceeds the 512MiB diagnostic limit")
    return dict(width=width, height=height, stride_y=stride_y, stride_uv=stride_uv,
                storage_height=storage_height, uv_offset=uv_offset, frame_size=frame_size,
                uv_frame_size=uv_frame_size, pixel_format=pixel_format)


def analyze_file(path, width, height, *, uv_plane=None, max_frames=None,
                 black_threshold=0, white_threshold=255, summary_limit=128, **options):
    """Storage layout comes from metadata, not an inference from file length."""
    spec = layout(width, height, **options)
    if not (0 <= black_threshold < white_threshold <= 255) or summary_limit < 0:
        raise ValueError("invalid clipping thresholds or summary bound")
    if max_frames is not None and max_frames <= 0:
        raise ValueError("max_frames must be positive")
    if (spec["pixel_format"] == "NV12M") != (uv_plane is not None):
        raise ValueError("NV12M requires --uv-plane; NV12 must not supply one")
    size = Path(path).stat().st_size
    if not size or size % spec["frame_size"]:
        raise ValueError("missing/truncated frame or supplied storage size mismatch")
    stored = size // spec["frame_size"]
    if uv_plane is not None and Path(uv_plane).stat().st_size != stored * spec["uv_frame_size"]:
        raise ValueError("NV12M files must contain equal counts of complete planes")
    count = min(stored, max_frames) if max_frames is not None else stored
    histogram, summaries = [0] * 256, []
    means, changes, differences = Moments(), Moments(), Moments()
    previous_mean = None
    with Path(path).open("rb") as stream:
        for frame in range(count):
            total = squares = 0
            for row in range(height):
                stream.seek(frame * spec["frame_size"] + row * spec["stride_y"])
                pixels = stream.read(width)
                if len(pixels) != width:
                    raise ValueError("luma payload truncated during analysis")
                for x, pixel in enumerate(pixels):
                    histogram[pixel] += 1
                    total += pixel
                    squares += pixel * pixel
                    if x:
                        differences.add(pixel - pixels[x - 1])
            mean = total / (width * height)
            deviation = math.sqrt(max(0.0, squares / (width * height) - mean * mean))
            means.add(mean)
            if previous_mean is not None:
                changes.add(abs(mean - previous_mean))
            previous_mean = mean
            if len(summaries) < summary_limit:
                summaries.append(dict(frame_index=frame, mean=mean, stddev=deviation))
    pixels = width * height * count
    mean = sum(v * n for v, n in enumerate(histogram)) / pixels
    deviation = math.sqrt(sum((v - mean) ** 2 * n for v, n in enumerate(histogram)) / pixels)
    return {
        "source": "OFFLINE_FILE_ANALYSIS", "hardware_confirmed": False,
        "input": str(path), "uv_plane": str(uv_plane) if uv_plane is not None else None,
        "layout": spec, "stored_frame_count": stored, "analyzed_frame_count": count,
        "luma": dict(histogram=histogram, mean=mean, stddev=deviation,
                     black_threshold=black_threshold, white_threshold=white_threshold,
                     black_clipping_ratio=sum(histogram[:black_threshold + 1]) / pixels,
                     white_clipping_ratio=sum(histogram[white_threshold:]) / pixels),
        "noise_proxy": dict(value=differences.stddev / math.sqrt(2) if differences.count else None,
                            method="horizontal_neighbor_difference_stddev_div_sqrt2",
                            limitation="STRUCTURE_SENSITIVE_PROXY_NOT_SENSOR_NOISE_CALIBRATION"),
        "temporal": dict(frame_mean_stddev=means.stddev,
                         absolute_mean_change_mean=changes.mean if changes.count else None,
                         absolute_mean_change_stddev=changes.stddev, ae_convergence="NOT_INFERRED"),
        "frame_summaries": summaries, "frame_summaries_truncated": count > len(summaries),
        "exposure_us": "NOT_MEASURED", "gain": "NOT_MEASURED", "ae_mode": "NOT_MEASURED",
        "awb_mode": "NOT_MEASURED", "iq_loaded": "NOT_MEASURED",
    }


def unsigned(text, field):
    if not isinstance(text, str) or not text.isascii() or not text.isdigit():
        raise ValueError(f"{field} must be an unsigned integer")
    return int(text)


def timestamp_metrics(path, *, sample_limit=4096):
    """Per-camera domains and integer subtraction preserve nanosecond differences."""
    if sample_limit <= 0:
        raise ValueError("timestamp sample bound must be positive")
    cameras = {}
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            raw = row.get("source_timestamp_ns") or row.get("timestamp_ns") or row.get("capture_timestamp_ns")
            timestamp = unsigned(raw, "timestamp_ns")
            camera = row.get("camera_id") or "unspecified"
            if camera != "unspecified" and unsigned(camera, "camera_id") > 1:
                raise ValueError("camera_id must be 0 or 1")
            domain = (row.get("clock_domain") or "UNKNOWN").upper()
            value = cameras.setdefault(camera, dict(count=0, first=timestamp, last=None, sequence=None,
                domain=domain, domain_changes=0, backwards=0, duplicates=0, zeros=0,
                gaps=0, sequence_duplicates=0, sequence_backwards=0, sequence_observations=0,
                intervals=deque(maxlen=sample_limit)))
            value["count"] += 1
            value["zeros"] += timestamp == 0
            prior = value["last"]
            if value["domain"] != domain:
                value["domain_changes"] += 1
                value["intervals"].clear()
            elif prior is not None:
                if timestamp < prior:
                    value["backwards"] += 1
                    value["intervals"].clear()
                elif timestamp == prior:
                    value["duplicates"] += 1
                elif prior and timestamp:
                    value["intervals"].append(timestamp - prior)
            raw_sequence = row.get("sequence") or row.get("source_sequence")
            if raw_sequence is not None and raw_sequence != "":
                seq = unsigned(raw_sequence, "sequence")
                previous = value["sequence"]
                if previous is not None:
                    value["gaps"] += max(0, seq - previous - 1)
                    value["sequence_duplicates"] += seq == previous
                    value["sequence_backwards"] += seq < previous
                value["sequence_observations"] += 1
                value["sequence"] = seq
            else:
                # Missing metadata cannot be interpreted as a captured-frame gap.
                value["sequence"] = None
            value["last"], value["domain"] = timestamp, domain
    results = []
    for camera, v in cameras.items():
        valid = (v["domain"] in ("MONOTONIC", "REALTIME", "SENSOR") and v["count"] > 1 and
                 not any(v[key] for key in ("domain_changes", "backwards", "duplicates", "zeros")))
        elapsed = v["last"] - v["first"]
        intervals = [delta / 1_000_000 for delta in v["intervals"]]
        results.append(dict(camera_id=None if camera == "unspecified" else int(camera),
            sample_count=v["count"], clock_domain=v["domain"], clock_domain_changes=v["domain_changes"],
            backwards_timestamps=v["backwards"], duplicate_timestamps=v["duplicates"], zero_timestamps=v["zeros"],
            sequence_gaps=v["gaps"] if v["sequence_observations"] == v["count"] else None,
            sequence_gap_status="COMPLETE_METADATA" if v["sequence_observations"] == v["count"] else "INCOMPLETE_METADATA",
            sequence_duplicates=v["sequence_duplicates"], sequence_backwards=v["sequence_backwards"],
            derived_fps=(v["count"] - 1) * 1_000_000_000 / elapsed if valid and elapsed > 0 else None,
            fps_status="DERIVED_FROM_SUPPLIED_TIMESTAMPS" if valid else "UNVERIFIED_OR_INVALID_CLOCK",
            interval_window_count=len(intervals), interval_window_capacity=sample_limit,
            delta_ms_p50=percentile(intervals, .5), delta_ms_p95=percentile(intervals, .95)))
    return dict(source="OFFLINE_METADATA_ANALYSIS", hardware_confirmed=False, cameras=results)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nv12", required=True, type=Path)
    parser.add_argument("--uv-plane", type=Path)
    parser.add_argument("--width", required=True, type=int)
    parser.add_argument("--height", required=True, type=int)
    parser.add_argument("--pixel-format", choices=("NV12", "NV12M"), default="NV12")
    for option in ("stride-y", "stride-uv", "storage-height", "uv-offset", "frame-size", "uv-frame-size", "max-frames"):
        parser.add_argument("--" + option, type=int)
    parser.add_argument("--black-threshold", type=int, default=0)
    parser.add_argument("--white-threshold", type=int, default=255)
    parser.add_argument("--timestamps", type=Path)
    parser.add_argument("--json", action="store_true", help="structured JSON (also the default output)")
    args = vars(parser.parse_args(argv))
    path, timestamps = args.pop("nv12"), args.pop("timestamps")
    args.pop("json")
    try:
        result = analyze_file(path, **args)
        result["timestamps"] = timestamp_metrics(timestamps) if timestamps else "NOT_MEASURED"
    except (OSError, ValueError, csv.Error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, allow_nan=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
