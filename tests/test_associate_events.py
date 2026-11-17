import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("associate_events", Path(__file__).resolve().parents[1] / "tools" / "associate_events.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class EventAssociationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = self.root / "index.jsonl"
        self.event = dict(event_id="fixture-1", camera_id=0, first_sequence=10, last_sequence=13,
                          start_timestamp_ns=100, end_timestamp_ns=400)

    def write_index(self, records):
        self.index.write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")

    @staticmethod
    def packet(seq, segment="a.mp4", offset=0, camera=0):
        return dict(type="packet", segment_id=segment, video_offset_ns=offset,
                    sources=[dict(camera_id=camera, sequence=seq, source_timestamp_ns=(seq - 9) * 100)])

    @staticmethod
    def final(segment="a.mp4", ok=True):
        return dict(type="finalize", segment_id=segment, finalized=ok)

    def test_multiple_finalized_segments_and_sparse_ai_endpoints(self):
        self.write_index([self.packet(10), self.packet(11, offset=10), self.final(),
                          self.packet(12, "b.mp4"), self.packet(13, "b.mp4", 10), self.final("b.mp4")])
        result = MODULE.associate_event(self.event, self.index)
        self.assertEqual(result["video_association"]["status"], "mapped")
        self.assertEqual(len(result["video_spans"]), 2)
        self.assertEqual(result["video_spans"][1]["first_sequence"], 12)

    def test_gap_is_partial_and_unfinalized_is_not_claimed(self):
        self.write_index([self.packet(10), self.packet(11), self.final(), self.packet(13, "b.mp4"), self.final("b.mp4", False)])
        result = MODULE.associate_event(self.event, self.index)
        self.assertEqual(result["video_association"]["status"], "partial")
        self.assertEqual(result["video_association"]["missing_frame_count"], 2)
        self.assertEqual(result["video_association"]["unfinalized_segments"], ["b.mp4"])
        self.assertEqual(len(result["video_spans"]), 1)

    def test_wrong_camera_source_clock_or_missing_finalize_remains_unmapped(self):
        self.write_index([self.packet(10, camera=1), self.packet(10), self.packet(11)])
        result = MODULE.associate_event(self.event, self.index)
        self.assertEqual(result["video_association"]["status"], "unmapped")
        self.assertEqual(result["video_spans"], [])
        wrong = self.packet(10); wrong["sources"][0]["source_timestamp_ns"] = 200
        self.write_index([wrong, self.final()])
        self.assertEqual(MODULE.associate_event(self.event, self.index)["video_association"]["timestamp_or_order_conflicts"], 1)

    def test_real_files_required_when_requested_and_existing_output_preserved(self):
        self.write_index([self.packet(i, offset=i) for i in range(10, 14)] + [self.final()])
        result = MODULE.associate_event(self.event, self.index, recording_dir=self.root)
        self.assertEqual(result["video_association"]["status"], "unmapped")
        (self.root / "a.mp4").touch()
        self.assertEqual(MODULE.associate_event(self.event, self.index, recording_dir=self.root)["video_association"]["status"], "mapped")
        events = self.root / "events.jsonl"; events.write_text(json.dumps(self.event) + "\n{}\n", encoding="utf-8")
        output = self.root / "mapped.jsonl"
        counts = MODULE.associate_files(events, self.index, output)
        self.assertEqual(counts["mapped"], 1)
        self.assertEqual(counts["non_event_records"], 1)
        with self.assertRaises(FileExistsError):
            MODULE.associate_files(events, self.index, output)
        with self.assertRaises(ValueError):
            MODULE.associate_files(events, self.index, events)

    def test_reused_session_sequences_and_unknown_clock_are_not_guessed(self):
        self.write_index([self.packet(i, offset=i) for i in range(10, 14)] + [self.final()] +
                         [self.packet(i, "other-session.mp4", i) for i in range(10, 14)] + [self.final("other-session.mp4")])
        result = MODULE.associate_event(self.event, self.index)
        self.assertEqual(result["video_association"]["status"], "unmapped")
        self.assertEqual(result["video_spans"], [])
        event = dict(self.event, start_timestamp_ns=0, end_timestamp_ns=0)
        packets = [self.packet(i, offset=i) for i in range(10, 14)]
        for packet in packets:
            packet["sources"][0]["source_timestamp_ns"] = 0
        self.write_index(packets + [self.final()])
        result = MODULE.associate_event(event, self.index)
        self.assertFalse(result["video_association"]["source_clock_available"])
        self.assertEqual(result["video_spans"], [])


if __name__ == "__main__":
    unittest.main()
