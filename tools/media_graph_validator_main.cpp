#include "media_graph_validator.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
bool value(int argc, char **argv, int *index, const std::string &arg, std::string *out) {
    const size_t equal = arg.find('=');
    if (equal != std::string::npos) {
        *out = arg.substr(equal + 1);
        return !out->empty();
    }
    if (*index + 1 >= argc) return false;
    *out = argv[++(*index)];
    return !out->empty();
}
}

int main(int argc, char **argv) {
    MediaGraphValidationConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        std::string v;
        auto take = [&](const char *name, std::string *target) {
            if (arg == name || arg.rfind(std::string(name) + "=", 0) == 0) {
                if (!value(argc, argv, &i, arg, &v)) return false;
                *target = v;
                return true;
            }
            return false;
        };
        if (take("--media-device", &config.media_device) ||
            take("--camera0", &config.camera0_node) || take("--camera1", &config.camera1_node) ||
            take("--graph-file", &config.graph_file) || take("--camera0-info", &config.camera0_info_file) ||
            take("--camera1-info", &config.camera1_info_file)) {
            continue;
        }
        if (take("--width", &v)) { config.expected_width = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10)); continue; }
        if (take("--height", &v)) { config.expected_height = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10)); continue; }
        if (take("--fps", &v)) { config.expected_fps = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10)); continue; }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " --camera0 /dev/videoX --camera1 /dev/videoY [options]\n"
                      << "  --media-device PATH  media node (default /dev/media0)\n"
                      << "  --graph-file PATH    use captured media-ctl -p output (read only)\n"
                      << "  --camera0-info PATH  use captured v4l2-ctl output\n"
                      << "  --camera1-info PATH  use captured v4l2-ctl output\n"
                      << "  --width N --height N --fps N  optional exact checks\n";
            return 0;
        }
        std::cerr << "unknown option: " << arg << "\n";
        return 2;
    }
    if (config.camera0_node.empty() || config.camera1_node.empty()) {
        std::cerr << "--camera0 and --camera1 are required\n";
        return 2;
    }
    const MediaGraphValidationReport report = validate_media_graph(config);
    std::cout << report.summary() << "\n";
    std::cout << "graph_source=" << report.graph_source << "\n"
              << "camera0_source=" << report.camera0_source << "\n"
              << "camera1_source=" << report.camera1_source << "\n";
    return report.ok() ? 0 : 1;
}

