#include "isp/isp_health_monitor.h"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace isp {
namespace {
std::optional<double> fps(const std::deque<uint64_t> &intervals) {
    if (intervals.empty())
        return std::nullopt;
    const long double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0L);
    return sum > 0 ? std::optional<double>(static_cast<double>(intervals.size() * 1'000'000'000.0L / sum))
                   : std::nullopt;
}
bool expired(uint64_t now, uint64_t last, uint64_t timeout) { return now < last || now - last > timeout; }
} // namespace
IspHealthMonitor::IspHealthMonitor(uint64_t capture_timeout, uint64_t service_timeout, size_t capacity)
    : capture_timeout_ns_(capture_timeout), service_timeout_ns_(service_timeout),
      interval_capacity_(capacity) {
    if (!capture_timeout || !service_timeout || !capacity || capacity > 65536)
        throw std::invalid_argument("ISP health timeouts and bounded interval capacity must be positive");
    for (IspCameraId id = 0; id < cameras_.size(); ++id) {
        cameras_[id].value.camera_id = id;
        cameras_[id].value.status.camera_id = id;
    }
}
bool IspHealthMonitor::observe_capture(IspCameraId id, const IspFrameObservation &frame) {
    if (id >= cameras_.size() || !frame.monotonic_timestamp_ns ||
        frame.evidence == EvidenceSource::NotMeasured)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto &camera = cameras_[id];
    auto &value = camera.value;
    if (!value.capture_expected) {
        value.capture_expected = true;
        value.stream_started_ns = frame.monotonic_timestamp_ns;
        value.capture_heartbeat_ns = 0;
        camera.previous.reset();
        camera.arrivals.clear();
        camera.source_intervals.clear();
    }
    if (camera.previous) {
        const auto &prior = *camera.previous;
        if (frame.monotonic_timestamp_ns <= prior.monotonic_timestamp_ns) {
            ++value.timestamp_errors;
            return false;
        }
        camera.arrivals.push_back(frame.monotonic_timestamp_ns - prior.monotonic_timestamp_ns);
        if (frame.sequence > prior.sequence) {
            value.sequence_gaps += frame.sequence - prior.sequence - 1;
        } else if (frame.sequence == prior.sequence) {
            ++value.sequence_duplicates;
        } else {
            ++value.sequence_backwards;
            camera.source_intervals.clear();
        }
        if (frame.evidence != prior.evidence) {
            // Do not join clock/rate measurements across test/offline/live sources.
            camera.arrivals.clear();
            camera.source_intervals.clear();
        } else if (frame.source_clock_known && prior.source_clock_known && frame.source_timestamp_ns &&
                   prior.source_timestamp_ns && frame.source_timestamp_ns > prior.source_timestamp_ns &&
                   frame.sequence > prior.sequence) {
            camera.source_intervals.push_back(frame.source_timestamp_ns - prior.source_timestamp_ns);
        } else {
            if (frame.source_clock_known && prior.source_clock_known)
                ++value.timestamp_errors;
            camera.source_intervals.clear();
        }
    }
    while (camera.arrivals.size() > interval_capacity_)
        camera.arrivals.pop_front();
    while (camera.source_intervals.size() > interval_capacity_)
        camera.source_intervals.pop_front();
    value.capture_heartbeat_ns = frame.monotonic_timestamp_ns;
    value.capture_evidence = frame.evidence;
    ++value.captured_frames;
    camera.previous = frame;
    return true;
}
bool IspHealthMonitor::capture_started(IspCameraId id, uint64_t now_ns) {
    if (id >= cameras_.size() || !now_ns)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto &camera = cameras_[id];
    if (camera.value.capture_expected)
        return false;
    camera.value.capture_expected = true;
    camera.value.stream_started_ns = now_ns;
    camera.value.capture_heartbeat_ns = 0;
    camera.previous.reset();
    camera.arrivals.clear();
    camera.source_intervals.clear();
    return true;
}
bool IspHealthMonitor::capture_stopped(IspCameraId id) {
    if (id >= cameras_.size())
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    cameras_[id].value.capture_expected = false;
    return true;
}
bool IspHealthMonitor::observe_service(const IspStatus &status, uint64_t now_ns) {
    if (status.camera_id >= cameras_.size() || !now_ns)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto &camera = cameras_[status.camera_id];
    if (camera.service_seen && now_ns < camera.value.service_observation_ns) {
        ++camera.value.timestamp_errors;
        return false;
    }
    camera.value.status = status;
    camera.value.service_observation_ns = now_ns;
    camera.value.service_evidence =
        status.owner.source != EvidenceSource::NotMeasured ? status.owner.source : status.backend.evidence;
    camera.service_seen = true;
    return true;
}
bool IspHealthMonitor::record_error(IspCameraId id) {
    if (id >= cameras_.size())
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    ++cameras_[id].value.errors;
    return true;
}
IspHealth IspHealthMonitor::snapshot(IspCameraId id, uint64_t now_ns) const {
    if (id >= cameras_.size())
        throw std::invalid_argument("ISP health camera_id must be 0 or 1");
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &camera = cameras_[id];
    auto result = camera.value;
    result.arrival_fps = fps(camera.arrivals);
    result.source_fps = fps(camera.source_intervals);
    if (!camera.arrivals.empty()) {
        auto sorted = std::vector<uint64_t>(camera.arrivals.begin(), camera.arrivals.end());
        std::sort(sorted.begin(), sorted.end());
        const double position = (sorted.size() - 1) * .95;
        const auto low = static_cast<size_t>(position);
        const auto high = std::min(low + 1, sorted.size() - 1);
        result.frame_interval_p95_ms =
            (sorted[low] + (sorted[high] - sorted[low]) * (position - low)) / 1'000'000.0;
    }
    const auto last_capture =
        result.capture_heartbeat_ns ? result.capture_heartbeat_ns : result.stream_started_ns;
    result.capture_stalled = result.capture_expected && expired(now_ns, last_capture, capture_timeout_ns_);
    result.service_stale =
        !camera.service_seen || expired(now_ns, result.service_observation_ns, service_timeout_ns_);
    result.state = result.status.state;
    // Explicit configuration/ownership failures always take precedence over observed frame arrival.
    if (result.state == IspRuntimeState::OwnershipConflict ||
        result.state == IspRuntimeState::ConfigMismatch || result.state == IspRuntimeState::Error ||
        result.state == IspRuntimeState::Unavailable)
        return result;
    if (result.capture_stalled) {
        result.state = IspRuntimeState::Degraded;
    } else if (result.service_stale) {
        result.state = camera.previous ? IspRuntimeState::Degraded : IspRuntimeState::Unknown;
    } else if (result.state == IspRuntimeState::Ready || result.state == IspRuntimeState::Running) {
        result.state =
            camera.previous && result.capture_expected ? IspRuntimeState::Running : IspRuntimeState::Ready;
    }
    return result;
}
std::string health_json(const IspHealth &value) {
    std::ostringstream out;
    out << "{\"camera_id\":" << value.camera_id << ",\"state\":\"" << to_string(value.state)
        << "\",\"capture_heartbeat_ns\":" << value.capture_heartbeat_ns
        << ",\"stream_started_ns\":" << value.stream_started_ns
        << ",\"service_observation_ns\":" << value.service_observation_ns
        << ",\"captured_frames\":" << value.captured_frames << ",\"sequence_gaps\":" << value.sequence_gaps
        << ",\"sequence_duplicates\":" << value.sequence_duplicates
        << ",\"sequence_backwards\":" << value.sequence_backwards
        << ",\"timestamp_errors\":" << value.timestamp_errors << ",\"errors\":" << value.errors
        << ",\"capture_stalled\":" << (value.capture_stalled ? "true" : "false")
        << ",\"capture_expected\":" << (value.capture_expected ? "true" : "false")
        << ",\"service_stale\":" << (value.service_stale ? "true" : "false") << ",\"capture_evidence\":\""
        << to_string(value.capture_evidence) << "\",\"service_evidence\":\""
        << to_string(value.service_evidence) << "\"";
    auto number = [&](const char *key, const std::optional<double> &n) {
        out << ",\"" << key << "\":";
        if (n)
            out << *n;
        else
            out << "null";
    };
    number("arrival_fps", value.arrival_fps);
    number("source_fps", value.source_fps);
    number("frame_interval_p95_ms", value.frame_interval_p95_ms);
    out << ",\"reconnect_status\":\"NOT_IMPLEMENTED\",\"isp_status\":" << status_json(value.status) << '}';
    return out.str();
}
} // namespace isp
