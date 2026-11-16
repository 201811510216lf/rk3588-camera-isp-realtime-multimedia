#include "aiq_controller.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void usage(const char *name) {
    std::cout << "Usage: " << name << " [--list|--media-graph] [--camera N] <operation>\n"
              << "  --status | --iq-info | --ae-status | --awb-status\n"
              << "  --set-exposure US | --set-gain VALUE\n"
              << "  --capture-nv12 [--device PATH --width N --height N --output PATH]\n"
              << "  --quality-report [--input PATH --width N --height N]\n"
              << "  --iq-root PATH\n";
}

bool take_value(int argc, char **argv, int *index, const std::string &arg,
                const char *name, std::string *value) {
    if (arg != name && arg.rfind(std::string(name) + "=", 0) != 0) return false;
    const size_t equal = arg.find('=');
    if (equal != std::string::npos) *value = arg.substr(equal + 1);
    else if (*index + 1 < argc) *value = argv[++(*index)];
    else *value = {};
    return !value->empty();
}

void list_nodes() {
    bool found = false;
    for (const char *root : {"/dev", "."}) {
        std::error_code error;
        if (!fs::is_directory(root, error)) continue;
        for (const auto &entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, error)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("video", 0) == 0 || name.rfind("media", 0) == 0) {
                std::cout << entry.path().string() << '\n';
                found = true;
            }
        }
        if (found) break;
    }
    if (!found) std::cout << "no camera media nodes found (NOT_TESTED)\n";
}

int unsupported(const std::string &what) {
    std::cerr << "Unsupported: " << what
              << "; no board-specific rkAIQ/V4L2 backend is installed\n";
    return 3;
}

} // namespace

int main(int argc, char **argv) {
    bool list = false;
    bool media_graph = false;
    bool status = false;
    bool iq_info = false;
    bool ae_status = false;
    bool awb_status = false;
    bool capture = false;
    bool quality = false;
    std::string exposure;
    std::string gain;
    std::string iq_root = "/etc/iqfiles";
    std::string input;
    std::string device;
    std::string output;
    uint32_t camera = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        if (arg == "--list") { list = true; continue; }
        if (arg == "--media-graph") { media_graph = true; continue; }
        if (arg == "--status") { status = true; continue; }
        if (arg == "--iq-info") { iq_info = true; continue; }
        if (arg == "--ae-status") { ae_status = true; continue; }
        if (arg == "--awb-status") { awb_status = true; continue; }
        if (arg == "--capture-nv12") { capture = true; continue; }
        if (arg == "--quality-report") { quality = true; continue; }
        std::string value;
        if (take_value(argc, argv, &i, arg, "--camera", &value)) { camera = static_cast<uint32_t>(std::stoul(value)); continue; }
        if (take_value(argc, argv, &i, arg, "--iq-root", &iq_root)) continue;
        if (take_value(argc, argv, &i, arg, "--input", &input)) continue;
        if (take_value(argc, argv, &i, arg, "--device", &device)) continue;
        if (take_value(argc, argv, &i, arg, "--output", &output)) continue;
        if (take_value(argc, argv, &i, arg, "--width", &value)) { width = static_cast<uint32_t>(std::stoul(value)); continue; }
        if (take_value(argc, argv, &i, arg, "--height", &value)) { height = static_cast<uint32_t>(std::stoul(value)); continue; }
        if (take_value(argc, argv, &i, arg, "--set-exposure", &exposure)) continue;
        if (take_value(argc, argv, &i, arg, "--set-gain", &gain)) continue;
        std::cerr << "unknown option: " << arg << '\n';
        return 2;
    }
    if (list) { list_nodes(); return 0; }
    if (media_graph) {
        std::cout << "media graph inspection is read-only; run scripts/isp_probe.sh or media-ctl -p on the board\n";
        return unsupported("media graph query without a board media-ctl session");
    }
    if (capture) return unsupported("NV12 capture until the target V4L2 node and format contract are identified");
    if (quality) return input.empty() ? unsupported("quality report requires a captured RAW/NV12 input")
                                      : unsupported("quality report is provided by scripts/isp_quality_analyzer.py");

    AiqControllerConfig config;
    config.camera_id = camera;
    config.iq_roots.push_back(iq_root);
    AiqController controller(config);
    if (status || iq_info || ae_status || awb_status ||
        (!exposure.empty() && exposure != "") || !gain.empty()) {
        AiqStatus report;
        const AiqResult result = controller.query_status(&report);
        if (status || iq_info || ae_status || awb_status) {
            std::cout << "camera=" << report.camera_id << '\n'
                      << "daemon=" << (report.daemon_detected ? report.owner : "none") << '\n'
                      << "runtime=" << (report.vendor_runtime_detected ? report.iq_path : "none") << '\n'
                      << "iq_match=" << report.iq_match << '\n'
                      << "controls=" << (report.control_backend_available ? "available" : "unsupported") << '\n'
                      << report.detail << '\n';
            if (result != AiqResult::Ok) return 1;
            if (ae_status || awb_status) return unsupported("AE/AWB status readback without a BSP rkAIQ backend");
            if (status || iq_info) return 0;
        }
        if (!exposure.empty()) {
            std::string error;
            const auto rc = controller.set_exposure_us(static_cast<uint32_t>(std::stoul(exposure)), &error);
            if (rc != AiqResult::Ok) return unsupported(error);
        }
        if (!gain.empty()) {
            std::string error;
            const auto rc = controller.set_gain(std::stod(gain), &error);
            if (rc != AiqResult::Ok) return unsupported(error);
        }
        return 0;
    }
    usage(argv[0]);
    return 2;
}
