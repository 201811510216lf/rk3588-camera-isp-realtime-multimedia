#include "media_graph_validator.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
uint32_t number(const std::string &text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned integer");
    size_t used = 0;
    const auto value = std::stoull(text, &used);
    if (used != text.size() || value > UINT32_MAX)
        throw std::out_of_range("integer exceeds uint32 range");
    return static_cast<uint32_t>(value);
}
} // namespace
int main(int argc, char **argv) {
    MediaGraphValidationConfig config;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i], value;
            if (arg == "--help" || arg == "-h") {
                std::cout
                    << "Usage: " << argv[0] << " --config PATH\n"
                    << "or --media-device PATH --camera0 PATH --camera1 PATH --sensor0 ENTITY --sensor1 "
                       "ENTITY --isp0 ENTITY --isp1 ENTITY\n"
                    << "  --graph-file PATH --camera0-info PATH --camera1-info PATH  offline diagnostics\n"
                    << "  --width N --height N --fps N  optional active-format checks\n"
                    << "No links, formats, controls or stream state are modified. Offline results do not "
                       "prove live readiness.\n";
                return 0;
            }
            auto equal = arg.find('=');
            if (equal != std::string::npos) {
                value = arg.substr(equal + 1);
                arg.resize(equal);
            } else {
                if (++i >= argc)
                    throw std::invalid_argument("missing value for " + arg);
                value = argv[i];
            }
            if (value.empty())
                throw std::invalid_argument("empty value for " + arg);
            if (arg == "--config")
                config.isp_config_file = value;
            else if (arg == "--media-device")
                config.media_device = value;
            else if (arg == "--camera0")
                config.camera0_node = value;
            else if (arg == "--camera1")
                config.camera1_node = value;
            else if (arg == "--sensor0")
                config.camera0_sensor = value;
            else if (arg == "--sensor1")
                config.camera1_sensor = value;
            else if (arg == "--isp0")
                config.camera0_isp = value;
            else if (arg == "--isp1")
                config.camera1_isp = value;
            else if (arg == "--graph-file")
                config.graph_file = value;
            else if (arg == "--camera0-info")
                config.camera0_info_file = value;
            else if (arg == "--camera1-info")
                config.camera1_info_file = value;
            else if (arg == "--width")
                config.expected_width = number(value);
            else if (arg == "--height")
                config.expected_height = number(value);
            else if (arg == "--fps")
                config.expected_fps = number(value);
            else
                throw std::invalid_argument("unknown option: " + arg);
        }
    } catch (const std::exception &error) {
        std::cerr << "CONFIG_ERROR: " << error.what() << '\n';
        return 2;
    }
    const auto report = validate_media_graph(config);
    std::cout << report.summary() << '\n'
              << "graph_source=" << report.graph_source << '\n'
              << "camera0_source=" << report.camera0_source << '\n'
              << "camera1_source=" << report.camera1_source << '\n';
    return report.ok() ? 0 : 1;
}
