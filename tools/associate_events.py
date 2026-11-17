#!/usr/bin/env python3
"""Associate final behavior events with accepted packets in finalized recordings.

The index is scanned once per event: O(events * packets), bounded by the number
of spans for one event, without loading a 24-hour packet log into RAM.
"""

import argparse
import json
from pathlib import Path


def json_lines(path):
    with Path(path).open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: invalid JSON") from exc
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{line_number}: expected JSON object")
            yield value


def nonnegative(value, field):
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError(f"{field} must be a nonnegative integer")
    return value


def associate_event(event, index_path, *, recording_dir=None, max_spans=10000):
    """Return a copy; never create spans for missing/unfinalized packet evidence."""
    camera = nonnegative(event["camera_id"], "camera_id")
    first = nonnegative(event["first_sequence"], "first_sequence")
    last = nonnegative(event["last_sequence"], "last_sequence")
    start = nonnegative(event["start_timestamp_ns"], "start_timestamp_ns")
    end = nonnegative(event["end_timestamp_ns"], "end_timestamp_ns")
    if first > last or start > end or max_spans <= 0:
        raise ValueError("invalid event interval or max_spans")
    spans = []
    finalized = {}
    last_sequence = None
    conflicts = 0
    endpoints = set()
    for record in json_lines(index_path):
        kind = record.get("type")
        segment = record.get("segment_id")
        if not isinstance(segment, str) or not segment:
            raise ValueError("index record requires segment_id")
        if kind == "finalize":
            if type(record.get("finalized")) is not bool:
                raise ValueError("finalize record requires boolean finalized")
            if segment in finalized:
                # Any explicit failure remains a failure even if the log repeats.
                finalized[segment] = record["finalized"] and finalized[segment] is not False
            continue
        if kind != "packet":
            raise ValueError("unknown index record type")
        offset = nonnegative(record["video_offset_ns"], "video_offset_ns")
        sources = record.get("sources")
        if not isinstance(sources, list):
            raise ValueError("packet sources must be an array")
        for source in sources:
            if not isinstance(source, dict):
                raise ValueError("packet source must be an object")
            source_camera = nonnegative(source["camera_id"], "source camera_id")
            seq = nonnegative(source["sequence"], "source sequence")
            timestamp = nonnegative(source["source_timestamp_ns"], "source_timestamp_ns")
            if source_camera != camera or not first <= seq <= last:
                continue
            if not start <= timestamp <= end or (seq == first and timestamp != start) or (seq == last and timestamp != end):
                conflicts += 1
                continue
            if last_sequence is not None and seq <= last_sequence:
                if seq < last_sequence or (spans and segment != spans[-1]["segment_id"]):
                    conflicts += 1
                continue  # repeated hold-last source frames are not new captures
            if seq in (first, last):
                endpoints.add(seq)
            if spans and spans[-1]["segment_id"] == segment and seq == spans[-1]["last_sequence"] + 1 and offset >= spans[-1]["end_offset_ns"]:
                spans[-1]["last_sequence"] = seq
                spans[-1]["end_offset_ns"] = offset
            else:
                if len(spans) >= max_spans:
                    raise ValueError("event exceeds max_spans; narrow the interval or explicitly increase the bound")
                spans.append({"segment_id": segment, "start_offset_ns": offset, "end_offset_ns": offset,
                              "first_sequence": seq, "last_sequence": seq})
            finalized.setdefault(segment, None)
            last_sequence = seq
    available = []
    unfinalized = set()
    missing_files = set()
    for span in spans:
        segment = span["segment_id"]
        if finalized[segment] is not True:
            unfinalized.add(segment)
            continue
        if recording_dir is not None:
            # Only recorder-owned file names are accepted, never path traversal.
            if Path(segment).name != segment or "/" in segment or "\\" in segment or not (Path(recording_dir) / segment).is_file():
                missing_files.add(segment)
                continue
        available.append(span)
    # Reused sequence ranges across sessions or conflicting clocks are ambiguous.
    # Do not attach a plausible but unproved segment to an experimental event.
    if conflicts or start == 0 or end == 0:
        available = []
    mapped = sum(span["last_sequence"] - span["first_sequence"] + 1 for span in available)
    expected = last - first + 1
    complete = mapped == expected and endpoints == {first, last} and conflicts == 0
    result = dict(event)
    result["video_spans"] = available
    result["video_association"] = {
        "status": "mapped" if complete else ("partial" if mapped else "unmapped"),
        "expected_frame_count": expected,
        "finalized_frame_count": mapped,
        "missing_frame_count": expected - mapped,
        "timestamp_or_order_conflicts": conflicts,
        "source_clock_available": start != 0 and end != 0,
        "unfinalized_segments": sorted(unfinalized),
        "missing_files": sorted(missing_files),
    }
    return result


def associate_files(events_path, index_path, output_path, *, recording_dir=None):
    output_path = Path(output_path)
    if output_path.resolve() in (Path(events_path).resolve(), Path(index_path).resolve()):
        raise ValueError("output must differ from both input files")
    counts = {"mapped": 0, "partial": 0, "unmapped": 0, "non_event_records": 0}
    # Exclusive creation preserves earlier results and original experimental logs.
    with output_path.open("x", encoding="utf-8", newline="\n") as output:
        for event in json_lines(events_path):
            if "event_id" not in event:
                counts["non_event_records"] += 1
                continue
            associated = associate_event(event, index_path, recording_dir=recording_dir)
            counts[associated["video_association"]["status"]] += 1
            output.write(json.dumps(associated, ensure_ascii=False, allow_nan=False) + "\n")
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", required=True, type=Path)
    parser.add_argument("--index", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--recording-dir", type=Path, help="also verify referenced finalized files exist")
    args = parser.parse_args()
    print(json.dumps(associate_files(args.events, args.index, args.output, recording_dir=args.recording_dir)))


if __name__ == "__main__":
    main()
