#include "aiq_controller.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool take(int argc, char **argv, int *index, const std::string &arg, const char *name, std::string *value) {
    if (arg != name && arg.rfind(std::string(name) + "=", 0) != 0) return false;
    const size_t equal = arg.find('=');
    if (equal != std::string::npos) *value = arg.substr(equal + 1);
    else if (*index + 1 < argc) *value = argv[++(*index)];
    else *value = {};
    return !value->empty();
}
}

int main(int argc, char **argv) {
    bool list = false;
    bool status = false;
    bool ae_seen = false;
    bool ae_value = false;
    bool awb_seen = false;
    bool awb_value = false;
    std::string exposure;
    std::string gain;
    std::string dump;
    std::string iq_root = "/etc/iqfiles";
    uint32_t camera_id = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--list-cameras") { list = true; continue; }
        if (arg == "--isp-status") { status = true; continue; }
        std::string value;
        if (take(argc, argv, &i, arg, "--camera-id", &value)) { camera_id = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10)); continue; }
        if (take(argc, argv, &i, arg, "--iq-root", &iq_root)) continue;
        if (take(argc, argv, &i, arg, "--dump-isp-state", &dump)) continue;
        if (arg == "--ae-auto") { ae_seen = true; ae_value = true; continue; }
        if (arg == "--awb-auto") { awb_seen = true; awb_value = true; continue; }
        if (take(argc, argv, &i, arg, "--exposure-us", &exposure)) continue;
        if (take(argc, argv, &i, arg, "--gain", &gain)) continue;
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--list-cameras|--isp-status] [control]\n"
                      << "  --camera-id N --iq-root PATH --dump-isp-state PATH\n"
                      << "  --ae-auto --awb-auto --exposure-us N --gain VALUE\n";
            return 0;
        }
        std::cerr << "unknown option: " << arg << "\n";
        return 2;
    }
    AiqControllerConfig config;
    config.camera_id = camera_id;
    config.iq_roots.push_back(iq_root);
    AiqController controller(config);
    if (list || status || (!ae_seen && !awb_seen && exposure.empty() && gain.empty() && dump.empty())) {
        AiqStatus report;
        const AiqResult result = controller.query_status(&report);
        std::cout << "camera_id=" << report.camera_id << "\n"
                  << "daemon=" << (report.daemon_detected ? report.owner : "none") << "\n"
                  << "runtime=" << (report.vendor_runtime_detected ? report.iq_path : "none") << "\n"
                  << "iq_match=" << report.iq_match << "\n"
                  << "controls=" << (report.control_backend_available ? "available" : "unsupported") << "\n"
                  << report.detail << "\n";
        if (result != AiqResult::Ok) return 1;
        if (!ae_seen && !awb_seen && exposure.empty() && gain.empty() && dump.empty()) return 0;
    }
    std::string error;
    AiqResult result = AiqResult::Ok;
    if (ae_seen) result = controller.set_ae_auto(ae_value, &error);
    else if (awb_seen) result = controller.set_awb_auto(awb_value, &error);
    else if (!exposure.empty()) result = controller.set_exposure_us(static_cast<uint32_t>(std::strtoul(exposure.c_str(), nullptr, 10)), &error);
    else if (!gain.empty()) result = controller.set_gain(std::strtod(gain.c_str(), nullptr), &error);
    else if (!dump.empty()) result = controller.dump_state(dump, &error);
    if (result != AiqResult::Ok) {
        std::cerr << AiqController::result_name(result) << ": " << error << "\n";
        return result == AiqResult::Unsupported ? 3 : 1;
    }
    return 0;
}

