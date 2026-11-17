#include "media_graph_validator.h"

#include "isp/sensor_registry.h"

#include <sstream>
#include <utility>

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
        report.graph_available = report.graph_available && graph_ok;
        if (!graph_ok && report.error.empty())
            report.error = error;
        const bool capture_ok = inspector.query_output(c.video_node, c.capture_info_file, &output, &error);
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
        const auto evidence = isp::SensorRegistry{}.resolve(c, graph, output);
        report.nv12_output = report.nv12_output && evidence.output_valid;
        report.sensor_mapping = report.sensor_mapping && evidence.mapping_valid;
        report.live_evidence = report.live_evidence && evidence.live_evidence;
        if (!evidence.errors.empty() && report.error.empty())
            report.error = evidence.errors.front();
    }
    if (!report.independent_nodes)
        report.error = "camera0 and camera1 use the same output node";
    return report;
}
