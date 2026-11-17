#include "behavior/event_manager.h"
#include "bounded_frame_queue.h"
#include "frame_pairer.h"
#include "output/packet_sink.h"
#include "output/recording_index.h"

#include <atomic>
#include <cassert>

int main() {
    // Memory fixtures exercise software contracts; no Camera, NPU or encoder is emulated as a product source.
    BoundedFrameQueue<FrameSample> queue0(2), queue1(2);
    BoundedFrameQueue<FrameSample> *queues[2]{&queue0, &queue1};
    FramePairer pairer;
    behavior::RatTracker tracker;
    behavior::EventConfig config;
    config.session_id = "integration";
    config.min_observations = 2;
    config.min_duration_ns = 100'000'000;
    config.missing_grace_ns = 200'000'000;
    behavior::EventManager events(config);
    std::atomic<unsigned> consumed{0};
    output::AsyncPacketSink sink(4, output::AsyncPacketSink::Overflow::FailRecording,
                                 [&](const EncodedPacket &packet) {
                                     assert(packet.source_count == 2);
                                     assert(packet.sources[0].sequence == packet.sources[1].sequence);
                                     const auto row = output::packet_index_json("fixture.mp4", packet, 0);
                                     assert(row.find("fixture.mp4") != std::string::npos);
                                     ++consumed;
                                     return true;
                                 });
    uint64_t track_id = 0;
    for (uint64_t sequence = 1; sequence <= 2; ++sequence) {
        for (uint32_t camera_id = 0; camera_id < 2; ++camera_id) {
            FrameSample sample;
            sample.camera_id = camera_id;
            sample.sequence = sequence;
            sample.timestamp_ns = sequence * 100'000'000 + camera_id * 1000;
            sample.arrival_ns = sequence * 100'000'000;
            sample.clock_domain = FrameClockDomain::Monotonic;
            assert(queues[camera_id]->push(std::move(sample)));
        }
        FrameSample a, b;
        assert(queues[0]->pop(a) && queues[1]->pop(b));
        assert(pairer.push(a, a.arrival_ns).empty());
        auto pairs = pairer.push(b, b.arrival_ns);
        assert(pairs.size() == 1 && !pairs[0].is_stale());
        behavior::DetectionResult result;
        result.frame.camera_id = 0;
        result.frame.sequence = a.sequence;
        result.frame.source_timestamp_ns = a.timestamp_ns;
        result.frame.monotonic_timestamp_ns = a.arrival_ns;
        result.frame.model_id = "test-fixture";
        result.frame.model_version = "1";
        result.detections.push_back({{1, 1, 4, 4}, 0, 0.9F});
        auto tracks = tracker.update(result);
        assert(tracks.size() == 1);
        if (track_id)
            assert(track_id == tracks[0].track_id);
        track_id = tracks[0].track_id;
        assert(behavior::classify_behavior(result.frame, tracks, {}).samples.empty());
        assert(
            events.update(behavior::classify_behavior(result.frame, tracks, {{0, "fixture-state"}})).empty());
        EncodedPacket packet;
        packet.bytes = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{0x01});
        packet.pts = static_cast<int64_t>(sequence - 1);
        packet.dts = packet.pts;
        packet.time_base_den = 10;
        packet.keyframe = sequence == 1;
        packet.source_count = 2;
        packet.sources[0] = {0, a.sequence, a.timestamp_ns};
        packet.sources[1] = {1, b.sequence, b.timestamp_ns};
        assert(sink.enqueue(packet));
    }
    sink.close();
    assert(consumed == 2);
    auto completed = events.tick(0, 500'000'000);
    assert(completed.size() == 1 && completed[0].first_sequence == 1 && completed[0].last_sequence == 2);
    assert(behavior::add_video_span(&completed[0], {"fixture.mp4", 0, 100'000'000, 1, 2}));
    assert(behavior::event_json(completed[0]).find("fixture.mp4") != std::string::npos);
    for (auto *queue : queues)
        queue->close();
    pairer.clear();
    return 0;
}
