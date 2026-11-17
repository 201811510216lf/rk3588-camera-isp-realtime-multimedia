#include "output/recording_index.h"

#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace output {
std::optional<uint64_t> packet_time_ns(int64_t ticks, uint32_t num, uint32_t den) {
    if (ticks < 0 || !num || !den)
        return std::nullopt;
    const uint64_t factor = static_cast<uint64_t>(num) * 1'000'000'000ULL;
    const uint64_t count = static_cast<uint64_t>(ticks);
    const uint64_t seconds = count / den;
    const uint64_t remainder = count % den;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (seconds > maximum / factor)
        return std::nullopt;
    // num is uint32, remainder < den <= uint32, product fits uint64.
    const uint64_t remainder_units = remainder * static_cast<uint64_t>(num);
    const uint64_t extra_seconds = remainder_units / den;
    const uint64_t extra_fraction = remainder_units % den;
    if (extra_seconds > maximum / 1'000'000'000ULL)
        return std::nullopt;
    const uint64_t extra = extra_seconds * 1'000'000'000ULL + (extra_fraction * 1'000'000'000ULL) / den;
    const uint64_t whole = seconds * factor;
    if (extra > maximum - whole)
        return std::nullopt;
    return whole + extra;
}

std::string segment_name(uint64_t wallclock_ns, uint64_t number, int camera_id) {
    const auto seconds = static_cast<std::time_t>(wallclock_ns / 1'000'000'000ULL);
    std::tm wall{};
#ifdef _WIN32
    gmtime_s(&wall, &seconds);
#else
    gmtime_r(&seconds, &wall);
#endif
    std::ostringstream result;
    result << (camera_id < 0 ? "joint" : "cam" + std::to_string(camera_id)) << '-'
           << std::put_time(&wall, "%Y%m%dT%H%M%S") << '-' << std::setw(9) << std::setfill('0')
           << wallclock_ns % 1'000'000'000ULL << "Z-" << number << ".mp4";
    return result.str();
}

std::string packet_index_json(const std::string &segment_id, const EncodedPacket &packet, uint64_t first_ns) {
    const auto time = packet_time_ns(packet.pts, packet.time_base_num, packet.time_base_den);
    if (!time || *time < first_ns || packet.source_count > packet.sources.size())
        return {};
    std::ostringstream out;
    out << "{\"type\":\"packet\",\"segment_id\":\"" << segment_id
        << "\",\"video_offset_ns\":" << *time - first_ns << ",\"pts\":" << packet.pts
        << ",\"time_base_num\":" << packet.time_base_num << ",\"time_base_den\":" << packet.time_base_den
        << ",\"keyframe\":" << (packet.keyframe ? "true" : "false") << ",\"sources\":[";
    for (size_t i = 0; i < packet.source_count; ++i) {
        if (i)
            out << ',';
        const auto &source = packet.sources[i];
        out << "{\"camera_id\":" << source.camera_id << ",\"sequence\":" << source.sequence
            << ",\"source_timestamp_ns\":" << source.timestamp_ns << '}';
    }
    out << "]}";
    return out.str();
}
std::string finalized_index_json(const std::string &segment_id, bool finalized) {
    return "{\"type\":\"finalize\",\"segment_id\":\"" + segment_id +
           "\",\"finalized\":" + (finalized ? "true}" : "false}");
}
} // namespace output
