#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <utility>

#include "app_options.h"

#ifdef __linux__
#include "dual_camera_pipeline.h"
#endif

namespace {
std::atomic<bool> g_stop{false};

void signal_handler(int) { g_stop.store(true); }
} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef __linux__
    // RTMP/FFmpeg sinks are subprocess pipes. A disconnected sink must be
    // reported as a write failure instead of terminating the capture process
    // asynchronously via SIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    AppOptions options;
    bool show_help = false;
    std::string parse_error;
    if (!parse_app_options(argc, argv, &options, &show_help, &parse_error)) {
        std::cerr << "Argument error: " << parse_error << "\n";
        print_app_help(argv[0]);
        return 2;
    }
    if (show_help) {
        print_app_help(argv[0]);
        return 0;
    }

#ifdef __linux__
    DualCameraPipelineConfig pipeline_config;
    pipeline_config.camera0 = options.device0;
    pipeline_config.camera1 = options.device1;
    pipeline_config.input_width = options.width;
    pipeline_config.input_height = options.height;
    pipeline_config.output_width = options.output_width;
    pipeline_config.output_height = options.output_height;
    pipeline_config.fps = options.fps;
    pipeline_config.capture_buffer_count = options.capture_buffer_count;
    pipeline_config.pixel_format = options.pixel_format;
    pipeline_config.video_mode = options.video_mode;
    pipeline_config.model_contract_path = options.model_contract_path;
    pipeline_config.v4l2_timeout_ms = options.v4l2_timeout_ms;
    pipeline_config.bitrate = options.bitrate;
    pipeline_config.output_path = options.output;
    pipeline_config.output_dir = options.output_dir;
    pipeline_config.segment_sec = options.segment_sec;
    pipeline_config.minimum_free_bytes = static_cast<uint64_t>(options.minimum_free_mb) * 1024ULL * 1024ULL;
    pipeline_config.rtmp_url = options.rtmp_url;
    pipeline_config.events_path = options.events_path;
    pipeline_config.sync_diagnostics_path = options.sync_diagnostics_path;
    pipeline_config.isp_config_path = options.isp_config_path;
    pipeline_config.strict_isp_ready = options.strict_isp_ready;
    pipeline_config.allow_unverified_isp = options.allow_unverified_isp;
    pipeline_config.isp_policy_set = options.isp_policy_set;
    pipeline_config.sync_mode = options.sync_mode;
    pipeline_config.sync_policy = options.sync_policy;
    pipeline_config.sync_tolerance_ms = options.sync_tolerance_ms;
    pipeline_config.sync_max_wait_ms = options.sync_max_wait_ms;
    pipeline_config.ai_mode = options.ai_mode;
    pipeline_config.ai_required = options.ai_required;
    pipeline_config.ai_fps = options.ai_fps;
    pipeline_config.simulate_ai_ms = options.simulate_ai_ms;
    pipeline_config.model0_path = options.model0_path;
    pipeline_config.model1_path = options.model1_path;
    pipeline_config.npu_core0 = options.npu_core0;
    pipeline_config.npu_core1 = options.npu_core1;
    pipeline_config.no_stream = options.no_stream;
    pipeline_config.no_record = options.no_record;
    pipeline_config.record = options.record;
    pipeline_config.duration_sec = options.duration_sec;
    pipeline_config.stats_interval_sec = options.stats_interval_sec;
    pipeline_config.external_stop = &g_stop;

    DualCameraPipeline pipeline(std::move(pipeline_config));
    std::string pipeline_error;
    const bool success = pipeline.run(&pipeline_error);
    const DualCameraPipelineStats stats = pipeline.stats();
    std::cout << "dual-camera stats: cam0=" << stats.camera0_frames << " cam1=" << stats.camera1_frames
              << " pairs=" << stats.pair_frames << " pair_drops=" << stats.pair_drops
              << " encode=" << stats.encoded_frames << " sync_p50_ms=" << stats.sync_p50_ms
              << " sync_p95_ms=" << stats.sync_p95_ms << " sync_p99_ms=" << stats.sync_p99_ms
              << " sync_max_delta_ms=" << stats.max_delta_ms << " ai0=" << stats.ai0_processed
              << " ai1=" << stats.ai1_processed << " encode_fps=" << stats.encode_fps << '\n';
    if (!success) {
        std::cerr << "dual-camera pipeline failed: " << pipeline_error << '\n';
        return 1;
    }
    return 0;
#else
    std::cerr << "dual-camera MIPI pipeline requires a Linux RK3588 target with V4L2/MPP/RGA/RKNN support\n";
    return 2;
#endif
}
