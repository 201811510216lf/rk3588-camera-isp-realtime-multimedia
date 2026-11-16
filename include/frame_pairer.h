#pragma once

#include "camera_frame.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

// Pairing is deliberately independent from the capture implementation.  A
// sample contains the timestamp supplied by V4L2 (or a test source) and a
// CameraFrame lease.  The pairer never fabricates a timestamp and never
// compares samples from different clock domains.
using FrameClockDomain = camera::CameraClockDomain;

enum class FramePairStatus : uint8_t {
    Matched = 0,
    StaleHoldLast = 1,
};

enum class FramePairPolicy : uint8_t {
    DropOlder = 0,
    HoldLast = 1,
};

struct FrameSample {
    uint32_t camera_id = 0;
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint64_t normalized_timestamp_ns = 0;
    FrameClockDomain clock_domain = FrameClockDomain::Unknown;
    // Arrival time is in the pairer's local monotonic domain.  A zero value
    // means "set by FramePairer::push".
    uint64_t arrival_ns = 0;
    camera::CameraFrame frame;
};

struct FramePair {
    uint64_t pair_id = 0;
    FrameSample cam0;
    FrameSample cam1;
    uint64_t delta_ns = 0;
    FramePairStatus status = FramePairStatus::Matched;
    bool clock_domain_verified = false;

    bool is_stale() const { return status == FramePairStatus::StaleHoldLast; }
};

struct FramePairerConfig {
    uint64_t tolerance_ns = 5'000'000;   // 5 ms
    uint64_t max_wait_ns = 50'000'000;    // 50 ms
    size_t max_queue_depth = 8;
    FramePairPolicy policy = FramePairPolicy::DropOlder;
};

struct FramePairerStats {
    uint64_t matched_pairs = 0;
    uint64_t stale_pairs = 0;
    uint64_t pair_drops = 0;
    uint64_t camera0_source_drops = 0;
    uint64_t camera1_source_drops = 0;
    uint64_t out_of_order_drops = 0;
    uint64_t clock_domain_rejects = 0;
    uint64_t timeout_drops = 0;
    uint64_t max_depth_camera0 = 0;
    uint64_t max_depth_camera1 = 0;
    uint64_t last_delta_ns = 0;
    uint64_t max_delta_ns = 0;
    std::vector<uint64_t> deltas_ns;

    double percentile(double p) const;
    double p50_ms() const { return percentile(50.0) / 1'000'000.0; }
    double p95_ms() const { return percentile(95.0) / 1'000'000.0; }
    double p99_ms() const { return percentile(99.0) / 1'000'000.0; }
};

class FramePairer {
public:
    explicit FramePairer(FramePairerConfig config = {});

    FramePairer(const FramePairer &) = delete;
    FramePairer &operator=(const FramePairer &) = delete;

    // Push one sample and return every pair that became ready.  now_ns is a
    // monotonic value used only for timeout accounting; tests may provide a
    // deterministic value.
    std::vector<FramePair> push(FrameSample sample, uint64_t now_ns = 0);

    // Expire samples that have waited beyond max_wait_ns.  This must be called
    // by the pipeline even when one camera has stopped producing frames.
    std::vector<FramePair> tick(uint64_t now_ns);

    // Drop all pending samples and release their CameraFrame leases.
    void clear();

    FramePairerStats stats() const;
    size_t pending(uint32_t camera_id) const;
    void configure(FramePairerConfig config);
    const FramePairerConfig &config() const { return config_; }

private:
    static uint64_t monotonic_now_ns();
    static bool timestamp_less(const FrameSample &a, const FrameSample &b);
    bool compatible_clock_domains(const FrameSample &a, const FrameSample &b) const;
    std::vector<FramePair> drain_ready_locked(uint64_t now_ns);
    std::vector<FramePair> expire_locked(uint64_t now_ns);
    void drop_front_locked(uint32_t camera_id, bool timeout);
    void record_match_locked(const FramePair &pair);

    FramePairerConfig config_;
    mutable std::mutex mutex_;
    std::deque<FrameSample> queues_[2];
    std::optional<FrameSample> last_[2];
    uint64_t last_timestamp_[2] = {0, 0};
    uint64_t last_sequence_[2] = {0, 0};
    bool have_last_timestamp_[2] = {false, false};
    uint64_t next_pair_id_ = 1;
    FramePairerStats stats_;
};

