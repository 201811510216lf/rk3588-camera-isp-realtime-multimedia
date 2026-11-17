#!/usr/bin/env python3
"""Read-only, bounded CLI observations with explicit provenance and failure records."""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import time


FIELDS = ["observation_wallclock_ns", "observation_monotonic_ns", "camera_id", "cli_exit_code",
          "state", "source", "hardware_confirmed", "ownership", "owners_json", "backend_name",
          "sdk_version", "iq_file", "iq_loaded", "iq_sensor_match", "ae_mode", "awb_mode",
          "exposure_us", "analog_gain", "controls_hardware_confirmed", "brightness", "message"]


def status_row(payload, camera_id, exit_code, wallclock_ns, monotonic_ns):
    if not isinstance(payload, dict) or type(payload.get("camera_id")) is not int or payload["camera_id"] != camera_id:
        raise ValueError("CLI response must be a status/result JSON object for the selected camera")
    controls = payload.get("controls", {})
    if not isinstance(controls, dict):
        raise ValueError("CLI controls must be an object")
    live_source = payload.get("source") == "LIVE_DEVICE"
    measured = controls.get("hardware_confirmed") is True and live_source
    row = {field: "NOT_MEASURED" for field in FIELDS}
    row.update(observation_wallclock_ns=wallclock_ns, observation_monotonic_ns=monotonic_ns,
               camera_id=camera_id, cli_exit_code=exit_code, hardware_confirmed=payload.get("hardware_confirmed") is True and live_source,
               controls_hardware_confirmed=measured, brightness="NOT_MEASURED")
    for field in ("state", "source", "ownership", "backend_name", "sdk_version", "iq_file", "iq_loaded", "iq_sensor_match", "message"):
        row[field] = payload.get(field, "NOT_MEASURED")
    row["owners_json"] = json.dumps(payload.get("owners", []), ensure_ascii=False)
    if "status" in payload and "state" not in payload:
        row["state"] = payload["status"]
    for field in ("ae_mode", "awb_mode", "exposure_us", "analog_gain"):
        value = controls.get(field)
        if measured and value is not None:
            if isinstance(value, (int, float)) and (isinstance(value, bool) or not math.isfinite(value)):
                raise ValueError("CLI readback must contain finite control values")
            row[field] = value
    return row


def collect_sample(cli, camera_id, isp_config=None, timeout_sec=5):
    arguments = [str(cli), "--isp-status", "--json", "--camera-id", str(camera_id)]
    if isp_config is not None:
        arguments += ["--isp-config", str(isp_config)]
    wall, monotonic = time.time_ns(), time.monotonic_ns()
    try:
        completed = subprocess.run(arguments, capture_output=True, text=True, timeout=timeout_sec,
                                   encoding="utf-8", errors="replace", check=False)
        payload = json.loads(completed.stdout)
        row = status_row(payload, camera_id, completed.returncode, wall, monotonic)
        return row, completed.returncode == 0
    except (OSError, subprocess.TimeoutExpired, ValueError, json.JSONDecodeError) as exc:
        row = {field: "NOT_MEASURED" for field in FIELDS}
        row.update(observation_wallclock_ns=wall, observation_monotonic_ns=monotonic, camera_id=camera_id,
                   cli_exit_code="ERROR", state="OBSERVATION_FAILED", source="NOT_MEASURED",
                   hardware_confirmed=False, controls_hardware_confirmed=False, message=str(exc))
        return row, False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--camera-id", choices=(0, 1), type=int, default=0)
    parser.add_argument("--isp-config", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--interval-sec", type=float, default=1)
    parser.add_argument("--timeout-sec", type=float, default=5)
    args = parser.parse_args(argv)
    if not 1 <= args.samples <= 10_000_000:
        parser.error("samples must be between 1 and 10000000")
    if not math.isfinite(args.interval_sec) or not 0 <= args.interval_sec <= 3600:
        parser.error("interval-sec must be finite and between 0 and 3600")
    if not math.isfinite(args.timeout_sec) or not 0 < args.timeout_sec <= 3600:
        parser.error("timeout-sec must be finite and between 0 and 3600 (exclusive lower bound)")
    if not args.cli.is_file():
        parser.error("CLI file does not exist")
    if args.isp_config is not None and not args.isp_config.is_file():
        parser.error("ISP config file does not exist")
    failed = False
    try:
        # Exclusive creation avoids erasing laboratory observation logs.
        with args.output.open("x", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            for index in range(args.samples):
                row, ok = collect_sample(args.cli, args.camera_id, args.isp_config, args.timeout_sec)
                writer.writerow(row)
                stream.flush()
                failed = failed or not ok
                if index + 1 < args.samples:
                    time.sleep(args.interval_sec)
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if failed:
        print("One or more CLI observations failed; failure records retained in CSV", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
