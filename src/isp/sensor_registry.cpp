#include "isp/sensor_registry.h"
#include "isp/rkisp_capability.h"

#include <algorithm>
#include <map>
#include <queue>
#include <regex>
#include <set>

namespace isp {
namespace {
const MediaEntityInfo *find_entity(const IspDeviceInfo &graph, const std::string &name) {
    for (const auto &e : graph.entities)
        if (e.name == name)
            return &e;
    return nullptr;
}
std::vector<std::string> enabled_route(const IspDeviceInfo &g, const std::string &from,
                                       const std::string &to) {
    std::queue<std::string> pending;
    pending.push(from);
    std::map<std::string, std::string> parent;
    parent[from] = "";
    while (!pending.empty()) {
        auto current = pending.front();
        pending.pop();
        if (current == to) {
            std::vector<std::string> result;
            for (auto value = to; !value.empty(); value = parent[value])
                result.push_back(value);
            std::reverse(result.begin(), result.end());
            return result;
        }
        for (const auto &link : g.links)
            if (link.enabled && link.source_entity == current && !parent.count(link.sink_entity)) {
                parent[link.sink_entity] = current;
                pending.push(link.sink_entity);
            }
    }
    return {};
}
} // namespace
IspCameraEvidence SensorRegistry::resolve(const AiqCameraConfig &config, const IspDeviceInfo &graph,
                                          const IspOutputNode &output) const {
    IspCameraEvidence result;
    result.camera_id = config.camera_id;
    result.graph = graph;
    result.output = output;
    result.live_evidence =
        graph.evidence == EvidenceSource::LiveDevice && output.evidence == EvidenceSource::LiveDevice;
    result.isp_generation = reported_isp_generation(graph);
    result.sensor.identity.entity = config.sensor_entity;
    result.sensor.identity.evidence = graph.evidence;
    if (config.media_device.empty() || config.sensor_entity.empty() || config.expected_isp_entity.empty() ||
        config.video_node.empty()) {
        result.errors.push_back(
            "explicit media, sensor entity, ISP entity and output node mapping is required");
        return result;
    }
    if (graph.media_device != config.media_device) {
        result.errors.push_back("media device differs from configured mapping");
        return result;
    }
    const auto *sensor = find_entity(graph, config.sensor_entity);
    const auto *isp = find_entity(graph, config.expected_isp_entity);
    const MediaEntityInfo *capture = nullptr;
    for (const auto &e : graph.entities)
        if (e.device_node == config.video_node) {
            if (capture) {
                result.errors.push_back("output node belongs to multiple entities");
                return result;
            }
            capture = &e;
        }
    if (!sensor || !isp || !capture)
        result.errors.push_back("configured sensor/ISP/output entity is absent from topology");
    else if (sensor == isp || isp == capture || sensor == capture)
        result.errors.push_back("sensor, ISP and output must be distinct topology entities");
    else if (!std::regex_search(sensor->function, std::regex(R"(\bsubtype\s+Sensor\b)", std::regex::icase)))
        result.errors.push_back("configured sensor entity has no reported Sensor function");
    else {
        const auto upstream = enabled_route(graph, sensor->name, isp->name);
        const auto downstream = enabled_route(graph, isp->name, capture->name);
        if (upstream.empty() || downstream.empty())
            result.errors.push_back("no enabled sensor-to-ISP-to-output route exists");
        else {
            result.enabled_path = upstream;
            result.enabled_path.insert(result.enabled_path.end(), downstream.begin() + 1, downstream.end());
            result.mapping_valid = true;
            result.sensor.subdev = sensor->device_node;
            // Sensor entity identity is established; model/physical camera id are not guessed from its text.
            result.sensor.identity.confirmed = true;
        }
    }
    const auto format = validate_isp_output(config, output);
    result.output_valid = format.ok();
    if (!format.ok())
        result.errors.push_back(format.message);
    return result;
}
IspCameraEvidence inspect_isp_camera(const AiqCameraConfig &config, IMediaGraphInspector &inspector) {
    IspCameraEvidence result;
    result.camera_id = config.camera_id;
    std::string error;
    if (!inspector.inspect(config.media_device, config.graph_file, &result.graph, &error)) {
        result.errors.push_back(error);
        return result;
    }
    if (!inspector.query_output(config.video_node, config.capture_info_file, &result.output, &error)) {
        result.errors.push_back(error);
        return result;
    }
    return SensorRegistry{}.resolve(config, result.graph, result.output);
}
} // namespace isp
