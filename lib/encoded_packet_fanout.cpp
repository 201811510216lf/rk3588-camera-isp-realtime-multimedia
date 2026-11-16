#include "encoded_packet_fanout.h"

#include <utility>

bool EncodedPacketDistributor::add_sink(std::string name, Sink sink) {
    if (name.empty() || !sink) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const NamedSink &existing : sinks_) {
        if (existing.name == name) return false;
    }
    sinks_.push_back({std::move(name), std::move(sink)});
    return true;
}

bool EncodedPacketDistributor::publish(const EncodedPacket &packet) {
    std::vector<NamedSink> sinks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!packet.bytes || packet.bytes->empty() || packet.dts > packet.pts ||
            (last_pts_ >= 0 && packet.pts < last_pts_) ||
            (last_dts_ >= 0 && packet.dts < last_dts_)) {
            ++stats_.rejected;
            return false;
        }
        last_pts_ = packet.pts;
        last_dts_ = packet.dts;
        sinks = sinks_;
        ++stats_.published;
    }
    bool all_ok = true;
    uint64_t sink_errors = 0;
    for (const NamedSink &sink : sinks) {
        bool ok = false;
        try {
            ok = sink.sink(packet);
        } catch (...) {
            ok = false;
        }
        if (!ok) {
            all_ok = false;
            ++sink_errors;
        }
    }
    if (sink_errors != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.sink_errors += sink_errors;
    }
    return all_ok;
}

void EncodedPacketDistributor::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
    last_pts_ = -1;
    last_dts_ = -1;
}

EncodedPacketDistributor::Stats EncodedPacketDistributor::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

