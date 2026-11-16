#!/usr/bin/env python3
"""Read-only IQ inventory for RKAIQ bring-up evidence.

The tool never edits or generates IQ data. It records paths, hashes, file size,
and conservative filename matching; schema/SDK compatibility still requires the
vendor loader on the target board.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inventory(root: Path, sensor: str) -> list[dict[str, object]]:
    if not root.is_dir():
        raise ValueError(f"IQ root is not a directory: {root}")
    result: list[dict[str, object]] = []
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        try:
            digest = sha256(path)
            size = path.stat().st_size
        except OSError as exc:
            result.append({"path": str(path), "error": str(exc)})
            continue
        name = path.name.lower()
        result.append({
            "path": str(path),
            "size": size,
            "sha256": digest,
            "sensor_filename_match": sensor.lower() in name,
            "sdk_version": "NOT_IDENTIFIED",
            "schema_compatibility": "BOARD_LOADER_REQUIRED",
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/etc/iqfiles"))
    parser.add_argument("--sensor", default="imx415")
    parser.add_argument("--camera-id", type=int, default=0)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        files = inventory(args.root, args.sensor)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    report = {
        "camera_id": args.camera_id,
        "sensor": args.sensor,
        "root": str(args.root),
        "iq_files": files,
        "loader_status": "NOT_TESTED",
        "sdk_version": "NOT_IDENTIFIED",
    }
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"camera_id={args.camera_id}")
        print(f"sensor={args.sensor}")
        print(f"root={args.root}")
        print(f"file_count={len(files)}")
        print("loader_status=NOT_TESTED")
        for item in files:
            print(f"file={item.get('path')} sha256={item.get('sha256', 'ERROR')} "
                  f"sensor_filename_match={item.get('sensor_filename_match', False)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
