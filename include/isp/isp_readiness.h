#pragma once

#include "isp/rkaiq_backend.h"
#include "isp/sensor_registry.h"

#include <array>
#include <functional>
#include <memory>

namespace isp {
struct IspReadiness {
    IspControlResult result;
    bool ready = false;
    bool diagnostic_unverified = false;
};
struct CaptureBinding {
    std::array<std::string, 2> nodes;
    uint32_t width = 0, height = 0, fps = 0;
    std::string pixel_format = "auto";
};
IspControlResult validate_capture_binding(const IspConfig &, const CaptureBinding &);
// Policy evaluation never converts configuration, ownership or known IQ mismatches into readiness.
IspReadiness evaluate_readiness(const AiqCameraConfig &, const IspCameraEvidence &, const IspStatus &,
                                bool allow_unverified);
// Periodic checks reuse the startup confirmation rules; diagnostic capture never promotes readiness.
IspReadiness evaluate_runtime_status(IspCameraId expected_id, const IspStatus &, bool allow_unverified);
IspControlResult invoke_readiness_operation(const char *operation,
                                            const std::function<IspControlResult()> &callback);

class IspReadinessProvider {
  public:
    virtual ~IspReadinessProvider() = default;
    virtual IspControlResult before_stream() = 0;
    virtual IspControlResult after_stream() = 0;
    virtual IspControlResult before_stream_stop() = 0;
    virtual IspControlResult after_stream_stop() = 0;
    virtual IspControlResult shutdown() = 0;
    virtual IspStatus status(IspCameraId) const = 0;
};
IspStatus query_readiness_status(const IspReadinessProvider &, IspCameraId);

// Coordinates discovery and the existing AiqController manager outside Capture workers.
class IspPipelineCoordinator final : public IspReadinessProvider {
  public:
    using BackendFactory =
        std::function<std::unique_ptr<IRkAiqBackend>(const AiqCameraConfig &, const OwnershipEvidence &)>;
    IspPipelineCoordinator(IspConfig config, std::unique_ptr<IMediaGraphInspector> inspector = {},
                           BackendFactory factory = {});
    ~IspPipelineCoordinator() override;
    IspControlResult before_stream() override;
    IspControlResult after_stream() override;
    IspControlResult before_stream_stop() override;
    IspControlResult after_stream_stop() override;
    IspControlResult shutdown() override;
    IspStatus status(IspCameraId) const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace isp
