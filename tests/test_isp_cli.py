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


if __name__ == "__main__":
    unittest.main()
