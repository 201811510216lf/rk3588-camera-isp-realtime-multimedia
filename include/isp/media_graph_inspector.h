#pragma once

#include "isp/isp_types.h"

#include <string>
#include <vector>

namespace isp {
enum class MediaPadDirection { Unknown, Sink, Source };
enum class MediaEntityRole { Unknown, Sensor, MipiCsi, Cif, Isp, Mainpath, Selfpath, VideoNode };
struct MediaRoleHint {
    MediaEntityRole role = MediaEntityRole::Unknown;
    // Reported functions and name hints are different evidence. Name hints never grant readiness.
    std::string source;
};
struct MediaPadInfo {
    uint32_t index = 0;
    MediaPadDirection direction = MediaPadDirection::Unknown;
    std::string bus_format;
    uint32_t width = 0, height = 0;
};
struct MediaEntityInfo {
    uint32_t id = 0;
    std::string name;
    std::string function;
    std::string device_node;
    std::vector<MediaPadInfo> pads;
    std::vector<MediaRoleHint> role_hints;
};
struct MediaLinkInfo {
    std::string source_entity, sink_entity;
    uint32_t source_pad = 0, sink_pad = 0;
    bool enabled = false, immutable = false;
};
struct IspDeviceInfo {
    std::string media_device, driver, model, bus_info;
    std::vector<MediaEntityInfo> entities;
    std::vector<MediaLinkInfo> links;
    EvidenceSource evidence = EvidenceSource::NotMeasured;
    std::string source;
};
struct IspPlaneInfo {
    uint32_t stride = 0;
    uint64_t sizeimage = 0;
};
struct IspOutputNode {
    std::string video_node, active_format;
    std::vector<std::string> advertised_formats;
    uint32_t width = 0, height = 0;
    uint32_t fps_numerator = 0, fps_denominator = 0;
    bool capture_capable = false, streaming_capable = false, multiplanar = false;
    uint32_t plane_count = 0;
    std::vector<IspPlaneInfo> planes;
    EvidenceSource evidence = EvidenceSource::NotMeasured;
    std::string source;
};

const char *to_string(MediaEntityRole role);
const char *to_string(MediaPadDirection direction);
// Classification is diagnostic only; configured entities and enabled links remain authoritative.
std::vector<MediaRoleHint> classify_media_entity(const MediaEntityInfo &entity);

// Parsers consume unchanged media-ctl/v4l2-ctl diagnostics. They never establish live provenance themselves.
bool parse_media_graph(const std::string &text, const std::string &media_device, IspDeviceInfo *output,
                       std::string *error = nullptr);
bool parse_capture_info(const std::string &text, const std::string &video_node, IspOutputNode *output,
                        std::string *error = nullptr);
class IMediaGraphInspector {
  public:
    virtual ~IMediaGraphInspector() = default;
    virtual bool inspect(const std::string &media_device, const std::string &graph_file,
                         IspDeviceInfo *output, std::string *error) = 0;
    virtual bool query_output(const std::string &video_node, const std::string &info_file,
                              IspOutputNode *output, std::string *error) = 0;
};
// Live mode is Linux-only and launches fixed read-only argv with bounded output and timeout.
class ReadOnlyMediaGraphInspector final : public IMediaGraphInspector {
  public:
    bool inspect(const std::string &, const std::string &, IspDeviceInfo *, std::string *) override;
    bool query_output(const std::string &, const std::string &, IspOutputNode *, std::string *) override;
};
} // namespace isp
