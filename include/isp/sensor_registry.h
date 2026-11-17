#pragma once

#include "isp/isp_config.h"
#include "isp/media_graph_inspector.h"

namespace isp {
struct SensorEntity {
    SensorIdentity identity;
    std::string subdev;
};
struct IspCameraEvidence {
    IspCameraId camera_id = 0;
    SensorEntity sensor;
    IspDeviceInfo graph;
    IspOutputNode output;
    bool mapping_valid = false;
    bool output_valid = false;
    bool live_evidence = false;
    std::vector<std::string> enabled_path;
    std::string configured_isp_entity;
    std::string output_entity;
    std::vector<MediaRoleHint> output_role_hints;
    std::string isp_generation = "UNKNOWN";
    std::vector<std::string> errors;
    bool valid() const { return mapping_valid && output_valid; }
};
class ISensorRegistry {
  public:
    virtual ~ISensorRegistry() = default;
    virtual IspCameraEvidence resolve(const AiqCameraConfig &, const IspDeviceInfo &,
                                      const IspOutputNode &) const = 0;
};
class SensorRegistry final : public ISensorRegistry {
  public:
    IspCameraEvidence resolve(const AiqCameraConfig &, const IspDeviceInfo &,
                              const IspOutputNode &) const override;
};
IspCameraEvidence inspect_isp_camera(const AiqCameraConfig &, IMediaGraphInspector &);
} // namespace isp
