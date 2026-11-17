"""Exercise the real discovery-only CLI, never a fake runtime control source."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class IspCliTest(unittest.TestCase):
    def run_cli(self, *arguments):
        return subprocess.run([os.environ["ISP_CONTROL_CLI"], *arguments], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=10)

    def test_status_and_capabilities_never_claim_readiness(self):
        result = self.run_cli("--isp-status", "--json")
        self.assertIn(result.returncode, (0, 3), result.stderr)
        status = json.loads(result.stdout)
        self.assertNotIn(status["state"], ("READY", "RUNNING"))
        self.assertFalse(status["hardware_confirmed"])
        self.assertNotEqual(status["iq_loaded"], "CONFIRMED")
        self.assertIsNone(status["controls"]["exposure_us"])
        self.assertFalse(json.loads(self.run_cli("--list-controls", "--json").stdout)["capabilities"]["readback"])

    def test_unsupported_controls_are_structured_and_nonzero(self):
        for arguments in [("--ae-auto",), ("--ae-manual",), ("--awb-auto",), ("--awb-manual",),
                          ("--exposure-us", "10000"), ("--gain", "2")]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments, "--json")
                self.assertNotEqual(result.returncode, 0)
                record = json.loads(result.stdout)
                self.assertIn(record["status"], ("UNSUPPORTED", "BACKEND_UNAVAILABLE"))
                self.assertFalse(record["hardware_confirmed"])

    def test_invalid_values_and_multiple_controls(self):
        for arguments in [("--camera-id", "-1"), ("--camera-id", "2"), ("--gain", "inf"),
                          ("--gain", "nan"), ("--exposure-us", "1oops"), ("--gain", "0"),
                          ("--ae-auto", "--awb-auto"), ("--iq-root",)]:
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_cli(*arguments).returncode, 2)

    def test_dump_is_json_and_existing_evidence_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            first = self.run_cli("--dump-isp-state", str(path), "--json")
            self.assertIn(first.returncode, (0, 3), first.stderr)
            self.assertFalse(json.loads(path.read_text())["hardware_confirmed"])
            prior = path.read_bytes()
            self.assertEqual(self.run_cli("--dump-isp-state", str(path)).returncode, 2)
            self.assertEqual(path.read_bytes(), prior)


    def test_sdk_info_and_capability_ranges_are_explicit(self):
        result = self.run_cli("--sdk-info", "--json")
        self.assertIn(result.returncode, (0, 3))
        info = json.loads(result.stdout)
        self.assertFalse(info["sdk_available"])
        self.assertEqual(info["sdk_version"], "UNKNOWN")
        self.assertEqual(info["sdk_version_source"], "NOT_MEASURED")
        self.assertIsNone(info["capabilities"]["exposure_us_range"]["minimum"])
        self.assertIsNone(info["capabilities"]["gain_range"]["maximum"])

    def test_explicit_control_queries_fail_without_backend(self):
        for operation in ("--get-controls", "--get-ae-mode", "--get-exposure-us", "--get-gain", "--get-awb-mode"):
            with self.subTest(operation=operation):
                result = self.run_cli(operation, "--json")
                self.assertEqual(result.returncode, 3, result.stderr)
                record = json.loads(result.stdout)
                self.assertIn(record["status"], ("UNSUPPORTED", "BACKEND_UNAVAILABLE"))
                self.assertFalse(record["controls"]["hardware_confirmed"])
                self.assertIsNone(record["controls"]["exposure_us"])

    def debug_cli(self, *arguments):
        return subprocess.run([os.environ["ISP_DEBUG_TOOL"], *arguments], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=10)

    def test_debug_aliases_share_status_and_readback(self):
        result = self.debug_cli("--status", "--camera=1", "--json")
        self.assertIn(result.returncode, (0, 3), result.stderr)
        status = json.loads(result.stdout)
        self.assertEqual(status["camera_id"], 1)
        self.assertFalse(status["hardware_confirmed"])
        self.assertIn("sdk_version_source", status)
        for operation in ("--ae-status", "--awb-status"):
            result = self.debug_cli(operation, "--json")
            self.assertEqual(result.returncode, 3)
            self.assertEqual(json.loads(result.stdout)["status"], "UNSUPPORTED")

    def test_legacy_list_preserves_inventory_without_inventing_cameras(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "video-fake-camera"
            fake.write_text("not a device")
            result = subprocess.run([os.environ["ISP_DEBUG_TOOL"], "--list", "--json"], cwd=directory,
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            record = json.loads(result.stdout)
            self.assertFalse(record["camera_mapping_inferred"])
            self.assertEqual(record["status"], "DISCOVERY_ONLY")
            self.assertNotIn(str(fake), record["device_nodes"])

    def test_debug_invalid_inputs_never_throw_or_write(self):
        for arguments in [("--camera", "-1"), ("--camera", "999999999999999999999"),
                          ("--set-exposure", "bad"), ("--set-gain", "nan"),
                          ("--capture-nv12", "--width", "4294967296"),
                          ("--capture-nv12", "--set-gain", "2"), ("--status", "--set-gain", "2")]:
            with self.subTest(arguments=arguments):
                result = self.debug_cli(*arguments, "--json")
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(json.loads(result.stdout)["status"], "INVALID_ARGUMENT")
        result = self.debug_cli("--capture-nv12", "--device", "/dev/unavailable", "--width", "640", "--height", "480", "--json")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(json.loads(result.stdout)["status"], "UNSUPPORTED")

    def test_structured_media_diagnostics_reused_by_both_tools(self):
        fixture = Path(__file__).resolve().parent / "fixtures" / "isp" / "isp_offline.yaml"
        arguments = ("--media-graph", "--isp-config", str(fixture), "--json")
        control = self.run_cli(*arguments)
        debug = self.debug_cli(*arguments)
        self.assertEqual(control.returncode, 0, control.stderr)
        self.assertEqual(debug.returncode, 0, debug.stderr)
        first, second = json.loads(control.stdout), json.loads(debug.stdout)
        self.assertEqual(first, second)
        self.assertFalse(first["live_evidence"])
        self.assertEqual(len(first["cameras"]), 2)
        self.assertNotEqual(first.get("isp_3a_ready"), True)


if __name__ == "__main__":
    unittest.main()
