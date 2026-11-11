#pragma once

#include "v4l2_isp_camera.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct MipiPipelineConfig {
    V4L2IspCamera::Config camera;
    std::string output_path = "camera.h264";
    std::string events_path;
    std::string model_path;
    uint32_t model_index = 0;
    uint32_t bitrate = 4000000;
    uint32_t duration_sec = 0;
    bool ai_enable = false;
    bool ai_required = false;
    uint32_t ai_fps = 5;
    int simulate_ai_ms = 0;
    std::atomic<bool> *external_stop = nullptr;
};

struct MipiPipelineStats {
    uint64_t capture_frames = 0;
    uint64_t encoded_frames = 0;
    uint64_t capture_drop = 0;
    uint64_t encode_drop = 0;
    uint64_t ai_processed = 0;
    uint64_t ai_skip = 0;
    uint64_t ai_drop_oldest = 0;
    uint64_t capture_errors = 0;
    uint64_t encode_errors = 0;
    uint64_t ai_errors = 0;
    double capture_fps = 0.0;
    double encode_fps = 0.0;
    double ai_fps = 0.0;
    double last_encode_latency_ms = 0.0;
    double last_ai_infer_ms = 0.0;
    size_t v4l2_inflight = 0;
    size_t ai_queue_depth = 0;
};

class MipiPipeline {
public:
    explicit MipiPipeline(MipiPipelineConfig config);
    ~MipiPipeline();

    MipiPipeline(const MipiPipeline &) = delete;
    MipiPipeline &operator=(const MipiPipeline &) = delete;

    // Runs until duration, Ctrl+C/request_stop, or a non-recoverable capture
    // or recorder error. Returns false only when setup or recorder operation
    // failed; a missing optional AI model is handled by the configured fallback.
    bool run(std::string *error = nullptr);
    void request_stop();
    MipiPipelineStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
