#pragma once

#include "isp/isp_types.h"

namespace isp {
struct AiqCameraConfig {
    IspCameraId camera_id = 0;
    std::string media_device;
    std::string sensor_entity;
    std::string expected_sensor_name;
    std::string expected_isp_entity;
    std::string video_node;
    std::string pixel_format = "NV12";
    uint32_t width = 0, height = 0, fps = 0;
    IspWorkingMode working_mode = IspWorkingMode::Normal;
    IspAiqOwnershipMode ownership = IspAiqOwnershipMode::ExternalService;
    std::string iq_root;
    std::string iq_file;
    // Saved diagnostics are offline evidence, never a substitute for live readiness.
    std::string graph_file;
    std::string capture_info_file;
    std::string isp_instance;
    bool independent_context_confirmed = false;
    AiqStartOrder start_order = AiqStartOrder::Unspecified;
    AiqStopOrder stop_order = AiqStopOrder::Unspecified;
};
struct IspConfig {
    std::vector<AiqCameraConfig> cameras;
    bool strict_isp_ready = true;
    bool allow_unverified_isp = false;
};
// Implemented by the configuration/discovery module.
IspControlResult validate_config(const IspConfig &config);
IspControlResult load_isp_config(const std::string &path, IspConfig *config);
} // namespace isp
