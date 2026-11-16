#include "aiq_controller.h"

#include <filesystem>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#ifdef __linux__
#include <dirent.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

bool contains_ci(const std::string &value, const std::string &needle) {
    if (needle.empty()) return false;
    std::string lhs = value;
    std::string rhs = needle;
    for (char &c : lhs) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char &c : rhs) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lhs.find(rhs) != std::string::npos;
}

std::string find_runtime(const std::vector<std::string> &roots) {
    for (const std::string &root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (contains_ci(name, "rkaiq") || contains_ci(name, "rkisp")) return it->path().string();
        }
    }
    return {};
}

std::string find_daemon(const std::string &hint) {
#ifdef __linux__
    if (!hint.empty()) {
        std::ifstream input("/proc/1/cmdline");
        (void)input;
    }
    DIR *directory = ::opendir("/proc");
    if (!directory) return {};
    while (dirent *entry = ::readdir(directory)) {
        if (entry->d_type != DT_DIR) continue;
        char *end = nullptr;
        (void)std::strtol(entry->d_name, &end, 10);
        if (!end || *end != '\0') continue;
        const std::string base = std::string("/proc/") + entry->d_name;
        std::ifstream comm(base + "/comm");
        std::string name;
        std::getline(comm, name);
        if (contains_ci(name, "rkaiq") || contains_ci(name, "rkisp") || contains_ci(name, "ispserver") ||
            (!hint.empty() && contains_ci(name, hint))) {
            ::closedir(directory);
            return name;
        }
    }
    ::closedir(directory);
#else
    (void)hint;
#endif
    return {};
}

AiqResult unsupported(const char *operation, std::string *error) {
    if (error) {
        *error = std::string(operation) + " is unavailable: no BSP-specific rkAIQ backend was identified; refusing to write controls";
    }
    return AiqResult::Unsupported;
}

} // namespace

AiqController::AiqController(AiqControllerConfig config) : config_(std::move(config)) {}

AiqResult AiqController::query_status(AiqStatus *status) const {
    if (!status) return AiqResult::InvalidArgument;
    *status = AiqStatus{};
    status->camera_id = config_.camera_id;
    status->owner = find_daemon(config_.daemon_hint);
    status->daemon_detected = !status->owner.empty();
    status->iq_path = find_runtime(config_.iq_roots);
    status->vendor_runtime_detected = !status->iq_path.empty();
    status->control_backend_available = false;
    status->iq_match = status->vendor_runtime_detected ? "UNVERIFIED" : "NOT_FOUND";
    std::ostringstream detail;
    detail << "camera_id=" << config_.camera_id
           << " daemon=" << (status->daemon_detected ? status->owner : "none")
           << " runtime=" << (status->vendor_runtime_detected ? status->iq_path : "none")
           << " controls=unsupported-until-bsp-backend-is-identified";
    status->detail = detail.str();
    return AiqResult::Ok;
}

AiqResult AiqController::set_ae_auto(bool, std::string *error) const {
    return unsupported("AE auto control", error);
}

AiqResult AiqController::set_exposure_us(uint32_t exposure_us, std::string *error) const {
    if (exposure_us == 0) {
        if (error) *error = "exposure_us must be greater than zero";
        return AiqResult::InvalidArgument;
    }
    return unsupported("exposure control", error);
}

AiqResult AiqController::set_gain(double gain, std::string *error) const {
    if (!(gain > 0.0)) {
        if (error) *error = "gain must be greater than zero";
        return AiqResult::InvalidArgument;
    }
    return unsupported("gain control", error);
}

AiqResult AiqController::set_awb_auto(bool, std::string *error) const {
    return unsupported("AWB auto control", error);
}

AiqResult AiqController::dump_state(const std::string &path, std::string *error) const {
    if (path.empty()) {
        if (error) *error = "dump path is empty";
        return AiqResult::InvalidArgument;
    }
    AiqStatus status;
    if (query_status(&status) != AiqResult::Ok) {
        if (error) *error = "status discovery failed";
        return AiqResult::BackendError;
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        if (error) *error = "cannot open state dump: " + path;
        return AiqResult::BackendError;
    }
    output << "camera_id=" << status.camera_id << '\n'
           << "daemon_detected=" << (status.daemon_detected ? "true" : "false") << '\n'
           << "owner=" << status.owner << '\n'
           << "vendor_runtime_detected=" << (status.vendor_runtime_detected ? "true" : "false") << '\n'
           << "runtime_or_iq_path=" << status.iq_path << '\n'
           << "iq_match=" << status.iq_match << '\n'
           << "control_backend_available=false\n"
           << "detail=" << status.detail << '\n';
    if (!output.good()) {
        if (error) *error = "state dump write failed: " + path;
        return AiqResult::BackendError;
    }
    return AiqResult::Ok;
}

const char *AiqController::result_name(AiqResult result) {
    switch (result) {
        case AiqResult::Ok: return "ok";
        case AiqResult::InvalidArgument: return "invalid_argument";
        case AiqResult::NotFound: return "not_found";
        case AiqResult::Unsupported: return "unsupported";
        case AiqResult::BackendError: return "backend_error";
    }
    return "unknown";
}

