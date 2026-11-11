#include "app_options.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

bool take_value(int argc, char **argv, int *index, const std::string &arg,
                std::string *value, std::string *error) {
    const auto equal = arg.find('=');
    if (equal != std::string::npos) {
        *value = arg.substr(equal + 1);
        if (value->empty()) {
            *error = "missing value for " + arg.substr(0, equal);
            return false;
        }
        return true;
    }
    if (*index + 1 >= argc) {
        *error = "missing value for " + arg;
        return false;
    }
    *value = argv[++(*index)];
    if (value->empty()) {
        *error = "missing value for " + arg;
        return false;
    }
    return true;
}

bool parse_uint(const std::string &text, uint32_t *value) {
    if (text.empty() || text.front() == '-') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_int(const std::string &text, int *value) {
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool parse_bool(const std::string &text, bool *value) {
    if (text == "1" || text == "true" || text == "on" || text == "yes") {
        *value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "off" || text == "no") {
        *value = false;
        return true;
    }
    return false;
}

bool require_uint(const std::string &arg, const std::string &text, uint32_t *out,
                  std::string *error) {
    if (!parse_uint(text, out)) {
        *error = "invalid unsigned integer for " + arg + ": " + text;
        return false;
    }
    return true;
}

} // namespace

void print_app_help(const char *program_name) {
    std::cout << "Usage: " << program_name << " [legacy_stream_count] [options]\n"
              << "  --source=legacy|mipi       input mode (default: legacy)\n"
              << "  --device PATH              V4L2 ISP NV12 node (mipi)\n"
              << "  --width N --height N       requested capture size\n"
              << "  --fps N                    requested capture/output rate\n"
              << "  --bitrate N                H.264 bitrate in bit/s\n"
              << "  --output PATH              raw H.264 output (mipi)\n"
              << "  --duration-sec N           stop after N seconds; 0 means until Ctrl+C\n"
              << "  --ai-enable[=BOOL]         enable one-model AI branch\n"
              << "  --ai-required BOOL         fail if AI cannot be initialized\n"
              << "  --ai-fps N                 maximum AI submission rate\n"
              << "  --model-index N             legacy model: 0 person, 1 helmet, 2 call/play\n"
              << "  --model PATH               explicit RKNN model path\n"
              << "  --events PATH              JSONL AI event output\n"
              << "  --simulate-ai-ms N         test-only AI delay injection\n"
              << "  --v4l2-timeout-ms N        dequeue poll timeout\n"
              << "  --help                     show this help\n";
}

bool parse_app_options(int argc, char **argv, AppOptions *options, bool *show_help,
                      std::string *error) {
    if (!options || !show_help || !error || argc < 1) {
        return false;
    }
    *options = AppOptions{};
    *show_help = false;
    error->clear();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--help" || arg == "-h") {
            *show_help = true;
            continue;
        }

        // Preserve the old invocation: ./myDemo 4
        if (i == 1 && !arg.empty() && arg.front() != '-') {
            int count = 0;
            if (!parse_int(arg, &count) || count < 1 || count > 6) {
                *error = "legacy stream count must be an integer in [1, 6]";
                return false;
            }
            options->legacy_streams = count;
            continue;
        }

        std::string value;
        if (arg == "--ai-enable") {
            options->ai_enable = true;
            continue;
        }
        if (arg == "--ai-required") {
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-') {
                value = argv[++i];
                if (!parse_bool(value, &options->ai_required)) {
                    *error = "invalid boolean for --ai-required: " + value;
                    return false;
                }
            } else {
                options->ai_required = true;
            }
            continue;
        }
        if (arg.rfind("--ai-enable=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error) ||
                !parse_bool(value, &options->ai_enable)) {
                *error = "invalid boolean for --ai-enable: " + value;
                return false;
            }
            continue;
        }

        auto parse_option_uint = [&](const char *name, uint32_t *target) -> bool {
            if (arg == name || arg.rfind(std::string(name) + "=", 0) == 0) {
                if (!take_value(argc, argv, &i, arg, &value, error)) {
                    return false;
                }
                return require_uint(name, value, target, error);
            }
            return true;
        };

        if (arg == "--source" || arg.rfind("--source=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error)) {
                return false;
            }
            if (value == "legacy") {
                options->source = AppOptions::Source::Legacy;
            } else if (value == "mipi") {
                options->source = AppOptions::Source::Mipi;
            } else {
                *error = "--source must be legacy or mipi";
                return false;
            }
        } else if (arg == "--device" || arg.rfind("--device=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &options->device, error)) return false;
        } else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &options->output, error)) return false;
        } else if (arg == "--model" || arg.rfind("--model=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &options->model_path, error)) return false;
        } else if (arg == "--events" || arg.rfind("--events=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &options->events_path, error)) return false;
        } else if (arg == "--ai-required" || arg.rfind("--ai-required=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error) ||
                !parse_bool(value, &options->ai_required)) {
                *error = "invalid boolean for --ai-required: " + value;
                return false;
            }
        } else if (!parse_option_uint("--width", &options->width) ||
                   !parse_option_uint("--height", &options->height) ||
                   !parse_option_uint("--fps", &options->fps) ||
                   !parse_option_uint("--bitrate", &options->bitrate) ||
                   !parse_option_uint("--duration-sec", &options->duration_sec) ||
                   !parse_option_uint("--ai-fps", &options->ai_fps) ||
                   !parse_option_uint("--model-index", &options->model_index) ||
                   !parse_option_uint("--v4l2-timeout-ms", &options->v4l2_timeout_ms)) {
            return false;
        } else if (arg == "--simulate-ai-ms" || arg.rfind("--simulate-ai-ms=", 0) == 0) {
            if (!take_value(argc, argv, &i, arg, &value, error) ||
                !parse_int(value, &options->simulate_ai_ms) || options->simulate_ai_ms < 0) {
                *error = "--simulate-ai-ms must be a non-negative integer";
                return false;
            }
        } else {
            *error = "unknown option: " + arg;
            return false;
        }
    }

    if (options->width == 0 || options->height == 0 || options->fps == 0 ||
        options->bitrate == 0 || options->v4l2_timeout_ms == 0) {
        *error = "width, height, fps, bitrate and v4l2 timeout must be greater than zero";
        return false;
    }
    if (options->ai_enable && options->ai_fps == 0) {
        *error = "--ai-fps must be greater than zero when AI is enabled";
        return false;
    }
    if (options->model_index > 3) {
        *error = "--model-index must be in [0, 3]";
        return false;
    }
    if (options->source == AppOptions::Source::Mipi &&
        options->output.size() >= 4 &&
        options->output.substr(options->output.size() - 4) == ".mp4") {
        *error = "mipi output is raw H.264; use a .h264 path instead of .mp4";
        return false;
    }
    return true;
}
