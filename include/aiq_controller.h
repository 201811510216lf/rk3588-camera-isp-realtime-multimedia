#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class AiqResult {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Unsupported,
    BackendError,
};

struct AiqControllerConfig {
    uint32_t camera_id = 0;
    std::vector<std::string> iq_roots;
    std::string daemon_hint;
};

struct AiqStatus {
    uint32_t camera_id = 0;
    bool daemon_detected = false;
    bool vendor_runtime_detected = false;
    bool control_backend_available = false;
    std::string owner;
    std::string iq_path;
    std::string iq_match = "UNKNOWN";
    std::string detail;
};

// The controller deliberately does not invent rkAIQ symbols. A board-specific
// backend must be supplied once the BSP headers/runtime are identified. The
// discovery/status path is useful without that backend and all control writes
// fail closed with Unsupported until a real backend is installed.
class AiqController {
public:
    explicit AiqController(AiqControllerConfig config);

    AiqResult query_status(AiqStatus *status) const;
    AiqResult set_ae_auto(bool enabled, std::string *error) const;
    AiqResult set_exposure_us(uint32_t exposure_us, std::string *error) const;
    AiqResult set_gain(double gain, std::string *error) const;
    AiqResult set_awb_auto(bool enabled, std::string *error) const;
    AiqResult dump_state(const std::string &path, std::string *error) const;

    static const char *result_name(AiqResult result);

private:
    AiqControllerConfig config_;
};

