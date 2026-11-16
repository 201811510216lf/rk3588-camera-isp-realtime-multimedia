#pragma once

#include <cstdint>
#include <string>

struct AppOptions {
    enum class Source { Mipi };
    enum class SyncMode { Software, Hardware };
    enum class SyncPolicy { DropOlder, HoldLast };
    enum class AiMode { Off, Async, Strict };

    Source source = Source::Mipi;
    // `device`/width/height are retained as compatibility aliases for the
    // original single-camera CLI. The dual-camera fields are authoritative.
    std::string device = "/dev/video0";
    std::string device0 = "/dev/video0";
    std::string device1 = "/dev/video1";
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t output_width = 1920;
    uint32_t output_height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t v4l2_timeout_ms = 1000;
    uint32_t duration_sec = 0;
    bool ai_enable = false;
    bool ai_required = false;
    AiMode ai_mode = AiMode::Off;
    uint32_t ai_fps = 5;
    int simulate_ai_ms = 0;
    std::string model_path;
    std::string model0_path;
    std::string model1_path;
    int npu_core0 = -1;
    int npu_core1 = -1;
    SyncMode sync_mode = SyncMode::Software;
    SyncPolicy sync_policy = SyncPolicy::DropOlder;
    uint32_t sync_tolerance_ms = 5;
    uint32_t sync_max_wait_ms = 50;
    std::string rtmp_url;
    bool no_stream = false;
    bool no_record = false;
    bool record = false;
    std::string output_dir = "recordings";
    uint32_t segment_sec = 300;
    uint32_t stats_interval_sec = 5;
    std::string sync_diagnostics_path;
    std::string output = "camera.h264";
    std::string events_path;
};

void print_app_help(const char *program_name);

// Returns true on success. On false, error contains a user-actionable message.
bool parse_app_options(int argc, char **argv, AppOptions *options, bool *show_help,
                      std::string *error);
