#include "isp/isp_config.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace isp {
namespace {
IspControlResult result(ControlStatus status, const std::string &op, const std::string &message) {
    IspControlResult value;
    value.status = status;
    value.operation = op;
    value.message = message;
    value.backend_name = "isp-config";
    value.request_accepted = status == ControlStatus::Ok;
    return value;
}
std::string trim(const std::string &s) {
    const auto begin = s.find_first_not_of(" \r\n");
    return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \r\n") - begin + 1);
}
bool unsigned_value(const std::string &s, uint32_t *out) {
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    try {
        size_t used = 0;
        auto n = std::stoull(s, &used);
        if (used != s.size() || n > UINT32_MAX)
            return false;
        *out = static_cast<uint32_t>(n);
        return true;
    } catch (...) {
        return false;
    }
}
bool boolean(const std::string &s, bool *out) {
    if (s == "true") {
        *out = true;
        return true;
    }
    if (s == "false") {
        *out = false;
        return true;
    }
    return false;
}
bool scalar(const std::string &input, std::string *out) {
    const auto text = trim(input);
    if (text.empty()) {
        out->clear();
        return true;
    }
    if (text[0] == '"' || text[0] == '\'') {
        const char quote = text[0];
        out->clear();
        size_t i = 1;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (c == quote)
                break;
            if (quote == '"' && c == '\\') {
                if (++i >= text.size() || (text[i] != '\\' && text[i] != '"'))
                    return false;
                out->push_back(text[i]);
            } else
                out->push_back(c);
        }
        if (i == text.size())
            return false;
        const auto rest = trim(text.substr(i + 1));
        return rest.empty() || rest[0] == '#';
    }
    if (text.find_first_of("[]{}&*!|>") != std::string::npos)
        return false;
    *out = trim(text.substr(0, text.find('#')));
    return true;
}
bool valid_device(const std::string &s) {
    return s.rfind("/dev/", 0) == 0 && s.size() > 5 && s.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool sane_name(const std::string &s) {
    return !s.empty() && s.size() <= 512 && s.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
} // namespace

IspControlResult validate_config(const IspConfig &config) {
    auto invalid = [&](const std::string &s) {
        return result(ControlStatus::InvalidArgument, "validate_config", s);
    };
    if (config.cameras.empty() || config.cameras.size() > 2)
        return invalid("ISP configuration requires one or two explicit camera entries");
    if (!config.strict_isp_ready && !config.allow_unverified_isp)
        return invalid("strict_isp_ready=false requires explicit allow_unverified_isp=true");
    if (config.strict_isp_ready && config.allow_unverified_isp)
        return invalid("strict_isp_ready=true conflicts with allow_unverified_isp=true");
    std::set<uint32_t> ids;
    std::set<std::string> outputs, sensors;
    for (const auto &camera : config.cameras) {
        if (camera.camera_id > 1 || !ids.insert(camera.camera_id).second)
            return invalid("logical camera id must be distinct 0/1, never inferred from device number");
        if (!valid_device(camera.media_device) || !valid_device(camera.video_node))
            return invalid("media_device and video_node must be explicit absolute /dev paths");
        if (!outputs.insert(camera.video_node).second ||
            !sensors.insert(camera.media_device + "\n" + camera.sensor_entity).second)
            return invalid("duplicate output node or sensor identity");
        if (!sane_name(camera.sensor_entity) || !sane_name(camera.expected_isp_entity) ||
            (!camera.expected_sensor_name.empty() && !sane_name(camera.expected_sensor_name)))
            return invalid("sensor_entity and expected_isp_entity require explicit bounded names");
        if (camera.pixel_format != "NV12" && camera.pixel_format != "NV12M")
            return invalid("product ISP output must be NV12 or NV12M, never a RAW node");
        if (!camera.width || !camera.height || (camera.width & 1U) || (camera.height & 1U) ||
            camera.width > 16384 || camera.height > 16384 || camera.fps > 1000)
            return invalid("invalid ISP output dimensions or requested FPS");
        if (camera.working_mode != IspWorkingMode::Normal && camera.working_mode != IspWorkingMode::Hdr2 &&
            camera.working_mode != IspWorkingMode::Hdr3)
            return invalid("invalid working_mode enum");
        if (camera.ownership != IspAiqOwnershipMode::ExternalService &&
            camera.ownership != IspAiqOwnershipMode::InProcess &&
            camera.ownership != IspAiqOwnershipMode::Unavailable)
            return invalid("invalid ownership enum");
        if (camera.ownership == IspAiqOwnershipMode::InProcess &&
            (!camera.independent_context_confirmed || !sane_name(camera.isp_instance)))
            return invalid(
                "IN_PROCESS needs explicit isp_instance and independent_context_confirmed from BSP topology");
        if (camera.start_order != AiqStartOrder::Unspecified &&
            camera.start_order != AiqStartOrder::BeforeStream &&
            camera.start_order != AiqStartOrder::AfterStream)
            return invalid("invalid start_order enum");
        if (camera.ownership == IspAiqOwnershipMode::InProcess &&
            camera.start_order == AiqStartOrder::Unspecified)
            return invalid("IN_PROCESS requires explicit BSP start_order before-stream or after-stream");
        if (camera.stop_order != AiqStopOrder::Unspecified &&
            camera.stop_order != AiqStopOrder::BeforeStreamOff &&
            camera.stop_order != AiqStopOrder::AfterStreamOff)
            return invalid("invalid stop_order enum");
        if (camera.ownership == IspAiqOwnershipMode::InProcess &&
            camera.stop_order == AiqStopOrder::Unspecified)
            return invalid("IN_PROCESS requires explicit BSP stop_order before-streamoff or after-streamoff");
    }
    return result(ControlStatus::Ok, "validate_config",
                  "software configuration validated; hardware identity/IQ readiness not asserted");
}

IspControlResult load_isp_config(const std::string &path, IspConfig *config) {
    auto invalid = [&](const std::string &s) {
        return result(ControlStatus::InvalidArgument, "load_isp_config", s);
    };
    if (!config)
        return invalid("null ISP configuration output");
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error))
        return result(ControlStatus::NotFound, "load_isp_config",
                      "configuration must be a readable regular file: " + path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return result(ControlStatus::NotFound, "load_isp_config", "cannot read ISP configuration: " + path);
    IspConfig parsed;
    std::string raw;
    size_t line_number = 0, total = 0;
    bool cameras = false, version = false;
    std::set<std::string> top_keys;
    std::vector<std::set<std::string>> camera_keys;
    auto error = [&](const std::string &s) {
        return invalid("line " + std::to_string(line_number) + ": " + s);
    };
    while (std::getline(input, raw)) {
        ++line_number;
        total += raw.size() + 1;
        if (total > 1024 * 1024 || raw.size() > 4096 || raw.find('\t') != std::string::npos ||
            raw.find('\0') != std::string::npos)
            return error("configuration exceeds bounds or uses tabs/NUL");
        if (line_number == 1 && raw.rfind("\xef\xbb\xbf", 0) == 0)
            raw.erase(0, 3);
        auto line = trim(raw);
        if (line.empty() || line[0] == '#')
            continue;
        const auto indent = raw.find_first_not_of(' ');
        bool new_camera = line.rfind("- ", 0) == 0;
        if (new_camera) {
            if (!cameras || indent != 2 || parsed.cameras.size() >= 2)
                return error("camera list requires two-space '- camera_id:' entries (max two)");
            parsed.cameras.push_back({});
            camera_keys.emplace_back();
            line = trim(line.substr(2));
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            return error("expected a scalar key: value");
        const auto key = trim(line.substr(0, colon));
        std::string value;
        if (key.empty() || !scalar(line.substr(colon + 1), &value))
            return error("unsupported YAML scalar syntax");
        if (indent == 0) {
            if (!top_keys.insert(key).second)
                return error("duplicate top-level key: " + key);
            if (key == "version") {
                if (value != "1")
                    return error("only version 1 is supported");
                version = true;
            } else if (key == "strict_isp_ready") {
                if (!boolean(value, &parsed.strict_isp_ready))
                    return error("strict_isp_ready must be true/false");
            } else if (key == "allow_unverified_isp") {
                if (!boolean(value, &parsed.allow_unverified_isp))
                    return error("allow_unverified_isp must be true/false");
            } else if (key == "cameras" && value.empty())
                cameras = true;
            else
                return error("unknown top-level key: " + key);
            continue;
        }
        if (!cameras || parsed.cameras.empty() || (!new_camera && indent != 4))
            return error("camera scalar fields require four-space indentation");
        if (!camera_keys.back().insert(key).second)
            return error("duplicate camera key: " + key);
        auto &c = parsed.cameras.back();
        if (new_camera && key != "camera_id")
            return error("camera list entries must start with camera_id");
        if (key == "camera_id") {
            if (!unsigned_value(value, &c.camera_id))
                return error("invalid camera_id");
        } else if (key == "width") {
            if (!unsigned_value(value, &c.width))
                return error("invalid width");
        } else if (key == "height") {
            if (!unsigned_value(value, &c.height))
                return error("invalid height");
        } else if (key == "fps") {
            if (!unsigned_value(value, &c.fps))
                return error("invalid fps");
        } else if (key == "media_device")
            c.media_device = value;
        else if (key == "sensor_entity")
            c.sensor_entity = value;
        else if (key == "expected_sensor_name")
            c.expected_sensor_name = value;
        else if (key == "expected_isp_entity")
            c.expected_isp_entity = value;
        else if (key == "video_node")
            c.video_node = value;
        else if (key == "pixel_format")
            c.pixel_format = value;
        else if (key == "iq_root")
            c.iq_root = value;
        else if (key == "iq_file")
            c.iq_file = value;
        else if (key == "graph_file")
            c.graph_file = value;
        else if (key == "capture_info_file")
            c.capture_info_file = value;
        else if (key == "isp_instance")
            c.isp_instance = value;
        else if (key == "independent_context_confirmed") {
            if (!boolean(value, &c.independent_context_confirmed))
                return error("invalid independent_context_confirmed boolean");
        } else if (key == "start_order") {
            if (value == "unspecified")
                c.start_order = AiqStartOrder::Unspecified;
            else if (value == "before-stream")
                c.start_order = AiqStartOrder::BeforeStream;
            else if (value == "after-stream")
                c.start_order = AiqStartOrder::AfterStream;
            else
                return error("unknown start_order");
        } else if (key == "stop_order") {
            if (value == "unspecified")
                c.stop_order = AiqStopOrder::Unspecified;
            else if (value == "before-streamoff")
                c.stop_order = AiqStopOrder::BeforeStreamOff;
            else if (value == "after-streamoff")
                c.stop_order = AiqStopOrder::AfterStreamOff;
            else
                return error("unknown stop_order");
        } else if (key == "ownership") {
            if (value == "EXTERNAL_SERVICE")
                c.ownership = IspAiqOwnershipMode::ExternalService;
            else if (value == "IN_PROCESS")
                c.ownership = IspAiqOwnershipMode::InProcess;
            else if (value == "UNAVAILABLE")
                c.ownership = IspAiqOwnershipMode::Unavailable;
            else
                return error("unknown ownership mode");
        } else if (key == "working_mode") {
            if (value == "NORMAL")
                c.working_mode = IspWorkingMode::Normal;
            else if (value == "HDR2")
                c.working_mode = IspWorkingMode::Hdr2;
            else if (value == "HDR3")
                c.working_mode = IspWorkingMode::Hdr3;
            else
                return error("unknown working_mode");
        } else
            return error("unknown camera key: " + key);
    }
    if (input.bad())
        return result(ControlStatus::BackendError, "load_isp_config", "configuration read failed");
    if (!version)
        return invalid("version: 1 is required");
    for (const auto &keys : camera_keys)
        for (const auto *key : {"camera_id", "media_device", "sensor_entity", "expected_isp_entity",
                                "video_node", "pixel_format", "width", "height", "ownership", "working_mode"})
            if (!keys.count(key))
                return invalid(std::string("camera entry missing required key: ") + key);
    const auto base = std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    for (auto &c : parsed.cameras)
        for (auto *p : {&c.graph_file, &c.capture_info_file, &c.iq_root})
            if (!p->empty() && (*p)[0] != '/' && std::filesystem::path(*p).is_relative())
                *p = (base / *p).lexically_normal().string();
    const auto valid = validate_config(parsed);
    if (!valid.ok())
        return valid;
    *config = std::move(parsed);
    return result(ControlStatus::Ok, "load_isp_config",
                  "strict YAML subset parsed; runtime mapping still requires device evidence");
}
} // namespace isp
