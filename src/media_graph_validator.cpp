#include "media_graph_validator.h"

#include "isp/sensor_registry.h"

#include <sstream>
#include <utility>

namespace {
std::string quote(const std::string &value) {
    std::string result = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else
            result += static_cast<char>(c);
    }
    return result + '"';
}
const char *boolean(bool value) { return value ? "true" : "false"; }
void roles_json(std::ostream &out, const std::vector<isp::MediaRoleHint> &roles) {
    out << '[';
    for (size_t i = 0; i < roles.size(); ++i) {
        if (i)
            out << ',';
        out << "{\"role\":" << quote(isp::to_string(roles[i].role))
            << ",\"source\":" << quote(roles[i].source) << '}';
    }
    out << ']';
}
void camera_json(std::ostream &out, const isp::IspCameraEvidence &camera) {
    const auto &graph = camera.graph;
    const auto &output = camera.output;
    out << "{\"camera_id\":" << camera.camera_id << ",\"mapping_valid\":" << boolean(camera.mapping_valid)
        << ",\"output_valid\":" << boolean(camera.output_valid)
        << ",\"live_evidence\":" << boolean(camera.live_evidence)
        << ",\"isp_generation\":" << quote(camera.isp_generation)
        << ",\"configured_isp_entity\":" << quote(camera.configured_isp_entity)
        << ",\"sensor\":{\"entity\":" << quote(camera.sensor.identity.entity)
        << ",\"subdev\":" << quote(camera.sensor.subdev)
        << ",\"entity_confirmed\":" << boolean(camera.sensor.identity.confirmed)
        << ",\"source\":" << quote(isp::to_string(camera.sensor.identity.evidence))
        << ",\"sensor_name\":" << quote(camera.sensor.identity.sensor_name) << ",\"physical_camera_id\":";
    if (camera.sensor.identity.physical_camera_id)
        out << *camera.sensor.identity.physical_camera_id;
    else
        out << "null";
    out << "},\"enabled_path\":[";
    for (size_t i = 0; i < camera.enabled_path.size(); ++i) {
        if (i)
            out << ',';
        out << quote(camera.enabled_path[i]);
    }
    out << "],\"graph\":{\"media_device\":" << quote(graph.media_device)
        << ",\"driver\":" << quote(graph.driver) << ",\"model\":" << quote(graph.model)
        << ",\"bus_info\":" << quote(graph.bus_info) << ",\"source\":" << quote(graph.source)
        << ",\"evidence\":" << quote(isp::to_string(graph.evidence)) << ",\"entities\":[";
    for (size_t i = 0; i < graph.entities.size(); ++i) {
        if (i)
            out << ',';
        const auto &entity = graph.entities[i];
        out << "{\"id\":" << entity.id << ",\"name\":" << quote(entity.name)
            << ",\"function\":" << quote(entity.function) << ",\"device_node\":" << quote(entity.device_node)
            << ",\"role_hints\":";
        roles_json(out, entity.role_hints);
        out << ",\"pads\":[";
        for (size_t p = 0; p < entity.pads.size(); ++p) {
            if (p)
                out << ',';
            const auto &pad = entity.pads[p];
            out << "{\"index\":" << pad.index << ",\"direction\":" << quote(isp::to_string(pad.direction))
                << ",\"bus_format\":" << quote(pad.bus_format) << ",\"width\":" << pad.width
                << ",\"height\":" << pad.height << '}';
        }
        out << "]}";
    }
    out << "],\"links\":[";
    for (size_t i = 0; i < graph.links.size(); ++i) {
        if (i)
            out << ',';
        const auto &link = graph.links[i];
        out << "{\"source_entity\":" << quote(link.source_entity) << ",\"source_pad\":" << link.source_pad
            << ",\"sink_entity\":" << quote(link.sink_entity) << ",\"sink_pad\":" << link.sink_pad
            << ",\"enabled\":" << boolean(link.enabled) << ",\"immutable\":" << boolean(link.immutable)
            << '}';
    }
    out << "]},\"output\":{\"entity\":" << quote(camera.output_entity) << ",\"role_hints\":";
    roles_json(out, camera.output_role_hints);
    out << ",\"video_node\":" << quote(output.video_node)
        << ",\"active_format\":" << quote(output.active_format)
        << ",\"capture_capable\":" << boolean(output.capture_capable)
        << ",\"streaming_capable\":" << boolean(output.streaming_capable)
        << ",\"multiplanar\":" << boolean(output.multiplanar) << ",\"width\":" << output.width
        << ",\"height\":" << output.height << ",\"fps_numerator\":" << output.fps_numerator
        << ",\"fps_denominator\":" << output.fps_denominator << ",\"plane_count\":" << output.plane_count
        << ",\"evidence\":" << quote(isp::to_string(output.evidence))
        << ",\"source\":" << quote(output.source) << ",\"advertised_formats\":[";
    for (size_t i = 0; i < output.advertised_formats.size(); ++i) {
        if (i)
            out << ',';
        out << quote(output.advertised_formats[i]);
    }
    out << "],\"planes\":[";
    for (size_t i = 0; i < output.planes.size(); ++i) {
        if (i)
            out << ',';
        out << "{\"stride\":" << output.planes[i].stride << ",\"sizeimage\":" << output.planes[i].sizeimage
            << '}';
    }
    out << "]},\"errors\":[";
    for (size_t i = 0; i < camera.errors.size(); ++i) {
        if (i)
            out << ',';
        out << quote(camera.errors[i]);
    }
    out << "]}";
}
} // namespace

std::string MediaGraphValidationReport::json() const {
    std::ostringstream out;
    out << "{\"status\":" << quote(ok() ? "STRUCTURAL_VALIDATION_PASSED" : "VALIDATION_FAILED")
        << ",\"ok\":" << boolean(ok()) << ",\"graph_available\":" << boolean(graph_available)
        << ",\"camera0_available\":" << boolean(camera0_available)
        << ",\"camera1_available\":" << boolean(camera1_available)
        << ",\"independent_nodes\":" << boolean(independent_nodes)
        << ",\"nv12_output\":" << boolean(nv12_output) << ",\"sensor_mapping\":" << boolean(sensor_mapping)
        << ",\"dimensions_match\":" << boolean(dimensions_match) << ",\"fps_match\":" << boolean(fps_match)
        << ",\"live_evidence\":" << boolean(live_evidence)
        << ",\"hardware_confirmed\":false,\"isp_3a_ready\":\"NOT_MEASURED\""
        << ",\"role_hints_authoritative\":false,\"error\":" << quote(error) << ",\"cameras\":[";
    for (size_t i = 0; i < cameras.size(); ++i) {
        if (i)
            out << ',';
        camera_json(out, cameras[i]);
    }
    out << "]}";
    return out.str();
}

std::string MediaGraphValidationReport::summary() const {
    std::ostringstream out;
    out << "graph=" << (graph_available ? "parsed" : "missing")
        << " camera0=" << (camera0_available ? "parsed" : "missing")
        << " camera1=" << (camera1_available ? "parsed" : "missing")
        << " independent=" << (independent_nodes ? "yes" : "no")
        << " active_nv12=" << (nv12_output ? "yes" : "no")
        << " sensor_mapping=" << (sensor_mapping ? "enabled-path-verified" : "unverified")
        << " dimensions=" << (dimensions_match ? "match" : "mismatch")
        << " fps=" << (fps_match ? "match-or-unspecified" : "mismatch-or-unknown")
        << " evidence=" << (live_evidence ? "LIVE_DEVICE" : "OFFLINE_OR_UNAVAILABLE")
        << " isp_3a_ready=NOT_MEASURED";
    if (!error.empty())
        out << " error=" << error;
    return out.str();
}

MediaGraphValidationReport validate_media_graph(const MediaGraphValidationConfig &config) {
    MediaGraphValidationReport report;
    isp::IspConfig loaded;
    if (!config.isp_config_file.empty()) {
        const auto rc = isp::load_isp_config(config.isp_config_file, &loaded);
        if (!rc.ok()) {
            report.error = rc.message;
            return report;
        }
        if (loaded.cameras.size() != 2) {
            report.error = "dual-camera validator requires two configured cameras";
            return report;
        }
        if (loaded.cameras[0].camera_id == 1)
            std::swap(loaded.cameras[0], loaded.cameras[1]);
    } else {
        if (config.media_device.empty() || config.camera0_node.empty() || config.camera1_node.empty()) {
            report.error =
                "explicit media device and both ISP output nodes are required; no /dev numbering is inferred";
            return report;
        }
        loaded.cameras.resize(2);
        for (uint32_t i = 0; i < 2; ++i) {
            auto &c = loaded.cameras[i];
            c.camera_id = i;
            c.media_device = config.media_device;
            c.video_node = i ? config.camera1_node : config.camera0_node;
            c.sensor_entity = i ? config.camera1_sensor : config.camera0_sensor;
            c.expected_isp_entity = i ? config.camera1_isp : config.camera0_isp;
            c.graph_file = config.graph_file;
            c.capture_info_file = i ? config.camera1_info_file : config.camera0_info_file;
            c.width = config.expected_width;
            c.height = config.expected_height;
            c.fps = config.expected_fps;
        }
    }
    report.independent_nodes = loaded.cameras[0].video_node != loaded.cameras[1].video_node;
    report.graph_available = true;
    report.nv12_output = true;
    report.sensor_mapping = true;
    report.live_evidence = true;
    isp::ReadOnlyMediaGraphInspector inspector;
    for (size_t i = 0; i < loaded.cameras.size(); ++i) {
        auto c = loaded.cameras[i];
        isp::IspDeviceInfo graph;
        isp::IspOutputNode output;
        std::string error;
        const bool graph_ok = inspector.inspect(c.media_device, c.graph_file, &graph, &error);
        const auto graph_error = graph_ok ? std::string{} : error;
        if (graph.source.empty())
            graph.source = c.graph_file.empty() ? "media-ctl -p -d " + c.media_device : c.graph_file;
        report.graph_available = report.graph_available && graph_ok;
        if (!graph_ok && report.error.empty())
            report.error = error;
        const bool capture_ok = inspector.query_output(c.video_node, c.capture_info_file, &output, &error);
        const auto capture_error = capture_ok ? std::string{} : error;
        if (output.source.empty())
            output.source = c.capture_info_file.empty()
                                ? "v4l2-ctl -d " + c.video_node + " --all --list-formats-ext"
                                : c.capture_info_file;
        if (i == 0) {
            report.camera0_available = capture_ok;
            report.graph_source = graph.source;
            report.camera0_source = output.source;
        } else {
            report.camera1_available = capture_ok;
            report.camera1_source = output.source;
        }
        if (!capture_ok && report.error.empty())
            report.error = error;
        if (config.isp_config_file.empty()) {
            if (!c.width)
                c.width = output.width;
            if (!c.height)
                c.height = output.height;
            c.pixel_format = output.active_format;
        }
        report.dimensions_match =
            report.dimensions_match && c.width == output.width && c.height == output.height;
        report.fps_match =
            report.fps_match &&
            (!c.fps || (output.fps_denominator &&
                        static_cast<uint64_t>(c.fps) * output.fps_denominator == output.fps_numerator));
        auto evidence = isp::SensorRegistry{}.resolve(c, graph, output);
        if (!graph_error.empty())
            evidence.errors.insert(evidence.errors.begin(), graph_error);
        if (!capture_error.empty())
            evidence.errors.push_back(capture_error);
        report.nv12_output = report.nv12_output && evidence.output_valid;
        report.sensor_mapping = report.sensor_mapping && evidence.mapping_valid;
        report.live_evidence = report.live_evidence && evidence.live_evidence;
        if (!evidence.errors.empty() && report.error.empty())
            report.error = evidence.errors.front();
        report.cameras.push_back(std::move(evidence));
    }
    if (!report.independent_nodes)
        report.error = "camera0 and camera1 use the same output node";
    return report;
}
