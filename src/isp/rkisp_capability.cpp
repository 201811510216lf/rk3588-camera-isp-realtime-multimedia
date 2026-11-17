#include "isp/rkisp_capability.h"

#include <algorithm>

namespace isp {
IspControlResult validate_isp_output(const AiqCameraConfig &config, const IspOutputNode &output) {
    IspControlResult result;
    result.operation = "validate_isp_output";
    result.camera_id = config.camera_id;
    result.backend_name = "media-graph-inspector";
    result.status = ControlStatus::ConfigMismatch;
    auto reject = [&](const std::string &message) {
        result.message = message;
        return result;
    };
    if (output.video_node != config.video_node)
        return reject("capture node does not match configured output");
    if (!output.capture_capable || !output.streaming_capable)
        return reject("node does not report capture and streaming capabilities");
    if (output.active_format != "NV12" && output.active_format != "NV12M")
        return reject(
            "active output is not ISP processed NV12/NV12M (advertising NV12 alone is insufficient)");
    if (output.active_format != config.pixel_format)
        return reject("active pixel format differs from configuration");
    if (!output.width || !output.height || (output.width & 1U) || (output.height & 1U))
        return reject("NV12 output dimensions must be positive and even");
    if (output.width != config.width || output.height != config.height)
        return reject("active output dimensions differ from configuration");
    if (config.fps && (!output.fps_denominator ||
                       static_cast<uint64_t>(config.fps) * output.fps_denominator != output.fps_numerator))
        return reject("active output FPS differs from configuration or is not measured");
    const uint32_t expected_planes = output.active_format == "NV12M" ? 2U : 1U;
    if (output.plane_count != expected_planes || output.planes.size() != expected_planes ||
        (expected_planes == 2 && !output.multiplanar))
        return reject("output plane layout is missing or inconsistent with format");
    for (uint32_t i = 0; i < expected_planes; ++i) {
        const auto &plane = output.planes[i];
        const uint64_t rows = i ? output.height / 2U : output.height;
        uint64_t minimum = static_cast<uint64_t>(plane.stride) * rows;
        if (expected_planes == 1)
            minimum += static_cast<uint64_t>(plane.stride) * (output.height / 2U);
        if (plane.stride < output.width || plane.sizeimage < minimum)
            return reject("reported stride/sizeimage cannot hold the active plane");
    }
    result.status = ControlStatus::Ok;
    result.request_accepted = true;
    result.message =
        "active NV12 layout validated; DQBUF bytesused and hardware processing remain separate evidence";
    // Parsing live diagnostic text is not proof of successful ISP frames or 3A readiness.
    return result;
}
std::string reported_isp_generation(const IspDeviceInfo &graph) {
    // Generic rkisp driver names do not establish an ISP generation.
    if (graph.driver == "rkisp30" || graph.model == "rkisp30")
        return "RKISP30_REPORTED";
    return "UNKNOWN";
}
} // namespace isp
