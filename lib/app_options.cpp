#include "app_options.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

bool take_value(int argc, char **argv, int *index, const std::string &arg,
                std::string *value, std::string *error) {
    const auto equal = arg.find('=');
    if (equal != std::string::npos) *value = arg.substr(equal + 1);
    else if (*index + 1 < argc) *value = argv[++(*index)];
    else { *error = "missing value for " + arg; return false; }
    if (value->empty()) { *error = "missing value for " + arg; return false; }
    return true;
}

bool parse_uint(const std::string &text, uint32_t *value) {
    if (text.empty() || text.front() == '-') return false;
    char *end = nullptr; errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) return false;
    *value = static_cast<uint32_t>(parsed); return true;
}

bool parse_int(const std::string &text, int *value) {
    if (text.empty()) return false;
    char *end = nullptr; errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
    *value = static_cast<int>(parsed); return true;
}

bool parse_bool(const std::string &text, bool *value) {
    if (text == "1" || text == "true" || text == "on" || text == "yes") { *value = true; return true; }
    if (text == "0" || text == "false" || text == "off" || text == "no") { *value = false; return true; }
    return false;
}

bool uint_option(const std::string &arg, const char *name, int *index, int argc,
                 char **argv, uint32_t *target, std::string *error) {
    if (arg != name && arg.rfind(std::string(name) + "=", 0) != 0) return false;
    std::string value;
    if (!take_value(argc, argv, index, arg, &value, error) || !parse_uint(value, target))
        *error = "invalid unsigned integer for " + std::string(name) + ": " + value;
    return true;
}

bool bool_option(const std::string &arg, const char *name, int *index, int argc,
                 char **argv, bool *target, std::string *error) {
    if (arg != name && arg.rfind(std::string(name) + "=", 0) != 0) return false;
    if (arg == name && (*index + 1 >= argc || !argv[*index + 1] || argv[*index + 1][0] == '-')) {
        *target = true; return true;
    }
    std::string value;
    if (!take_value(argc, argv, index, arg, &value, error) || !parse_bool(value, target))
        *error = "invalid boolean for " + std::string(name) + ": " + value;
    return true;
}

} // namespace

void print_app_help(const char *program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "  --source=mipi                retained source selector\n"
              << "  --camera0 PATH --camera1 PATH  independent V4L2 ISP NV12 nodes\n"
              << "  --input-width N --input-height N --fps N\n"
              << "  --output-width N --output-height N  side-by-side output\n"
              << "  --sync-mode software|hardware --sync-tolerance-ms N --sync-max-wait-ms N\n"
              << "  --sync-policy drop-older|hold-last\n"
              << "  --ai-mode off|async|strict --model PATH [--model0 PATH --model1 PATH]\n"
              << "  --npu-core0 N --npu-core1 N (-1=auto, 0/1/2=core) --ai-fps N\n"
              << "  --rtmp-url URL --record --no-record --no-stream --output-dir DIR --segment-sec N\n"
              << "  --output PATH --events PATH --sync-diagnostics PATH --duration-sec N\n"
              << "  --device PATH --width N --height N  legacy aliases for camera0/input size\n"
              << "  --ai-enable[=BOOL] --ai-required BOOL --simulate-ai-ms N --v4l2-timeout-ms N\n"
              << "  --help\n";
}

bool parse_app_options(int argc, char **argv, AppOptions *options, bool *show_help,
                       std::string *error) {
    if (!options || !show_help || !error || argc < 1) return false;
    *options = AppOptions{}; *show_help = false; error->clear();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--help" || arg == "-h") { *show_help = true; continue; }
        std::string value;
        auto string_option = [&](const char *name, std::string *target) -> bool {
            if (arg != name && arg.rfind(std::string(name) + "=", 0) != 0) return false;
            if (!take_value(argc, argv, &i, arg, &value, error)) return true;
            *target = value; return true;
        };
        if (string_option("--device", &options->device)) { options->device0 = options->device; continue; }
        if (string_option("--camera0", &options->device0)) { options->device = options->device0; continue; }
        if (string_option("--camera1", &options->device1) || string_option("--output", &options->output) ||
            string_option("--model", &options->model_path) || string_option("--model0", &options->model0_path) ||
            string_option("--model1", &options->model1_path) || string_option("--events", &options->events_path) ||
            string_option("--rtmp-url", &options->rtmp_url) || string_option("--output-dir", &options->output_dir) ||
            string_option("--sync-diagnostics", &options->sync_diagnostics_path)) {
            if (arg.rfind("--model=", 0) == 0 || arg == "--model") {
                options->model0_path = options->model_path; options->model1_path = options->model_path;
            }
            continue;
        }
        if (arg == "--source" || arg.rfind("--source=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error)) return false;
            if (value != "mipi") { *error = "--source only supports mipi in this build"; return false; }
            continue;
        }
        if (uint_option(arg, "--width", &i, argc, argv, &options->width, error) ||
            uint_option(arg, "--input-width", &i, argc, argv, &options->width, error) ||
            uint_option(arg, "--height", &i, argc, argv, &options->height, error) ||
            uint_option(arg, "--input-height", &i, argc, argv, &options->height, error) ||
            uint_option(arg, "--output-width", &i, argc, argv, &options->output_width, error) ||
            uint_option(arg, "--output-height", &i, argc, argv, &options->output_height, error) ||
            uint_option(arg, "--fps", &i, argc, argv, &options->fps, error) ||
            uint_option(arg, "--bitrate", &i, argc, argv, &options->bitrate, error) ||
            uint_option(arg, "--duration-sec", &i, argc, argv, &options->duration_sec, error) ||
            uint_option(arg, "--ai-fps", &i, argc, argv, &options->ai_fps, error) ||
            uint_option(arg, "--v4l2-timeout-ms", &i, argc, argv, &options->v4l2_timeout_ms, error) ||
            uint_option(arg, "--sync-tolerance-ms", &i, argc, argv, &options->sync_tolerance_ms, error) ||
            uint_option(arg, "--sync-max-wait-ms", &i, argc, argv, &options->sync_max_wait_ms, error) ||
            uint_option(arg, "--segment-sec", &i, argc, argv, &options->segment_sec, error) ||
            uint_option(arg, "--stats-interval-sec", &i, argc, argv, &options->stats_interval_sec, error)) continue;
        if (arg == "--npu-core0" || arg.rfind("--npu-core0=", 0) == 0 || arg == "--npu-core1" || arg.rfind("--npu-core1=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error) || !parse_int(value, arg.find("core0") != std::string::npos ? &options->npu_core0 : &options->npu_core1)) {
                *error = "invalid integer for " + arg; return false;
            }
            continue;
        }
        if (arg == "--simulate-ai-ms" || arg.rfind("--simulate-ai-ms=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error) || !parse_int(value, &options->simulate_ai_ms) || options->simulate_ai_ms < 0) { *error = "--simulate-ai-ms must be non-negative"; return false; }
            continue;
        }
        if (bool_option(arg, "--ai-enable", &i, argc, argv, &options->ai_enable, error)) { options->ai_mode = options->ai_enable ? AppOptions::AiMode::Async : AppOptions::AiMode::Off; continue; }
        if (bool_option(arg, "--ai-required", &i, argc, argv, &options->ai_required, error)) continue;
        if (bool_option(arg, "--record", &i, argc, argv, &options->record, error)) { if (options->record) options->no_record = false; continue; }
        if (arg == "--no-record") { options->no_record = true; options->record = false; continue; }
        if (arg == "--no-stream") { options->no_stream = true; continue; }
        if (arg == "--no-ai") { options->ai_enable = false; options->ai_mode = AppOptions::AiMode::Off; continue; }
        if (arg == "--sync-mode" || arg.rfind("--sync-mode=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error)) return false;
            if (value == "software") options->sync_mode = AppOptions::SyncMode::Software;
            else if (value == "hardware") options->sync_mode = AppOptions::SyncMode::Hardware;
            else { *error = "--sync-mode must be software or hardware"; return false; }
            continue;
        }
        if (arg == "--sync-policy" || arg.rfind("--sync-policy=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error)) return false;
            if (value == "drop-older") options->sync_policy = AppOptions::SyncPolicy::DropOlder;
            else if (value == "hold-last") options->sync_policy = AppOptions::SyncPolicy::HoldLast;
            else { *error = "--sync-policy must be drop-older or hold-last"; return false; }
            continue;
        }
        if (arg == "--ai-mode" || arg.rfind("--ai-mode=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error)) return false;
            if (value == "off") options->ai_mode = AppOptions::AiMode::Off;
            else if (value == "async") options->ai_mode = AppOptions::AiMode::Async;
            else if (value == "strict") options->ai_mode = AppOptions::AiMode::Strict;
            else { *error = "--ai-mode must be off, async or strict"; return false; }
            options->ai_enable = options->ai_mode != AppOptions::AiMode::Off; continue;
        }
        *error = "unknown option: " + arg; return false;
    }
    if (!error->empty()) return false;
    if (options->npu_core0 < -1 || options->npu_core0 > 2 || options->npu_core1 < -1 || options->npu_core1 > 2) {
        *error = "NPU core must be -1 (auto), 0, 1 or 2"; return false;
    }
    if (options->width == 0 || options->height == 0 || options->output_width == 0 || options->output_height == 0 || options->fps == 0 || options->bitrate == 0 || options->v4l2_timeout_ms == 0 || options->sync_max_wait_ms == 0) {
        *error = "input/output dimensions, fps, bitrate, timeout and sync wait must be greater than zero"; return false;
    }
    if (options->device0.empty() || options->device1.empty()) { *error = "--camera0 and --camera1 must be non-empty"; return false; }
    if (options->model0_path.empty()) options->model0_path = options->model_path;
    if (options->model1_path.empty()) options->model1_path = options->model_path;
    if (options->ai_enable && options->ai_fps == 0) { *error = "--ai-fps must be greater than zero when AI is enabled"; return false; }
    if (options->output.size() >= 4 && options->output.substr(options->output.size() - 4) == ".mp4") { *error = "raw H.264 output path cannot use .mp4; use --output-dir for segmented MP4"; return false; }
    return true;
}
