#pragma once

#include "isp/isp_types.h"

#include <array>
#include <deque>
#include <mutex>

namespace isp {
struct IspFrameObservation {
    uint64_t sequence = 0;
    uint64_t source_timestamp_ns = 0;
    uint64_t monotonic_timestamp_ns = 0;
    bool source_clock_known = false;
    EvidenceSource evidence = EvidenceSource::NotMeasured;
};
struct IspHealth {
    IspCameraId camera_id = 0;
    IspRuntimeState state = IspRuntimeState::Unknown;
    uint64_t capture_heartbeat_ns = 0;
    uint64_t stream_started_ns = 0;
    uint64_t service_observation_ns = 0;
    uint64_t captured_frames = 0;
    uint64_t sequence_gaps = 0;
    uint64_t sequence_duplicates = 0;
    uint64_t sequence_backwards = 0;
    uint64_t timestamp_errors = 0;
    uint64_t errors = 0;
    bool capture_stalled = false;
    bool capture_expected = false;
    bool service_stale = true;
    EvidenceSource capture_evidence = EvidenceSource::NotMeasured;
    EvidenceSource service_evidence = EvidenceSource::NotMeasured;
    std::optional<double> arrival_fps;
    std::optional<double> source_fps;
    std::optional<double> frame_interval_p95_ms;
    IspStatus status;
    std::string reconnect_status = "NOT_IMPLEMENTED";
};
class IIspHealthProvider {
  public:
    virtual ~IIspHealthProvider() = default;
    virtual IspHealth snapshot(IspCameraId camera_id, uint64_t now_ns) const = 0;
};
// Each sample carries its origin. A test/offline observation is never promoted to hardware evidence.
class IspHealthMonitor final : public IIspHealthProvider {
  public:
    explicit IspHealthMonitor(uint64_t capture_timeout_ns = 5'000'000'000ULL,
                              uint64_t service_timeout_ns = 5'000'000'000ULL, size_t interval_capacity = 256);
    bool observe_capture(IspCameraId camera_id, const IspFrameObservation &frame);
    bool capture_started(IspCameraId camera_id, uint64_t monotonic_ns);
    bool capture_stopped(IspCameraId camera_id);
    bool observe_service(const IspStatus &status, uint64_t monotonic_ns);
    bool record_error(IspCameraId camera_id);
    IspHealth snapshot(IspCameraId camera_id, uint64_t now_ns) const override;

  private:
    struct Camera {
        IspHealth value;
        std::optional<IspFrameObservation> previous;
        std::deque<uint64_t> arrivals, source_intervals;
        bool service_seen = false;
    };
    uint64_t capture_timeout_ns_, service_timeout_ns_;
    size_t interval_capacity_;
    mutable std::mutex mutex_;
    std::array<Camera, 2> cameras_;
};
std::string health_json(const IspHealth &health);
} // namespace isp
