#include "media_graph_validator.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>

int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    const auto fixtures = std::filesystem::u8path(ISP_TEST_FIXTURE_DIR);
    MediaGraphValidationConfig config;
    config.isp_config_file = (fixtures / "isp_offline.yaml").string();
    const auto offline = validate_media_graph(config);
    assert(offline.ok() && offline.sensor_mapping && !offline.live_evidence);
    assert(offline.cameras.size() == 2 && offline.cameras[0].enabled_path.size() == 3);
    assert(offline.cameras[0].output_entity == "fixture output A");
    assert(offline.cameras[0].configured_isp_entity == "fixture ISP A");
    const auto json = offline.json();
    assert(json.find("\"isp_3a_ready\":\"NOT_MEASURED\"") != std::string::npos);
    assert(json.find("\"hardware_confirmed\":false") != std::string::npos);
    assert(json.find("\"evidence\":\"OFFLINE_FILE\"") != std::string::npos);
    assert(json.find("\"enabled_path\":[\"fixture sensor A\",\"fixture ISP A\",\"fixture output A\"]") !=
           std::string::npos);
    MediaGraphValidationReport escaped;
    escaped.error = "test\"error\nwith\\path";
    assert(escaped.json().find("test\\\"error\\u000awith\\\\path") != std::string::npos);
    config = {};
    config.media_device = "/dev/media19";
    config.camera0_node = "/dev/video42";
    config.camera1_node = "/dev/video47";
    config.graph_file = (fixtures / "media_graph.txt").string();
    config.camera0_info_file = (fixtures / "capture_nv12.txt").string();
    config.camera1_info_file = (fixtures / "capture_nv12m.txt").string();
    config.camera0_sensor = "fixture sensor A";
    config.camera1_sensor = "fixture sensor B";
    config.camera0_isp = "fixture ISP A";
    config.camera1_isp = "fixture ISP B";
    config.expected_width = 640;
    config.expected_height = 480;
    config.expected_fps = 30;
    assert(validate_media_graph(config).ok());
    config.camera1_node = config.camera0_node;
    assert(!validate_media_graph(config).ok());
    config.camera1_node = "/dev/video47";
    config.camera0_sensor.clear();
    assert(!validate_media_graph(config).ok());
    config.camera0_sensor = "fixture sensor A";
    config.expected_fps = 25;
    assert(!validate_media_graph(config).ok());
    config.media_device.clear();
    assert(!validate_media_graph(config).ok());
    return 0;
}
