#pragma once

#include "isp/rkaiq_backend.h"
#include <cstdint>
#include <memory>
#include <mutex>
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
    AiqController(isp::AiqCameraConfig config, std::unique_ptr<isp::IRkAiqBackend> backend,
                  isp::OwnershipEvidence ownership);
    ~AiqController();
    AiqController(const AiqController &) = delete;
    AiqController &operator=(const AiqController &) = delete;

    isp::IspControlResult initialize();
    isp::IspControlResult prepare();
    isp::IspControlResult start(bool defer_failure_cleanup = false);
    isp::IspControlResult stop();
    isp::IspControlResult deinitialize();
    isp::IspControlResult shutdown();
    isp::IspControlResult query(isp::IspStatus *status) const;
    isp::IspControlResult read_controls(isp::ControlSnapshot *controls) const;
    isp::IspControlResult set_ae_mode(isp::AeMode mode) const;
    isp::IspControlResult set_exposure_time_us(double value) const;
    isp::IspControlResult set_analog_gain(double value) const;
    isp::IspControlResult set_awb_mode(isp::AwbMode mode) const;
    const isp::AiqCameraConfig &configuration() const { return camera_config_; }

    AiqResult query_status(AiqStatus *status) const;
    AiqResult set_ae_auto(bool enabled, std::string *error) const;
    AiqResult set_exposure_us(uint32_t exposure_us, std::string *error) const;
    AiqResult set_gain(double gain, std::string *error) const;
    AiqResult set_awb_auto(bool enabled, std::string *error) const;
    AiqResult dump_state(const std::string &path, std::string *error) const;

    static const char *result_name(AiqResult result);

  private:
    AiqControllerConfig config_;
    isp::AiqCameraConfig camera_config_;
    std::unique_ptr<isp::IRkAiqBackend> backend_;
    isp::OwnershipEvidence ownership_;
    isp::IqValidation iq_;
    bool initialized_ = false, prepared_ = false, running_ = false;
    mutable std::mutex mutex_;
    int ownership_fd_ = -1;
    int isp_ownership_fd_ = -1;
    isp::BackendInfo backend_info() const noexcept;
    isp::IspControlResult result(isp::ControlStatus status, const std::string &op,
                                 const std::string &message) const;
    isp::IspControlResult decorate(isp::IspControlResult value) const;
    isp::IspControlResult shutdown_locked();
    isp::IspControlResult check_control(const char *operation,
                                        isp::IspControlCapabilities *capabilities) const;
    void release_ownership_lock();
};
