"""Focused host tests for RAW packing and image-level IQ measurements."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "isp_quality_analyzer.py"
SPEC = importlib.util.spec_from_file_location("isp_quality_analyzer", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def pack_raw10(values: list[int]) -> bytes:
    assert len(values) % 4 == 0
    result = bytearray()
    for offset in range(0, len(values), 4):
        p0, p1, p2, p3 = values[offset : offset + 4]
        result.extend((p0 & 0xFF, p1 & 0xFF, p2 & 0xFF, p3 & 0xFF,
                       ((p0 >> 8) & 3) | ((p1 >> 6) & 0x0C) |
                       ((p2 >> 4) & 0x30) | ((p3 >> 2) & 0xC0)))
    return bytes(result)


def pack_raw12(values: list[int]) -> bytes:
    assert len(values) % 2 == 0
    result = bytearray()
    for offset in range(0, len(values), 2):
        p0, p1 = values[offset : offset + 2]
        result.extend((p0 & 0xFF, p1 & 0xFF,
                       ((p0 >> 8) & 0x0F) | ((p1 >> 4) & 0xF0)))
    return bytes(result)


class RawQualityAnalyzerTest(unittest.TestCase):
    def test_raw10_unpack_with_high_bits(self) -> None:
        values = [0, 1, 0x155, 0x2AA, 0x3FF, 0x123, 0x001, 0x200]
        packed = pack_raw10(values)
        self.assertEqual(MODULE.unpack_raw10(packed, 4, 2), values)

    def test_raw12_unpack_with_high_bits(self) -> None:
        values = [0x000, 0xFFF, 0xABC, 0x123]
        packed = pack_raw12(values)
        self.assertEqual(MODULE.unpack_raw12(packed, 2, 2), values)

    def test_row_stride_skips_padding(self) -> None:
        row0 = pack_raw10([1, 2, 3, 4]) + b"\xEE\xEE"
        row1 = pack_raw10([5, 6, 7, 8]) + b"\xDD\xDD"
        self.assertEqual(MODULE.unpack_raw10(row0 + row1, 4, 2, 7), [1, 2, 3, 4, 5, 6, 7, 8])

    def test_bayer_stats_and_clipping(self) -> None:
        # RGGB 2x2: R=100, G=50/50, B=200.
        values = [100, 50, 50, 200]
        report = MODULE.analyze_raw(values, 2, 2, "rggb", 10, 10)
        channels = report["bayer_planes"]
        self.assertEqual(channels["R"]["mean"], 100.0)
        self.assertEqual(channels["G1"]["mean"], 50.0)
        self.assertEqual(channels["G2"]["mean"], 50.0)
        self.assertEqual(channels["B"]["mean"], 200.0)
        self.assertAlmostEqual(report["r_over_g"], 2.0)
        self.assertIsNotNone(report["overexposed_fraction"])

    def test_nv12_reads_only_y_plane(self) -> None:
        values = bytes([10, 20, 30, 40, 99, 99])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.nv12"
            path.write_bytes(values)
            self.assertEqual(MODULE.read_nv12_y(path, 2, 2), [10, 20, 30, 40])

    def test_cli_json_reports_sequence_and_explicit_limits(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frame0 = root / "frame0.raw10"
            frame1 = root / "frame1.raw10"
            frame0.write_bytes(pack_raw10([100, 50, 50, 200, 100, 50, 50, 200]))
            frame1.write_bytes(pack_raw10([101, 51, 51, 201, 101, 51, 51, 201]))
            frame0_pixels = MODULE.unpack_raw10(frame0.read_bytes(), 4, 2)
            frame1_pixels = MODULE.unpack_raw10(frame1.read_bytes(), 4, 2)
            result = MODULE.frame_luma_stability([frame0_pixels, frame1_pixels])
            self.assertEqual(result["frame_count"], 2)
            self.assertEqual(result["ae_convergence"], "NOT_INFERRED")
            encoded = json.dumps(result)
            self.assertIn("frame_to_frame_luma_stddev", encoded)


if __name__ == "__main__":
    unittest.main()
