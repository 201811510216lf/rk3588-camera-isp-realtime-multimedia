#include "output/packet_sink.h"
#include "output/recording_index.h"

#include <cassert>
#include <future>
#include <limits>
#include <stdexcept>

namespace {
EncodedPacket packet(int64_t sequence, bool keyframe = false) {
    EncodedPacket value;
    value.bytes = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{0, 0, 1, 0x65});
    value.pts = value.dts = sequence;
    value.keyframe = keyframe;
    value.time_base_den = 30;
    value.source_count = 1;
    value.sources[0] = {1, static_cast<uint64_t>(sequence), static_cast<uint64_t>(sequence) * 33'333'333ULL};
    return value;
}
} // namespace

int main() {
    assert(output::camera_publish_url("rtsp://user:password@host/live?token=x#frag", 1) ==
           "rtsp://user:password@host/live.cam1?token=x#frag");
    assert(output::camera_publish_url("rtmp://host", 0) == "rtmp://host/cam0");
    assert(output::packet_time_ns(30, 1, 30) == 1'000'000'000ULL);
    assert(output::packet_time_ns(1, 1, 30) == 33'333'333ULL);
    assert(output::packet_time_ns(3, 1001, 30000) == 100'100'000ULL);
    assert(!output::packet_time_ns(-1, 1, 30));
    assert(!output::packet_time_ns(1, 1, 0));
    assert(!output::packet_time_ns(std::numeric_limits<int64_t>::max(), 100000, 1));
    assert(output::segment_name(0, 2, 1) == "cam1-19700101T000000-000000000Z-2.mp4");
    const auto index = output::packet_index_json("segment.mp4", packet(30, true), 500'000'000);
    assert(index.find("\"video_offset_ns\":500000000") != std::string::npos);
    assert(index.find("\"camera_id\":1") != std::string::npos);
    assert(output::packet_index_json("segment.mp4", packet(0), 1).empty());

    std::promise<void> entered, unblock;
    auto gate = unblock.get_future().share();
    output::AsyncPacketSink network(2, output::AsyncPacketSink::Overflow::ResyncAtKeyframe,
                                    [&](const EncodedPacket &value) {
                                        if (value.pts == 0) {
                                            entered.set_value();
                                            gate.wait();
                                        }
                                        return true;
                                    });
    assert(network.enqueue(packet(0, true)));
    assert(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(network.enqueue(packet(1)));
    assert(network.enqueue(packet(2)));
    assert(network.enqueue(packet(3))); // Drops pending GOP; waits for next IDR.
    assert(network.stats().depth == 0 && network.stats().dropped == 3);
    assert(network.enqueue(packet(4, true)));

    // A blocked network callback cannot prevent an independent recording sink.
    std::promise<void> recorded;
    output::AsyncPacketSink recorder(2, output::AsyncPacketSink::Overflow::FailRecording,
                                     [&](const EncodedPacket &) {
                                         recorded.set_value();
                                         return true;
                                     });
    assert(recorder.enqueue(packet(0, true)));
    assert(recorded.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    recorder.close();
    unblock.set_value();
    network.close();
    assert(network.stats().consumed == 2);
    assert(network.stats().high_water <= 2);
    assert(!network.enqueue(packet(5)));

    std::promise<void> entered_record, unblock_record;
    auto record_gate = unblock_record.get_future().share();
    output::AsyncPacketSink overflow(1, output::AsyncPacketSink::Overflow::FailRecording,
                                     [&](const EncodedPacket &value) {
                                         if (value.pts == 0) {
                                             entered_record.set_value();
                                             record_gate.wait();
                                         }
                                         return true;
                                     });
    assert(overflow.enqueue(packet(0, true)));
    assert(entered_record.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(overflow.enqueue(packet(1)));
    assert(!overflow.enqueue(packet(2)));
    assert(overflow.stats().failed && overflow.stats().errors == 1);
    unblock_record.set_value();
    overflow.close();

    output::AsyncPacketSink throwing(
        1, output::AsyncPacketSink::Overflow::FailRecording,
        [](const EncodedPacket &) -> bool { throw std::runtime_error("sink failure"); });
    assert(throwing.enqueue(packet(0, true)));
    throwing.close();
    assert(throwing.stats().failed && throwing.stats().errors == 1);
    for (unsigned run = 0; run < 100; ++run) {
        output::AsyncPacketSink repeated(2, output::AsyncPacketSink::Overflow::ResyncAtKeyframe,
                                         [](const EncodedPacket &) { return true; });
        assert(repeated.enqueue(packet(0, true)));
        repeated.close();
        repeated.close();
    }
#ifndef __linux__
    output::FfmpegProcess unavailable;
    std::string error;
    assert(!unavailable.open({"-version"}, &error));
    assert(error.find("requires Linux") != std::string::npos);
#endif
    return 0;
}
