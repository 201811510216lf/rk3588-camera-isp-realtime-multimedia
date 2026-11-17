"""Offline fixtures and mocked CLI observations; never board validation."""
import csv
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[1] / "scripts" / f"{name}.py")
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


ANALYZE = module("analyze_isp_samples")
SAMPLE = module("sample_aiq_status")


class DiagnosticTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_padded_storage_and_multiple_frames(self):
        path = self.root / "frames.nv12"
        first = bytes([0, 10, 99, 99, 20, 255, 99, 99]) + bytes([88] * 16)
        second = bytes([10, 20, 99, 99, 30, 245, 99, 99]) + bytes([88] * 16)
        path.write_bytes(first + second)
        result = ANALYZE.analyze_file(path, 2, 2, stride_y=4, stride_uv=4, storage_height=4,
                                      uv_offset=16, frame_size=24, summary_limit=1)
        self.assertEqual(result["analyzed_frame_count"], 2)
        self.assertAlmostEqual(result["luma"]["mean"], 73.75)
        self.assertEqual(result["luma"]["histogram"][99], 0)
        self.assertEqual(result["luma"]["black_clipping_ratio"], 1 / 8)
        self.assertEqual(result["luma"]["white_clipping_ratio"], 1 / 8)
        self.assertEqual(result["temporal"]["absolute_mean_change_mean"], 5)
        self.assertTrue(result["frame_summaries_truncated"])
        self.assertFalse(result["hardware_confirmed"])
        self.assertEqual(result["exposure_us"], "NOT_MEASURED")

    def test_nv12m_and_truncated_or_invalid_layout(self):
        y, uv = self.root / "y.bin", self.root / "uv.bin"
        y.write_bytes(bytes([40, 40, 99, 99, 40, 40, 99, 99]))
        uv.write_bytes(bytes([128] * 4))
        result = ANALYZE.analyze_file(y, 2, 2, pixel_format="NV12M", uv_plane=uv, stride_y=4, stride_uv=4)
        self.assertEqual(result["luma"]["mean"], 40)
        self.assertEqual(result["noise_proxy"]["value"], 0)
        uv.write_bytes(b"\0")
        with self.assertRaises(ValueError):
            ANALYZE.analyze_file(y, 2, 2, pixel_format="NV12M", uv_plane=uv)
        with self.assertRaises(ValueError):
            ANALYZE.layout(3, 2)
        with self.assertRaises(ValueError):
            ANALYZE.layout(2, 2, uv_offset=1)
        with self.assertRaises(ValueError):
            ANALYZE.analyze_file(y, 2, 2)

    def test_payload_may_omit_padding_after_last_active_row(self):
        path = self.root / "bytesused.nv12"
        path.write_bytes(bytes([10, 20, 99, 99, 30, 40, 99, 99, 128, 128]))
        result = ANALYZE.analyze_file(path, 2, 2, stride_y=4, stride_uv=4, frame_size=10)
        self.assertEqual(result["luma"]["mean"], 25)
        y, uv = self.root / "y.bin", self.root / "uv.bin"
        y.write_bytes(bytes([10, 20, 99, 99, 30, 40]))
        uv.write_bytes(bytes([128, 128]))
        result = ANALYZE.analyze_file(y, 2, 2, uv_plane=uv, pixel_format="NV12M", stride_y=4,
                                      stride_uv=4, frame_size=6, uv_frame_size=2)
        self.assertEqual(result["luma"]["mean"], 25)

    def test_integer_timestamps_camera_separation_and_sequence_gaps(self):
        path = self.root / "metadata.csv"
        epoch = 1_700_000_000_000_000_000
        path.write_text("camera_id,source_timestamp_ns,sequence,clock_domain\n" +
                        f"0,{epoch},10,MONOTONIC\n1,{epoch},1,MONOTONIC\n" +
                        f"0,{epoch+100},12,MONOTONIC\n1,{epoch+200},2,MONOTONIC\n", encoding="utf-8")
        reports = ANALYZE.timestamp_metrics(path)["cameras"]
        self.assertEqual(reports[0]["delta_ms_p50"], .0001)
        self.assertEqual(reports[0]["sequence_gaps"], 1)
        self.assertEqual(reports[1]["sequence_gaps"], 0)
        self.assertEqual(reports[1]["derived_fps"], 5_000_000)

    def test_unknown_or_invalid_clock_not_reported_as_fps(self):
        path = self.root / "metadata.csv"
        path.write_text("timestamp_ns,sequence\n100,1\n90,2\n110,2\n", encoding="utf-8")
        report = ANALYZE.timestamp_metrics(path)["cameras"][0]
        self.assertEqual(report["backwards_timestamps"], 1)
        self.assertEqual(report["sequence_duplicates"], 1)
        self.assertIsNone(report["derived_fps"])
        path.write_text("timestamp_ns\n1.5\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            ANALYZE.timestamp_metrics(path)

    def test_timestamp_sample_window_is_bounded(self):
        path = self.root / "metadata.csv"
        path.write_text("timestamp_ns,clock_domain\n" + "".join(f"{i*100},MONOTONIC\n" for i in range(1, 30)), encoding="utf-8")
        result = ANALYZE.timestamp_metrics(path, sample_limit=3)["cameras"][0]
        self.assertEqual(result["sample_count"], 29)
        self.assertEqual(result["interval_window_count"], 3)

    def test_status_csv_uses_only_confirmed_readback(self):
        payload = dict(camera_id=0, state="DISCOVERED", source="MOCK_OR_TEST", iq_loaded="NOT_MEASURED",
                       owners=['daemon,"name'], controls=dict(exposure_us=10, hardware_confirmed=False))
        row = SAMPLE.status_row(payload, 0, 0, 100, 200)
        self.assertEqual(row["exposure_us"], "NOT_MEASURED")
        self.assertFalse(row["controls_hardware_confirmed"])
        payload["controls"]["hardware_confirmed"] = True
        self.assertEqual(SAMPLE.status_row(payload, 0, 0, 100, 200)["exposure_us"], "NOT_MEASURED")
        payload["source"] = "LIVE_DEVICE"
        self.assertEqual(SAMPLE.status_row(payload, 0, 0, 100, 200)["exposure_us"], 10)
        payload["controls"]["analog_gain"] = float("nan")
        with self.assertRaises(ValueError):
            SAMPLE.status_row(payload, 0, 0, 100, 200)

    def test_incomplete_sequence_and_domain_changes_are_not_joined(self):
        path = self.root / "metadata.csv"
        path.write_text("timestamp_ns,sequence,clock_domain\n100,1,MONOTONIC\n200,,MONOTONIC\n500,3,REALTIME\n600,4,REALTIME\n", encoding="utf-8")
        report = ANALYZE.timestamp_metrics(path)["cameras"][0]
        self.assertIsNone(report["sequence_gaps"])
        self.assertIsNone(report["derived_fps"])
        self.assertEqual(report["clock_domain_changes"], 1)
        self.assertEqual(report["interval_window_count"], 1)

    def test_cli_failure_and_timeout_are_preserved(self):
        completed = subprocess.CompletedProcess([], 3, json.dumps(dict(camera_id=1, status="BACKEND_UNAVAILABLE")), "")
        with patch.object(SAMPLE.subprocess, "run", return_value=completed) as run:
            row, ok = SAMPLE.collect_sample("path with spaces/cli", 1, "config file.yaml")
        self.assertFalse(ok)
        self.assertEqual(row["cli_exit_code"], 3)
        self.assertEqual(run.call_args.args[0][0], "path with spaces/cli")
        self.assertIn("config file.yaml", run.call_args.args[0])
        with patch.object(SAMPLE.subprocess, "run", side_effect=subprocess.TimeoutExpired([], 1)):
            row, ok = SAMPLE.collect_sample("cli", 0)
        self.assertFalse(ok)
        self.assertEqual(row["state"], "OBSERVATION_FAILED")

    def test_sampler_exclusive_csv_and_nonzero_on_failed_observation(self):
        cli, output = self.root / "cli", self.root / "samples.csv"
        cli.touch()
        row = {field: "NOT_MEASURED" for field in SAMPLE.FIELDS}
        row["message"] = 'comma,quote"newline\n'
        with patch.object(SAMPLE, "collect_sample", return_value=(row, False)), patch("sys.stderr", io.StringIO()):
            self.assertEqual(SAMPLE.main(["--cli", str(cli), "--output", str(output), "--samples", "1"]), 1)
            self.assertEqual(SAMPLE.main(["--cli", str(cli), "--output", str(output), "--samples", "1"]), 1)
        with output.open(newline="", encoding="utf-8") as stream:
            self.assertEqual(list(csv.DictReader(stream))[0]["message"], row["message"])


if __name__ == "__main__":
    unittest.main()
