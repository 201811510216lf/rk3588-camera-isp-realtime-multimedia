#pragma once

#include "app_options.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

struct DualCameraPipelineStats {
    uint64_t camera0_frames = 0;
    uint64_t camera1_frames = 0;
    uint64_t camera0_errors = 0;
    uint64_t camera1_errors = 0;
    uint64_t pair_frames = 0;
    uint64_t pair_drops = 0;
    uint64_t encoded_frames = 0;
    uint64_t encode_errors = 0;
    uint64_t ai0_processed = 0;
    uint64_t ai1_processed = 0;
    uint64_t ai0_drops = 0;
    uint64_t ai1_drops = 0;
    uint64_t rtmp_errors = 0;
    uint64_t mp4_errors = 0;
    double pair_fps = 0.0;
    double encode_fps = 0.0;
    double ai0_fps = 0.0;
    double ai1_fps = 0.0;
    double sync_p50_ms = 0.0;
    double sync_p95_ms = 0.0;
    double sync_p99_ms = 0.0;
    double last_delta_ms = 0.0;
    double max_delta_ms = 0.0;
};

struct DualCameraPipelineConfig {
    std::string camera0 = "/dev/video0";
    std::string camera1 = "/dev/video1";
    uint32_t input_width = 1920;
    uint32_t input_height = 1080;
    uint32_t fps = 30;
    uint32_t output_width = 1920;
    uint32_t output_height = 1080;
    uint32_t bitrate = 4000000;
    uint32_t v4l2_timeout_ms = 1000;
    uint32_t duration_sec = 0;
    uint32_t stats_interval_sec = 5;
    uint32_t sync_tolerance_ms = 5;
    uint32_t sync_max_wait_ms = 50;
    AppOptions::SyncMode sync_mode = AppOptions::SyncMode::Software;
    AppOptions::SyncPolicy sync_policy = AppOptions::SyncPolicy::DropOlder;
    AppOptions::AiMode ai_mode = AppOptions::AiMode::Off;
    bool ai_required = false;
    uint32_t ai_fps = 5;
    int simulate_ai_ms = 0;
    std::string model0_path;
    std::string model1_path;
    int npu_core0 = -1;
    int npu_core1 = -1;
    std::string output_path = "camera.h264";
    std::string rtmp_url;
    std::string output_dir = "recordings";
    uint32_t segment_sec = 300;
    std::string events_path;
    std::string sync_diagnostics_path;
    bool no_stream = false;
    bool no_record = false;
    bool record = false;
    std::atomic<bool> *external_stop = nullptr;
};

class DualCameraPipeline {
public:
    explicit DualCameraPipeline(DualCameraPipelineConfig config);
    ~DualCameraPipeline();
    DualCameraPipeline(const DualCameraPipeline &) = delete;
    DualCameraPipeline &operator=(const DualCameraPipeline &) = delete;

    bool run(std::string *error = nullptr);
    void request_stop();
    DualCameraPipelineStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
