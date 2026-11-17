"""Run the Linux capture executable without creating a Camera input substitute."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("ISP_CAPTURE"), "Linux V4L2 capture target required")
class CaptureCliTest(unittest.TestCase):
    def run_capture(self, *args):
        return subprocess.run([os.environ["ISP_CAPTURE"], *args], text=True,
                              capture_output=True, timeout=5)

    def test_help_is_read_only(self):
        result = self.run_capture("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MMAP", result.stdout)

    def test_invalid_limits_and_missing_selector_do_not_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            out_dir = Path(temporary) / "evidence"
            base = ["--isp-config", "missing.yaml", "--camera-id", "0", "--out-dir", str(out_dir)]
            cases = [base + ["--frames", "0"], base + ["--frames", "-1"],
                     base + ["--frames", "1000001"], base + ["--timeout-sec", "3601"],
                     base + ["--camera-id", "1"], base + ["--frames"],
                     base + ["--unknown"], base[:2] + base[4:]]
            for args in cases:
                with self.subTest(args=args):
                    result = self.run_capture(*args)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("CAPTURE_FAILED", result.stderr)
                    self.assertFalse(out_dir.exists())

    def test_existing_evidence_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            marker = Path(temporary) / "capture.nv12"
            marker.write_bytes(b"existing evidence")
            result = self.run_capture("--isp-config", "missing.yaml", "--camera-id", "0",
                                      "--out-dir", temporary)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must not exist", result.stderr)
            self.assertEqual(marker.read_bytes(), b"existing evidence")

    def test_missing_configuration_is_explicit(self):
        with tempfile.TemporaryDirectory() as temporary:
            out_dir = Path(temporary) / "capture"
            result = self.run_capture("--isp-config", str(Path(temporary) / "absent.yaml"),
                                      "--camera-id", "1", "--out-dir", str(out_dir))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CAPTURE_FAILED", result.stderr)
            self.assertFalse(out_dir.exists())


if __name__ == "__main__":
    unittest.main()
