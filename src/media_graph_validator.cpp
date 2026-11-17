#include "media_graph_validator.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <sys/wait.h>
#define POPEN popen
#define PCLOSE pclose
#endif

namespace {

bool read_file(const std::string &path, std::string *output) {
    if (path.empty() || !output) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream contents;
    contents << input.rdbuf();
    *output = contents.str();
    return true;
}

std::string quote_arg(const std::string &value) {
    // The command names are fixed below. Device paths come from the user and
    // must be shell quoted because popen is used for portability.
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted.push_back(c);
    }
    quoted += "'";
    return quoted;
}

bool run_read_only(const std::string &command, std::string *output) {
    if (!output) return false;
    output->clear();
    FILE *pipe = POPEN(command.c_str(), "r");
    if (!pipe) return false;
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output->append(buffer.data());
    }
    const int status = PCLOSE(pipe);
#ifdef _WIN32
    return status == 0;
#else
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool has_nv12(const std::string &text) {
    return text.find("NV12") != std::string::npos || text.find("nv12") != std::string::npos;
}

bool has_expected_size(const std::string &text, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return true;
    std::ostringstream token;
    token << width << "x" << height;
    return text.find(token.str()) != std::string::npos;
}

bool has_expected_fps(const std::string &text, uint32_t fps) {
    if (fps == 0) return true;
    std::ostringstream token;
    token << "(" << fps << "/1)";
    if (text.find(token.str()) != std::string::npos) return true;
    token.str("");
    token << fps << "/1";
    return text.find(token.str()) != std::string::npos;
}

std::string source_or_query(const std::string &path, const std::string &command, bool *ok) {
    std::string result;
    if (!path.empty()) {
        *ok = read_file(path, &result);
        return result;
    }
    *ok = run_read_only(command, &result);
    return result;
}

} // namespace

std::string MediaGraphValidationReport::summary() const {
    std::ostringstream out;
    out << "graph=" << (graph_available ? "ok" : "missing")
        << " camera0=" << (camera0_available ? "ok" : "missing")
        << " camera1=" << (camera1_available ? "ok" : "missing")
        << " independent=" << (independent_nodes ? "yes" : "no")
        << " nv12=" << (nv12_output ? "yes" : "no")
        << " dimensions=" << (dimensions_match ? "ok" : "mismatch")
        << " fps=" << (fps_match ? "ok" : "mismatch");
    if (!error.empty()) out << " error=" << error;
    return out.str();
}

MediaGraphValidationReport validate_media_graph(const MediaGraphValidationConfig &config) {
    MediaGraphValidationReport report;
    if (config.camera0_node.empty() || config.camera1_node.empty()) {
        report.error = "camera0 and camera1 V4L2 nodes are required";
        return report;
    }
    report.independent_nodes = config.camera0_node != config.camera1_node;

    bool graph_ok = false;
    const std::string graph_command = "media-ctl -p -d " + quote_arg(config.media_device.empty() ? "/dev/media0" : config.media_device);
    const std::string graph = source_or_query(config.graph_file, graph_command, &graph_ok);
    report.graph_source = config.graph_file.empty() ? graph_command : config.graph_file;
    report.graph_available = graph_ok && !graph.empty();
    if (!report.graph_available) {
        report.error = config.graph_file.empty()
                           ? "media-ctl query failed; provide --graph-file only with an unmodified board capture"
                           : "cannot read graph file";
    }

    bool cam0_ok = false;
    bool cam1_ok = false;
    const std::string cam0_command = "v4l2-ctl -d " + quote_arg(config.camera0_node) + " --all --list-formats-ext";
    const std::string cam1_command = "v4l2-ctl -d " + quote_arg(config.camera1_node) + " --all --list-formats-ext";
    const std::string cam0 = source_or_query(config.camera0_info_file, cam0_command, &cam0_ok);
    const std::string cam1 = source_or_query(config.camera1_info_file, cam1_command, &cam1_ok);
    report.camera0_source = config.camera0_info_file.empty() ? cam0_command : config.camera0_info_file;
    report.camera1_source = config.camera1_info_file.empty() ? cam1_command : config.camera1_info_file;
    report.camera0_available = cam0_ok && !cam0.empty();
    report.camera1_available = cam1_ok && !cam1.empty();
    report.nv12_output = has_nv12(cam0) && has_nv12(cam1);
    report.dimensions_match = has_expected_size(cam0, config.expected_width, config.expected_height) &&
                              has_expected_size(cam1, config.expected_width, config.expected_height);
    report.fps_match = has_expected_fps(cam0, config.expected_fps) &&
                       has_expected_fps(cam1, config.expected_fps);
    if (!report.camera0_available || !report.camera1_available) {
        report.error = "v4l2-ctl query failed for one or both camera nodes";
    }
    if (!report.independent_nodes) report.error = "camera0 and camera1 resolve to the same node";
    if (!report.nv12_output) report.error = "both nodes must advertise NV12 or NV12M";
    return report;
}

