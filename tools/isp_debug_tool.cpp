#include "isp/isp_cli.h"
#include "isp/isp_types.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string take(int argc, char **argv, int *index, const std::string &argument) {
    const auto equal = argument.find('=');
    std::string value;
    if (equal != std::string::npos)
        value = argument.substr(equal + 1);
    else if (*index + 1 < argc)
        value = argv[++*index];
    if (value.empty() || value.rfind("--", 0) == 0)
        throw std::invalid_argument("missing value for " + argument);
    return value;
}
void dimension(const std::string &value) {
    if (value.empty() || value.front() == '-')
        throw std::invalid_argument("dimension must be positive uint32");
    for (char c : value)
        if (c < '0' || c > '9')
            throw std::invalid_argument("invalid dimension");
    size_t used = 0;
    const auto number = std::stoull(value, &used);
    if (used != value.size() || number == 0 || number > std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("dimension is out of range");
}
} // namespace
int main(int argc, char **argv) {
    bool json = false, capture = false, quality = false, legacy_payload = false;
    uint32_t camera_id = 0;
    std::vector<std::string> forwarded{argv[0]};
    const std::map<std::string, std::string> aliases{
        {"--status", "--isp-status"},        {"--iq-info", "--isp-status"},
        {"--list", "--list-devices"},        {"--ae-status", "--get-ae-mode"},
        {"--awb-status", "--get-awb-mode"},  {"--camera", "--camera-id"},
        {"--set-exposure", "--exposure-us"}, {"--set-gain", "--gain"}};
    for (int i = 1; i < argc; ++i)
        if (argv[i] && std::string(argv[i]) == "--json")
            json = true;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            const auto equal = argument.find('=');
            const auto key = argument.substr(0, equal);
            if (argument == "--json")
                json = true;
            if (argument == "--help" || argument == "-h") {
                std::cout
                    << "isp_debug_tool uses the shared ISP control dispatcher. Legacy aliases: --status, "
                       "--iq-info, --ae-status, --awb-status, --camera, --set-exposure, --set-gain.\n"
                    << "Capture/quality operations remain explicit UNSUPPORTED; use the dedicated "
                       "capture/analyzer scripts.\n";
                std::vector<char *> help{argv[0], const_cast<char *>("--help")};
                return run_isp_control_cli(static_cast<int>(help.size()), help.data());
            }
            if (argument == "--capture-nv12") {
                capture = true;
                continue;
            }
            if (argument == "--quality-report") {
                quality = true;
                continue;
            }
            if (key == "--device" || key == "--input" || key == "--output" || key == "--width" ||
                key == "--height") {
                const auto value = take(argc, argv, &i, argument);
                if (key == "--width" || key == "--height")
                    dimension(value);
                legacy_payload = true;
                continue;
            }
            auto found = aliases.find(key);
            const auto translated = found == aliases.end() ? key : found->second;
            if (translated == "--camera-id") {
                const auto value = take(argc, argv, &i, argument);
                if (value != "0" && value != "1")
                    throw std::invalid_argument("camera-id must be logical 0 or 1");
                camera_id = static_cast<uint32_t>(value[0] - '0');
                forwarded.push_back("--camera-id=" + value);
                continue;
            }
            forwarded.push_back(translated + (equal == std::string::npos ? "" : argument.substr(equal)));
        }
        if (capture || quality) {
            if (capture && quality)
                throw std::invalid_argument("select one operation");
            for (const auto &argument : forwarded) {
                const auto key = argument.substr(0, argument.find('='));
                if (key == "--gain" || key == "--exposure-us" || key == "--ae-auto" || key == "--ae-manual" ||
                    key == "--awb-auto" || key == "--awb-manual")
                    throw std::invalid_argument("capture/quality cannot be combined with control writes");
            }
            isp::IspControlResult result;
            result.camera_id = camera_id;
            result.status = isp::ControlStatus::Unsupported;
            result.operation = capture ? "capture_nv12" : "quality_report";
            result.backend_name = "isp-debug-tool";
            result.message = capture ? "Use scripts/isp_capture_experiment.sh with a verified ISP output node"
                                     : "Use scripts/analyze_isp_samples.py with explicit recorded layout";
            if (json)
                std::cout << isp::result_json(result) << '\n';
            else
                std::cerr << "UNSUPPORTED: " << result.message << '\n';
            return 3;
        }
        if (legacy_payload)
            throw std::invalid_argument("image-layout arguments require --capture-nv12 or --quality-report");
        std::vector<char *> arguments;
        for (auto &argument : forwarded)
            arguments.push_back(argument.data());
        return run_isp_control_cli(static_cast<int>(arguments.size()), arguments.data());
    } catch (const std::exception &error) {
        if (json) {
            isp::IspControlResult result;
            result.status = isp::ControlStatus::InvalidArgument;
            result.camera_id = camera_id;
            result.operation = "parse_debug_cli";
            result.backend_name = "isp-debug-tool";
            result.message = error.what();
            std::cout << isp::result_json(result) << '\n';
        } else
            std::cerr << "INVALID_ARGUMENT: " << error.what() << '\n';
        return 2;
    }
}
