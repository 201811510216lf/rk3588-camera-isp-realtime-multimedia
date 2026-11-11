#pragma once

#include <cstdint>
#include <string>

struct AppOptions {
    enum class Source {
        Legacy,
        Mipi,
    };

    Source source = Source::Legacy;
    int legacy_streams = 4;
    std::string device = "/dev/video0";
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t v4l2_timeout_ms = 1000;
    uint32_t duration_sec = 0;
    bool ai_enable = false;
    bool ai_required = false;
    uint32_t ai_fps = 5;
    uint32_t model_index = 0;
    int simulate_ai_ms = 0;
    std::string model_path;
    std::string output = "camera.h264";
    std::string events_path;
};

void print_app_help(const char *program_name);

// Returns true on success. On false, error contains a user-actionable message.
bool parse_app_options(int argc, char **argv, AppOptions *options, bool *show_help,
                      std::string *error);
