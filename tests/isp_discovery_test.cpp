#include "isp/isp_config.h"
#include "isp/rkisp_capability.h"
#include "isp/sensor_registry.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace {
std::string read(const fs::path &path) {
    std::ifstream input(path);
    std::ostringstream out;
    out << input.rdbuf();
    assert(input.good() || input.eof());
    return out.str();
}
std::string replace(std::string text, const std::string &from, const std::string &to) {
    const auto at = text.find(from);
    assert(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}
void all_replace(std::string *text, const std::string &from, const std::string &to) {
    size_t at = 0;
    while ((at = text->find(from, at)) != std::string::npos) {
        text->replace(at, from.size(), to);
        at += to.size();
    }
}
bool has_role(const isp::MediaEntityInfo &entity, isp::MediaEntityRole role, const char *source) {
    return std::any_of(entity.role_hints.begin(), entity.role_hints.end(),
                       [&](const auto &hint) { return hint.role == role && hint.source == source; });
}
} // namespace
int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    const auto fixtures = fs::u8path(ISP_TEST_FIXTURE_DIR);
    isp::IspConfig config;
    assert(isp::load_isp_config((fixtures / "isp_offline.yaml").string(), &config).ok());
    assert(config.cameras.size() == 2 && config.strict_isp_ready && !config.allow_unverified_isp);
    assert(fs::path(config.cameras[0].graph_file).is_absolute());
    const auto graph_text = read(fixtures / "media_graph.txt");
    const auto single_text = read(fixtures / "capture_nv12.txt");
    const auto multi_text = read(fixtures / "capture_nv12m.txt");
    isp::IspDeviceInfo graph;
    isp::IspOutputNode single, multi;
    std::string error;
    assert(isp::parse_media_graph(graph_text, "/dev/media19", &graph, &error));
    assert(graph.entities.size() == 6 && graph.links.size() == 4);
    assert(graph.driver == "rkisp");
    assert(graph.entities[0].pads[0].bus_format == "SRGGB10_1X10");
    assert(graph.evidence == isp::EvidenceSource::NotMeasured);
    assert(isp::reported_isp_generation(graph) == "UNKNOWN");
    auto version_reported = graph;
    version_reported.driver = "rkisp30";
    assert(isp::reported_isp_generation(version_reported) == "RKISP30_REPORTED");
    assert(isp::parse_capture_info(single_text, "/dev/video42", &single, &error));
    assert(isp::parse_capture_info(multi_text, "/dev/video47", &multi, &error));
    assert(single.plane_count == 1 && single.planes[0].stride == 672 && single.planes[0].sizeimage == 483840);
    assert(multi.active_format == "NV12M" && multi.multiplanar && multi.plane_count == 2);
    assert(isp::validate_isp_output(config.cameras[0], single).ok());
    assert(isp::validate_isp_output(config.cameras[1], multi).ok());
    isp::IspDeviceInfo roles_graph;
    assert(isp::parse_media_graph(read(fixtures / "media_graph_roles.txt"), "/dev/media55", &roles_graph,
                                  &error));
    assert(roles_graph.entities.size() == 6 && roles_graph.links.size() == 5);
    assert(has_role(roles_graph.entities[0], isp::MediaEntityRole::Sensor, "REPORTED_FUNCTION"));
    assert(has_role(roles_graph.entities[1], isp::MediaEntityRole::MipiCsi, "ENTITY_NAME_HINT"));
    assert(has_role(roles_graph.entities[2], isp::MediaEntityRole::Cif, "ENTITY_NAME_HINT"));
    assert(has_role(roles_graph.entities[3], isp::MediaEntityRole::Isp, "ENTITY_NAME_HINT"));
    assert(has_role(roles_graph.entities[4], isp::MediaEntityRole::Mainpath, "ENTITY_NAME_HINT"));
    assert(has_role(roles_graph.entities[5], isp::MediaEntityRole::Selfpath, "ENTITY_NAME_HINT"));
    assert(has_role(roles_graph.entities[4], isp::MediaEntityRole::VideoNode, "REPORTED_NODE_TYPE"));
    isp::MediaEntityInfo unknown;
    unknown.name = "sensor-mainpathology";
    assert(isp::classify_media_entity(unknown).empty()); // No substring/prefix Sensor or mainpath claim.
    auto selected = config.cameras[0];
    selected.media_device = "/dev/media55";
    selected.sensor_entity = "fixture sensor C";
    selected.expected_isp_entity = "fixture-isp";
    selected.video_node = "/dev/video73";
    isp::IspOutputNode selected_output;
    assert(isp::parse_capture_info(single_text, selected.video_node, &selected_output, &error));
    const auto mainpath = isp::SensorRegistry{}.resolve(selected, roles_graph, selected_output);
    assert(mainpath.valid() && mainpath.enabled_path.size() == 5 &&
           mainpath.output_entity == "fixture-mainpath");
    assert(!mainpath.live_evidence && mainpath.isp_generation == "UNKNOWN");
    selected.video_node = "/dev/video74";
    selected_output.video_node = selected.video_node;
    const auto selfpath = isp::SensorRegistry{}.resolve(selected, roles_graph, selected_output);
    assert(!selfpath.mapping_valid &&
           selfpath.output_entity == "fixture-selfpath"); // Disabled output stays invalid.
    isp::ReadOnlyMediaGraphInspector inspector;
    auto evidence = isp::inspect_isp_camera(config.cameras[0], inspector);
    assert(evidence.valid() && !evidence.live_evidence && evidence.enabled_path.size() == 3);
    assert(evidence.graph.evidence == isp::EvidenceSource::OfflineFile);
    assert(evidence.sensor.identity.entity == "fixture sensor A" &&
           !evidence.sensor.identity.physical_camera_id);
    auto wrong = config.cameras[1];
    wrong.sensor_entity = config.cameras[0].sensor_entity;
    assert(!isp::SensorRegistry{}.resolve(wrong, graph, multi).mapping_valid);
    wrong = config.cameras[0];
    wrong.media_device = "/dev/media20";
    assert(!isp::SensorRegistry{}.resolve(wrong, graph, single).mapping_valid);
    auto disabled = graph_text;
    all_replace(&disabled, "[ENABLED]", "[]");
    assert(isp::parse_media_graph(disabled, "/dev/media19", &graph, &error));
    assert(!isp::SensorRegistry{}.resolve(config.cameras[0], graph, single).mapping_valid);
    auto misleading_flags = graph_text;
    all_replace(&misleading_flags, "[ENABLED]", "[NOT_ENABLED]");
    assert(isp::parse_media_graph(misleading_flags, "/dev/media19", &graph, &error));
    assert(!isp::SensorRegistry{}.resolve(config.cameras[0], graph, single).mapping_valid);
    assert(!isp::parse_media_graph("unstructured NV12 /dev/video42", "/dev/media19", &graph, &error));
    assert(!isp::parse_media_graph(replace(graph_text, "\"fixture ISP A\":0", "\"absent ISP\":0"),
                                   "/dev/media19", &graph, &error));
    assert(!isp::parse_media_graph(replace(graph_text, "pad0: Source", "pad0: Sink"), "/dev/media19", &graph,
                                   &error));
    assert(!isp::parse_media_graph(replace(graph_text, "- entity 2:", "- entity 1:"), "/dev/media19", &graph,
                                   &error));
    auto raw = replace(single_text, "Pixel Format      : 'NV12'", "Pixel Format      : 'RG10'");
    assert(isp::parse_capture_info(raw, "/dev/video42", &single, &error));
    assert(!isp::validate_isp_output(config.cameras[0], single)
                .ok()); // NV12 still appears in advertised formats.
    assert(isp::parse_capture_info(replace(single_text, "Bytes per Line    : 672", "Bytes per Line    : 600"),
                                   "/dev/video42", &single, &error));
    assert(!isp::validate_isp_output(config.cameras[0], single).ok());
    assert(isp::parse_capture_info(replace(single_text, "30.000 (30/1)", "29.970 (30000/1001)"),
                                   "/dev/video42", &single, &error));
    assert(!isp::validate_isp_output(config.cameras[0], single).ok());
    assert(isp::parse_capture_info(replace(multi_text, "Number of planes  : 2", "Number of planes  : 1"),
                                   "/dev/video47", &multi, &error));
    assert(!isp::validate_isp_output(config.cameras[1], multi).ok());
    assert(!isp::parse_capture_info("[0]: 'NV12'\n Size: Discrete 640x480", "/dev/video42", &single, &error));
    assert(!isp::parse_capture_info(replace(multi_text, "Plane 1 :", "Plane 99 :"), "/dev/video47", &multi,
                                    &error));
    const std::string unrelated_output =
        "\nStream Parameters Video Output:\n    Frames per second: 25.000 (25/1)\n"
        "ioctl: VIDIOC_ENUM_FMT\n    Type: Video Output\n    [0]: 'BGR3'\n";
    assert(isp::parse_capture_info(single_text + unrelated_output, "/dev/video42", &single, &error));
    assert(single.fps_numerator == 30 && single.advertised_formats == std::vector<std::string>{"NV12"});
    assert(isp::parse_capture_info(
        replace(single_text, "Frames per second: 30.000 (30/1)", "Frame interval unavailable") +
            "\nStream Parameters Video Output:\n    Frames per second: 30.000 (30/1)\n",
        "/dev/video42", &single, &error));
    assert(!single.fps_denominator && !isp::validate_isp_output(config.cameras[0], single).ok());
    assert(!isp::parse_capture_info(
        single_text + "\nStream Parameters Video Capture:\n    Frames per second: 30.000 (30/1)\n",
        "/dev/video42", &single, &error));
    auto invalid = config;
    invalid.cameras[0].fps = 0;
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[1].camera_id = 0;
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[1].video_node = invalid.cameras[0].video_node;
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.strict_isp_ready = false;
    assert(!isp::validate_config(invalid).ok());
    invalid.allow_unverified_isp = true;
    assert(isp::validate_config(invalid).ok());
    invalid.strict_isp_ready = true;
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[0].ownership = isp::IspAiqOwnershipMode::InProcess;
    assert(!isp::validate_config(invalid).ok());
    invalid.cameras[0].independent_context_confirmed = true;
    invalid.cameras[0].isp_instance = "fixture-ISP-resource";
    assert(!isp::validate_config(invalid).ok());
    invalid.cameras[0].start_order = isp::AiqStartOrder::AfterStream;
    assert(!isp::validate_config(invalid).ok());
    invalid.cameras[0].stop_order = isp::AiqStopOrder::BeforeStreamOff;
    assert(isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[0].pixel_format = "RAW10";
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[0].start_order = static_cast<isp::AiqStartOrder>(99);
    assert(!isp::validate_config(invalid).ok());
    invalid = config;
    invalid.cameras[0].stop_order = static_cast<isp::AiqStopOrder>(99);
    assert(!isp::validate_config(invalid).ok());
    const auto temp = fs::temp_directory_path() /
                      ("isp-config-fixture-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
    const auto good_yaml = read(fixtures / "isp_offline.yaml");
    for (const auto &bad_yaml :
         {good_yaml + "unknown_field: 1\n", good_yaml + "version: 1\n",
          replace(good_yaml, "camera_id: 1", "camera_id: -1"),
          replace(good_yaml, "width: 640", "width: 640\n    width: 640"),
          replace(good_yaml, "    fps: 30\n", ""), replace(good_yaml, "fps: 30", "fps: 0"),
          replace(good_yaml, "working_mode: NORMAL", "working_mode: HDR9"),
          replace(good_yaml, "pixel_format: NV12", "pixel_format: [NV12]"),
          replace(good_yaml, "media_device: /dev/media19", "media_device: \"unterminated")}) {
        std::ofstream(temp, std::ios::binary) << bad_yaml;
        auto untouched = config;
        assert(!isp::load_isp_config(temp.string(), &untouched).ok());
        assert(untouched.cameras.size() == 2 &&
               untouched.cameras[0].sensor_entity == config.cameras[0].sensor_entity);
    }
    fs::remove(temp);
    assert(!isp::load_isp_config((fixtures / "absent.yaml").string(), &config).ok());
    return 0;
}
