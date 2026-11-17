#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// A single encoded packet is owned once and shared by all output sinks. The
// packet bytes remain alive until every sink has returned from consume(). This
// is the ownership boundary used by RTMP and segmented recording adapters.
struct EncodedPacket {
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    int64_t pts = 0;
    int64_t dts = 0;
    bool keyframe = false;
    // PTS/DTS are ticks in this rational time base, never source nanoseconds.
    uint32_t time_base_num = 1;
    uint32_t time_base_den = 30;
    struct SourceFrame {
        uint32_t camera_id = 0;
        uint64_t sequence = 0;
        uint64_t timestamp_ns = 0;
    };
    std::array<SourceFrame, 2> sources{};
    size_t source_count = 0;
};

class EncodedPacketDistributor {
  public:
    using Sink = std::function<bool(const EncodedPacket &)>;

    struct Stats {
        uint64_t published = 0;
        uint64_t rejected = 0;
        uint64_t sink_errors = 0;
    };

    bool add_sink(std::string name, Sink sink);
    bool publish(const EncodedPacket &packet);
    void clear();
    Stats stats() const;

  private:
    struct NamedSink {
        std::string name;
        Sink sink;
    };

    mutable std::mutex mutex_;
    std::vector<NamedSink> sinks_;
    Stats stats_;
    int64_t last_pts_ = -1;
    int64_t last_dts_ = -1;
};
