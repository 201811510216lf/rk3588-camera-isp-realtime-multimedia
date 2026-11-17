#pragma once

#include "isp/isp_config.h"
#include <memory>

namespace isp {
// Every SDK call belongs in one version-reviewed adapter. No vendor API signatures are guessed here.
class IRkAiqBackend {
  public:
    virtual ~IRkAiqBackend() = default;
    virtual BackendInfo info() const = 0;
    virtual IspControlResult initialize(const AiqCameraConfig &config) = 0;
    virtual IspControlResult prepare() = 0;
    virtual IspControlResult start() = 0;
    virtual IspControlResult stop() = 0;
    virtual IspControlResult deinitialize() = 0;
    virtual IspControlResult query_capabilities(IspControlCapabilities *capabilities) const = 0;
    virtual IspControlResult query_status(IspStatus *status) const = 0;
    virtual IspControlResult read_controls(ControlSnapshot *controls) const = 0;
    virtual IspControlResult validate_iq(const AiqCameraConfig &config, IqValidation *validation) const = 0;
    virtual IspControlResult set_ae_mode(AeMode mode) = 0;
    virtual IspControlResult set_exposure_us(double value) = 0;
    virtual IspControlResult set_gain(double value) = 0;
    virtual IspControlResult set_awb_mode(AwbMode mode) = 0;
};

OwnershipEvidence discover_aiq_ownership(const std::string &daemon_hint = {});
IspControlResult validate_iq_file(const AiqCameraConfig &config, IqValidation *validation);
std::unique_ptr<IRkAiqBackend> create_external_service_backend(const AiqCameraConfig &config,
                                                               OwnershipEvidence ownership);
std::unique_ptr<IRkAiqBackend> create_rockchip_backend();
std::unique_ptr<IRkAiqBackend> create_unavailable_backend();
// Implemented only by RKAIQ_ADAPTER_SOURCE compiled against the selected real BSP SDK.
std::unique_ptr<IRkAiqBackend> create_sdk_backend();
} // namespace isp
