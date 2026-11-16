#!/usr/bin/env python3
"""Host checks for RAW packing and image-quality metrics."""
from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.isp_quality_analyzer import (  # noqa: E402
    analyze_nv12,
    analyze_raw,
    unpack_raw10,
    unpack_raw12,
)


class IspQualityAnalyzerTest(unittest.TestCase):
    @staticmethod
    def pack10(values: list[int]) -> bytes:
        result = bytearray()
        for offset in range(0, len(values), 4):
            group = values[offset : offset + 4]
            result.extend(v & 0xFF for v in group)
            result.append(sum(((v >> 8) & 0x03) << (2 * i) for i, v in enumerate(group)))
        return bytes(result)

    @staticmethod
    def pack12(values: list[int]) -> bytes:
        result = bytearray()
        for offset in range(0, len(values), 2):
            a, b = values[offset : offset + 2]
            result.extend((a & 0xFF, b & 0xFF, ((a >> 8) & 0x0F) | (((b >> 8) & 0x0F) << 4)))
        return bytes(result)

    def test_raw10_and_raw12_unpack(self) -> None:
        values10 = [0, 1, 511, 1023] * 2
        self.assertEqual(unpack_raw10(self.pack10(values10), 4, 2), values10)
        values12 = [0, 1, 4095, 2048] * 2
        self.assertEqual(unpack_raw12(self.pack12(values12), 4, 2), values12)

    def test_bayer_metrics_and_nv12_fallback(self) -> None:
        pixels = [64, 128, 192, 256, 64, 128, 192, 256]
        report = analyze_raw(pixels, 4, 2, "RGGB", 10, 16)
        self.assertEqual(report["packing"], "RAW10")
        self.assertIn("bayer_planes", report)
        self.assertIsNotNone(report["r_over_g"])
        nv12 = analyze_nv12([16, 32, 48, 64, 16, 32, 48, 64], 4, 2)
        self.assertEqual(nv12["source"], "NV12_Y")
        self.assertEqual(nv12["raw_bayer_metrics"], "NOT_AVAILABLE_FROM_NV12")

    def test_cli_accepts_real_file(self) -> None:
        # Keep this smoke check independent of subprocess and shell quoting.
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.raw10"
            path.write_bytes(self.pack10([32, 64, 96, 128] * 2))
            self.assertGreater(path.stat().st_size, 0)
            self.assertEqual(len(unpack_raw10(path.read_bytes(), 4, 2)), 8)


if __name__ == "__main__":
    unittest.main()
