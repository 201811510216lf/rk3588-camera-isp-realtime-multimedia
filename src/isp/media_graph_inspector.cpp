#include "isp/media_graph_inspector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace isp {
namespace {
constexpr size_t kMaxText = 4 * 1024 * 1024;
std::string trim(const std::string &s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}
bool fail(std::string *error, const std::string &s) {
    if (error)
        *error = s;
    return false;
}
bool number(const std::string &s, uint32_t *out) {
    try {
        size_t used = 0;
        const auto value = std::stoull(s, &used);
        if (used != s.size() || value > UINT32_MAX)
            return false;
        *out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}
bool load_file(const std::string &path, std::string *text, std::string *error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return fail(error, "saved diagnostic must be a regular file: " + path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return fail(error, "cannot read saved diagnostic: " + path);
    text->clear();
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        if (text->size() + static_cast<size_t>(input.gcount()) > kMaxText)
            return fail(error, "diagnostic exceeds 4 MiB bound");
        text->append(buffer, static_cast<size_t>(input.gcount()));
    }
    if (input.bad())
        return fail(error, "diagnostic read failed: " + path);
    return true;
}
bool query(const std::vector<std::string> &argv, std::string *text, std::string *error) {
#ifdef __linux__
    int pipes[2];
    if (::pipe2(pipes, O_CLOEXEC | O_NONBLOCK))
        return fail(error, "read-only query pipe failed");
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        return fail(error, "spawn action initialization failed");
    }
    // Child output must be blocking; the parent endpoint alone remains nonblocking.
    const int write_flags = ::fcntl(pipes[1], F_GETFL, 0);
    if (write_flags < 0 || ::fcntl(pipes[1], F_SETFL, write_flags & ~O_NONBLOCK) < 0)
        rc = errno;
    if (!rc)
        rc = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (!rc)
        rc = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO);
    if (!rc && pipes[0] != STDOUT_FILENO && pipes[0] != STDERR_FILENO)
        rc = posix_spawn_file_actions_addclose(&actions, pipes[0]);
    if (!rc && pipes[1] != STDOUT_FILENO && pipes[1] != STDERR_FILENO)
        rc = posix_spawn_file_actions_addclose(&actions, pipes[1]);
    std::vector<char *> args;
    for (const auto &arg : argv)
        args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);
    pid_t child = -1;
    if (!rc)
        rc = posix_spawnp(&child, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipes[1]);
    if (rc) {
        ::close(pipes[0]);
        return fail(error, "read-only query unavailable (spawn code " + std::to_string(rc) + ")");
    }
    text->clear();
    bool done = false, failed = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done && !failed) {
        if (std::chrono::steady_clock::now() >= deadline) {
            failed = true;
            break;
        }
        pollfd pfd{pipes[0], POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 50);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || pfd.revents & POLLNVAL) {
            failed = true;
            break;
        }
        if (ready > 0) {
            char buffer[4096];
            for (;;) {
                const auto count = ::read(pipes[0], buffer, sizeof(buffer));
                if (count > 0) {
                    if (text->size() + static_cast<size_t>(count) > kMaxText) {
                        failed = true;
                        break;
                    }
                    text->append(buffer, static_cast<size_t>(count));
                } else if (!count) {
                    done = true;
                    break;
                } else if (errno == EINTR)
                    continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                else {
                    failed = true;
                    break;
                }
            }
        }
    }
    ::close(pipes[0]);
    bool reaped = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = ::waitpid(child, &status, WNOHANG);
        if (result == child) {
            reaped = true;
            break;
        }
        if (result < 0 && errno != EINTR)
            break;
        if (failed)
            break;
        ::poll(nullptr, 0, 10);
    }
    if (!reaped) {
        ::kill(child, SIGKILL);
        // The child is our fixed utility process. Reap after termination; no wait on inherited pipe writers.
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        return fail(error, failed ? "read-only query exceeded timeout/output bound"
                                  : "read-only query did not terminate");
    }
    if (failed || !WIFEXITED(status) || WEXITSTATUS(status))
        return fail(error, "read-only utility returned failure");
    return true;
#else
    (void)argv;
    (void)text;
    return fail(error, "BLOCKED_HARDWARE: live media discovery requires Linux and board tools");
#endif
}
const MediaEntityInfo *entity(const IspDeviceInfo &g, const std::string &name) {
    for (const auto &e : g.entities)
        if (e.name == name)
            return &e;
    return nullptr;
}
const MediaPadInfo *pad(const MediaEntityInfo &e, uint32_t id) {
    for (const auto &p : e.pads)
        if (p.index == id)
            return &p;
    return nullptr;
}
std::string normalized_format(std::string value) { return value == "NM12" ? "NV12M" : value; }
} // namespace

const char *to_string(MediaEntityRole role) {
    switch (role) {
    case MediaEntityRole::Sensor:
        return "SENSOR";
    case MediaEntityRole::MipiCsi:
        return "MIPI_CSI";
    case MediaEntityRole::Cif:
        return "CIF";
    case MediaEntityRole::Isp:
        return "ISP";
    case MediaEntityRole::Mainpath:
        return "MAINPATH";
    case MediaEntityRole::Selfpath:
        return "SELFPATH";
    case MediaEntityRole::VideoNode:
        return "VIDEO_NODE";
    default:
        return "UNKNOWN";
    }
}
const char *to_string(MediaPadDirection direction) {
    switch (direction) {
    case MediaPadDirection::Sink:
        return "SINK";
    case MediaPadDirection::Source:
        return "SOURCE";
    default:
        return "UNKNOWN";
    }
}
std::vector<MediaRoleHint> classify_media_entity(const MediaEntityInfo &info) {
    std::vector<MediaRoleHint> result;
    if (std::regex_search(info.function, std::regex(R"(\bsubtype\s+Sensor\b)", std::regex::icase)))
        result.push_back({MediaEntityRole::Sensor, "REPORTED_FUNCTION"});
    std::string name = info.name;
    for (char &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto hint = [&](MediaEntityRole role, const char *pattern) {
        if (std::regex_search(name, std::regex(pattern)))
            result.push_back({role, "ENTITY_NAME_HINT"});
    };
    // These tokens describe what the entity is called, not what hardware is present.
    hint(MediaEntityRole::MipiCsi, R"((^|[^a-z0-9])(mipi|csi[0-9]*|dphy[0-9]*)([^a-z0-9]|$))");
    hint(MediaEntityRole::Cif, R"((^|[^a-z0-9])(rkcif|cif)([^a-z0-9]|$))");
    hint(MediaEntityRole::Isp, R"((^|[^a-z0-9])isp([^a-z0-9]|$)|rkisp[-_]isp[-_]subdev)");
    hint(MediaEntityRole::Mainpath, R"((^|[^a-z0-9])mainpath([^a-z0-9]|$))");
    hint(MediaEntityRole::Selfpath, R"((^|[^a-z0-9])selfpath([^a-z0-9]|$))");
    if (!info.device_node.empty() &&
        std::regex_search(info.function, std::regex(R"(^type\s+Node\b)", std::regex::icase)))
        result.push_back({MediaEntityRole::VideoNode, "REPORTED_NODE_TYPE"});
    return result;
}

bool parse_media_graph(const std::string &text, const std::string &device, IspDeviceInfo *out,
                       std::string *error) {
    if (error)
        error->clear();
    if (!out || text.empty() || text.size() > kMaxText)
        return fail(error, "empty/oversized media graph or null output");
    *out = {};
    out->media_device = device;
    static const std::regex entity_line(
        R"(^-\s*entity\s+([0-9]+):\s*(.*?)\s*\(([0-9]+)\s+pads?,\s*([0-9]+)\s+links?\).*$)");
    static const std::regex pad_line(R"(^pad([0-9]+):\s*(Sink|Source).*$)");
    static const std::regex link_line(R"link(^(->|<-)\s+"([^"]+)":([0-9]+)\s+\[([^\]]*)\].*$)link");
    static const std::regex format_line(R"(\[fmt:([^/]+)/([0-9]+)x([0-9]+).*)");
    std::istringstream input(text);
    std::string raw;
    MediaEntityInfo *current = nullptr;
    MediaPadInfo *current_pad = nullptr;
    std::set<uint32_t> ids;
    std::set<std::string> names, nodes;
    while (std::getline(input, raw)) {
        if (raw.size() > 65536)
            return fail(error, "media graph line exceeds bound");
        const auto line = trim(raw);
        std::smatch match;
        if (line.rfind("- entity", 0) == 0) {
            if (!std::regex_match(line, match, entity_line) || out->entities.size() >= 1024)
                return fail(error, "malformed or excessive media entity");
            uint32_t id = 0, pads = 0, links = 0;
            if (!number(match[1], &id) || !number(match[3], &pads) || !number(match[4], &links) ||
                pads > 64 || links > 8192 || match[2].str().empty() || match[2].str().size() > 512 ||
                !ids.insert(id).second || !names.insert(match[2]).second)
                return fail(error, "duplicate/invalid media entity");
            out->entities.push_back({});
            current = &out->entities.back();
            current_pad = nullptr;
            current->id = id;
            current->name = match[2];
        } else if (line.rfind("driver ", 0) == 0 && line.rfind("driver version", 0) != 0 && !current)
            out->driver = trim(line.substr(6));
        else if (line.rfind("model", 0) == 0 && !current)
            out->model = trim(line.substr(5));
        else if (line.rfind("bus info", 0) == 0 && !current)
            out->bus_info = trim(line.substr(8));
        else if (current && line.rfind("type ", 0) == 0)
            current->function = line;
        else if (current && line.rfind("device node name ", 0) == 0) {
            current->device_node = trim(line.substr(17));
            if (current->device_node.empty() || !nodes.insert(current->device_node).second)
                return fail(error, "duplicate/empty media device node");
        } else if (current && std::regex_match(line, match, pad_line)) {
            uint32_t id = 0;
            if (!number(match[1], &id) || current->pads.size() >= 64 || pad(*current, id))
                return fail(error, "duplicate/invalid media pad");
            current->pads.push_back({});
            current_pad = &current->pads.back();
            current_pad->index = id;
            current_pad->direction = match[2] == "Sink" ? MediaPadDirection::Sink : MediaPadDirection::Source;
        } else if (current && (line.rfind("->", 0) == 0 || line.rfind("<-", 0) == 0)) {
            uint32_t other_pad = 0;
            if (!current_pad || !std::regex_match(line, match, link_line) || !number(match[3], &other_pad) ||
                out->links.size() >= 8192)
                return fail(error, "malformed/excessive media link");
            MediaLinkInfo link;
            const bool outgoing = match[1] == "->";
            link.source_entity = outgoing ? current->name : match[2].str();
            link.sink_entity = outgoing ? match[2].str() : current->name;
            link.source_pad = outgoing ? current_pad->index : other_pad;
            link.sink_pad = outgoing ? other_pad : current_pad->index;
            std::istringstream flags(match[4]);
            std::string flag;
            while (std::getline(flags, flag, ',')) {
                flag = trim(flag);
                if (flag == "ENABLED")
                    link.enabled = true;
                if (flag == "IMMUTABLE")
                    link.immutable = true;
            }
            auto old = std::find_if(out->links.begin(), out->links.end(), [&](const auto &v) {
                return v.source_entity == link.source_entity && v.source_pad == link.source_pad &&
                       v.sink_entity == link.sink_entity && v.sink_pad == link.sink_pad;
            });
            if (old == out->links.end())
                out->links.push_back(std::move(link));
            else if (old->enabled != link.enabled || old->immutable != link.immutable)
                return fail(error, "inconsistent duplicated media link");
        } else if (current_pad && std::regex_match(line, match, format_line)) {
            if (!number(match[2], &current_pad->width) || !number(match[3], &current_pad->height))
                return fail(error, "invalid media-bus dimensions");
            current_pad->bus_format = match[1];
        }
    }
    if (out->entities.empty())
        return fail(error, "no structured media entities found");
    for (auto &info : out->entities)
        info.role_hints = classify_media_entity(info);
    for (const auto &link : out->links) {
        const auto *source = entity(*out, link.source_entity), *sink = entity(*out, link.sink_entity);
        if (!source || !sink || !pad(*source, link.source_pad) || !pad(*sink, link.sink_pad))
            return fail(error, "link refers to an absent entity/pad");
        if (pad(*source, link.source_pad)->direction != MediaPadDirection::Source ||
            pad(*sink, link.sink_pad)->direction != MediaPadDirection::Sink)
            return fail(error, "media link direction disagrees with pads");
    }
    return true;
}

bool parse_capture_info(const std::string &text, const std::string &node, IspOutputNode *out,
                        std::string *error) {
    if (error)
        error->clear();
    if (!out || text.empty() || text.size() > kMaxText)
        return fail(error, "empty/oversized capture info or null output");
    *out = {};
    out->video_node = node;
    const std::regex size(R"(^Width/Height\s*:\s*([0-9]+)\s*/\s*([0-9]+).*$)");
    const std::regex pixel(R"(^Pixel Format\s*:\s*'([^']+)'.*$)");
    const std::regex fps(R"(^Frames per second\s*:.*\(([0-9]+)/([0-9]+)\).*$)");
    const std::regex count(R"(^Number of planes\s*:\s*([0-9]+).*$)");
    const std::regex plane(R"(^Plane\s+([0-9]+)\s*:.*$)");
    const std::regex stride(R"(^Bytes per Line\s*:\s*([0-9]+).*$)");
    const std::regex image(R"(^Size Image\s*:\s*([0-9]+).*$)");
    const std::regex advertised(R"(^\[[0-9]+\]:\s*'([^']+)'.*$)");
    const std::regex enumeration_type(R"(^Type\s*:\s*(.*)$)");
    bool active = false, capture_parameters = false, capture_enumeration = false;
    size_t current_plane = 0;
    std::istringstream input(text);
    std::string raw;
    while (std::getline(input, raw)) {
        if (raw.size() > 65536)
            return fail(error, "capture info line exceeds bound");
        const auto line = trim(raw);
        std::smatch m;
        if (line == "Video Capture" || line == "Video Capture Multiplanar")
            out->capture_capable = true;
        if (line == "Streaming")
            out->streaming_capable = true;
        if (line.rfind("Stream Parameters", 0) == 0)
            capture_parameters = line.rfind("Stream Parameters Video Capture", 0) == 0;
        else if (line.rfind("Format ", 0) == 0 || line.rfind("ioctl:", 0) == 0)
            capture_parameters = false;
        if (line.rfind("ioctl:", 0) == 0)
            capture_enumeration = false;
        if (std::regex_match(line, m, enumeration_type))
            capture_enumeration = m[1] == "Video Capture" || m[1] == "Video Capture Multiplanar";
        if (line.rfind("Format Video Capture", 0) == 0) {
            if (active || !out->active_format.empty())
                return fail(error, "multiple active capture formats are ambiguous");
            active = true;
            out->multiplanar = line.find("Multiplanar") != std::string::npos;
        } else if (line.rfind("Format ", 0) == 0 || line.rfind("Stream Parameters", 0) == 0 ||
                   line.rfind("ioctl:", 0) == 0)
            active = false;
        if (capture_parameters && std::regex_match(line, m, fps)) {
            if (out->fps_denominator)
                return fail(error, "multiple capture frame rate values are ambiguous");
            if (!number(m[1], &out->fps_numerator) || !number(m[2], &out->fps_denominator) ||
                !out->fps_denominator)
                return fail(error, "invalid capture frame rate");
        }
        if (capture_enumeration && std::regex_match(line, m, advertised))
            out->advertised_formats.push_back(normalized_format(m[1]));
        if (!active)
            continue;
        if (std::regex_match(line, m, size)) {
            if (!number(m[1], &out->width) || !number(m[2], &out->height))
                return fail(error, "invalid capture dimensions");
        } else if (std::regex_match(line, m, pixel))
            out->active_format = normalized_format(m[1]);
        else if (std::regex_match(line, m, count)) {
            if (!number(m[1], &out->plane_count) || !out->plane_count || out->plane_count > 8)
                return fail(error, "invalid capture plane count");
        } else if (std::regex_match(line, m, plane)) {
            uint32_t index = 0;
            if (!number(m[1], &index) || index >= 8)
                return fail(error, "capture plane index exceeds bound");
            current_plane = index;
        } else if (std::regex_match(line, m, stride)) {
            if (out->planes.size() <= current_plane)
                out->planes.resize(current_plane + 1);
            if (!number(m[1], &out->planes[current_plane].stride))
                return fail(error, "invalid capture stride");
        } else if (std::regex_match(line, m, image)) {
            uint32_t value = 0;
            if (!number(m[1], &value))
                return fail(error, "invalid capture sizeimage");
            if (out->planes.size() <= current_plane)
                out->planes.resize(current_plane + 1);
            out->planes[current_plane].sizeimage = value;
        }
    }
    if (!out->multiplanar && !out->active_format.empty())
        out->plane_count = 1;
    if (out->active_format.empty() || !out->width || !out->height)
        return fail(
            error, "active capture format/dimensions are missing; advertised formats alone are insufficient");
    return true;
}

bool ReadOnlyMediaGraphInspector::inspect(const std::string &device, const std::string &file,
                                          IspDeviceInfo *out, std::string *error) {
    if (device.empty())
        return fail(error, "explicit media device is required");
    std::string text;
    if (!(file.empty() ? query({"media-ctl", "-p", "-d", device}, &text, error)
                       : load_file(file, &text, error)))
        return false;
    if (!parse_media_graph(text, device, out, error))
        return false;
    out->evidence = file.empty() ? EvidenceSource::LiveDevice : EvidenceSource::OfflineFile;
    out->source = file.empty() ? "media-ctl -p -d " + device : file;
    return true;
}
bool ReadOnlyMediaGraphInspector::query_output(const std::string &node, const std::string &file,
                                               IspOutputNode *out, std::string *error) {
    if (node.empty())
        return fail(error, "explicit ISP output node is required");
    std::string text;
    if (!(file.empty() ? query({"v4l2-ctl", "-d", node, "--all", "--list-formats-ext"}, &text, error)
                       : load_file(file, &text, error)))
        return false;
    if (!parse_capture_info(text, node, out, error))
        return false;
    out->evidence = file.empty() ? EvidenceSource::LiveDevice : EvidenceSource::OfflineFile;
    out->source = file.empty() ? "v4l2-ctl -d " + node + " --all --list-formats-ext" : file;
    return true;
}
} // namespace isp
