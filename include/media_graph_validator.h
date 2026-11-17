#pragma once

#include "isp/sensor_registry.h"

#include <cstdint>
#include <string>

struct MediaGraphValidationConfig {
    std::string isp_config_file;
    std::string media_device;
    std::string camera0_node;
    std::string camera1_node;
    std::string graph_file;
    std::string camera0_info_file;
    std::string camera1_info_file;
    std::string camera0_sensor;
    std::string camera1_sensor;
    std::string camera0_isp;
    std::string camera1_isp;
    uint32_t expected_width = 0;
    uint32_t expected_height = 0;
    uint32_t expected_fps = 0;
};

struct MediaGraphValidationReport {
    bool graph_available = false;
    bool camera0_available = false;
    bool camera1_available = false;
    bool independent_nodes = false;
    bool nv12_output = false;
    bool sensor_mapping = false;
    bool live_evidence = false;
    bool dimensions_match = true;
    bool fps_match = true;
    std::string graph_source;
    std::string camera0_source;
    std::string camera1_source;
    std::string error;
    std::vector<isp::IspCameraEvidence> cameras;

    bool ok() const {
        return graph_available && camera0_available && camera1_available && independent_nodes &&
               nv12_output && sensor_mapping && dimensions_match && fps_match;
    }
    std::string summary() const;
    std::string json() const;
};

// Executes only read-only media-ctl/v4l2-ctl queries when no capture files are
// supplied. Missing tools or malformed output are reported as failures; this
// function never changes formats, links, controls, or stream state.
MediaGraphValidationReport validate_media_graph(const MediaGraphValidationConfig &config);
