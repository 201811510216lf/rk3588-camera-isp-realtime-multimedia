"""Test structured discovery output from synthetic saved diagnostics, never a device source."""
import json
import os
from pathlib import Path
import subprocess
import unittest


class MediaGraphCliTest(unittest.TestCase):
    def run_cli(self, *arguments):
        return subprocess.run([os.environ["MEDIA_GRAPH_VALIDATOR"], *arguments], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=10)

    def test_offline_topology_and_planes_are_structured_with_provenance(self):
        config = Path(__file__).resolve().parent / "fixtures" / "isp" / "isp_offline.yaml"
        result = self.run_cli("--config", str(config), "--json")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report["ok"])
        self.assertFalse(report["live_evidence"])
        self.assertFalse(report["hardware_confirmed"])
        self.assertFalse(report["role_hints_authoritative"])
        self.assertEqual(report["isp_3a_ready"], "NOT_MEASURED")
        self.assertEqual(len(report["cameras"]), 2)
        first, second = report["cameras"]
        self.assertEqual(first["graph"]["evidence"], "OFFLINE_FILE")
        self.assertEqual(first["enabled_path"], ["fixture sensor A", "fixture ISP A", "fixture output A"])
        self.assertEqual(first["graph"]["entities"][0]["role_hints"],
                         [{"role": "SENSOR", "source": "REPORTED_FUNCTION"}])
        self.assertIsNone(first["sensor"]["physical_camera_id"])
        self.assertEqual(second["output"]["active_format"], "NV12M")
        self.assertEqual(second["output"]["plane_count"], 2)
        self.assertEqual(len(second["output"]["planes"]), 2)

    def test_invalid_config_and_mixed_options_are_nonzero_json(self):
        result = self.run_cli("--config", "absent-isp-fixture.yaml", "--json")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)["status"], "VALIDATION_FAILED")
        result = self.run_cli("--config", "unused.yaml", "--width", "640", "--json")
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot be combined", json.loads(result.stdout)["error"])

    def test_bad_arguments_remain_valid_json_without_starting_device_queries(self):
        for arguments in [("--width", "nan"), ("--width", "--json"), ('--bad"option=foo',)]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments, "--json")
                self.assertEqual(result.returncode, 2)
                report = json.loads(result.stdout)
                self.assertFalse(report["ok"])
                self.assertFalse(report["hardware_confirmed"])


if __name__ == "__main__":
    unittest.main()
