#include "camera_frame.h"
#include "isp/isp_readiness.h"
#include "v4l2_isp_camera.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }

uint32_t number(const std::string &value, uint32_t maximum) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned decimal value");
    const auto parsed = std::stoull(value);
    if (parsed > maximum)
        throw std::invalid_argument("numeric argument exceeds limit");
    return static_cast<uint32_t>(parsed);
}
std::string quoted(const std::string &value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
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
void require(const isp::IspControlResult &result) {
    if (!result.ok())
        throw std::runtime_error(isp::result_json(result));
}
} // namespace

// A bounded single-Camera diagnostic using the production capture implementation.
// There is no alternate Camera source, ISP algorithm or implicit service takeover.
int main(int argc, char **argv) {
    V4L2IspCamera camera;
    std::unique_ptr<isp::IspPipelineCoordinator> coordinator;
    bool prepared = false;
    std::ofstream raw, metadata;
    uint32_t captured = 0;
    uint64_t packed_bytes = 0;
    auto cleanup = [&] {
        bool ok = true;
        if (prepared) {
            const auto result = isp::invoke_readiness_operation(
                "capture_before_stream_stop", [&] { return coordinator->before_stream_stop(); });
            if (!result.ok()) {
                std::cerr << isp::result_json(result) << '\n';
                ok = false;
            }
        }
        std::string error;
        if (!camera.stop(&error)) {
            std::cerr << "capture stop: " << error << '\n';
            ok = false;
            if (camera.is_open())
                return false;
        }
        if (coordinator) {
            if (prepared) {
                const auto result = isp::invoke_readiness_operation(
                    "capture_after_stream_stop", [&] { return coordinator->after_stream_stop(); });
                if (!result.ok()) {
                    std::cerr << isp::result_json(result) << '\n';
                    ok = false;
                }
            }
            const auto result =
                isp::invoke_readiness_operation("capture_shutdown", [&] { return coordinator->shutdown(); });
            if (!result.ok()) {
                std::cerr << isp::result_json(result) << '\n';
                ok = false;
            }
        }
        prepared = false;
        return ok;
    };
    try {
        std::string config_path, out_dir;
        uint32_t camera_id = 0, frames = 120, timeout_sec = 60;
        bool selected = false, allow_unverified = false;
        std::set<std::string> seen;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: isp_capture --isp-config PATH --camera-id 0|1 --out-dir NEW_DIR\n"
                             "  [--frames 1..1000000] [--timeout-sec 1..3600] [--allow-unverified-isp]\n"
                             "Saves packed NV12 plus driver metadata. MMAP capture; no service takeover.\n";
                return 0;
            }
            if (!seen.insert(arg).second)
                throw std::invalid_argument("duplicate option: " + arg);
            if (arg == "--allow-unverified-isp") {
                allow_unverified = true;
                continue;
            }
            if (arg != "--isp-config" && arg != "--camera-id" && arg != "--out-dir" && arg != "--frames" &&
                arg != "--timeout-sec")
                throw std::invalid_argument("unknown option: " + arg);
            if (++i >= argc || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
                throw std::invalid_argument("missing value for " + arg);
            const std::string value = argv[i];
            if (arg == "--isp-config")
                config_path = value;
            else if (arg == "--out-dir")
                out_dir = value;
            else if (arg == "--camera-id") {
                camera_id = number(value, 1);
                selected = true;
            } else if (arg == "--frames")
                frames = number(value, 1000000);
            else
                timeout_sec = number(value, 3600);
        }
        if (config_path.empty() || out_dir.empty() || !selected || !frames || !timeout_sec)
            throw std::invalid_argument("config, camera-id, new out-dir and positive limits are required");
        if (std::filesystem::exists(out_dir))
            throw std::invalid_argument("out-dir must not exist; existing evidence will not be overwritten");
        isp::IspConfig config;
        require(isp::load_isp_config(config_path, &config));
        const auto found = std::find_if(config.cameras.begin(), config.cameras.end(),
                                        [&](const auto &item) { return item.camera_id == camera_id; });
        if (found == config.cameras.end())
            throw std::invalid_argument("camera-id is absent from ISP configuration");
        const auto selected_camera = *found;
        if (!selected_camera.fps)
            throw std::invalid_argument("selected Camera requires a positive requested fps");
        // Do not initialize the other Camera during the single-Camera acceptance step.
        config.cameras = {selected_camera};
        if (allow_unverified) {
            config.strict_isp_ready = false;
            config.allow_unverified_isp = true;
        }
        coordinator = std::make_unique<isp::IspPipelineCoordinator>(config);
        require(isp::invoke_readiness_operation("capture_before_stream",
                                                [&] { return coordinator->before_stream(); }));
        prepared = true;
        V4L2IspCamera::Config capture;
        capture.camera_id = camera_id;
        capture.device = selected_camera.video_node;
        capture.width = selected_camera.width;
        capture.height = selected_camera.height;
        capture.fps = selected_camera.fps;
        capture.pixel_format = selected_camera.pixel_format;
        capture.poll_timeout_ms = 250;
        std::string error;
        if (!camera.set_config(capture, &error) || !camera.open(&error))
            throw std::runtime_error(error);
        const auto &actual = camera.capabilities();
        if (actual.width != capture.width || actual.height != capture.height ||
            actual.format_name != capture.pixel_format)
            throw std::runtime_error("driver G_FMT differs from the explicit Sensor/output configuration");
        if (!camera.start(&error))
            throw std::runtime_error(error);
        require(isp::invoke_readiness_operation("capture_after_stream",
                                                [&] { return coordinator->after_stream(); }));
        if (!std::filesystem::create_directory(out_dir))
            throw std::runtime_error("cannot create new capture directory");
        const auto directory = std::filesystem::path(out_dir);
        raw.open(directory / "capture.nv12", std::ios::out | std::ios::binary);
        metadata.open(directory / "capture.jsonl", std::ios::out | std::ios::binary);
        if (!raw || !metadata)
            throw std::runtime_error("cannot create capture evidence files");
        metadata << "{\"record\":\"session\",\"source\":\"LIVE_V4L2_CAPTURE\",\"camera_id\":" << camera_id
                 << ",\"device\":" << quoted(capture.device) << ",\"width\":" << actual.width
                 << ",\"height\":" << actual.height << ",\"driver_format\":" << quoted(actual.format_name)
                 << ",\"driver_field\":" << actual.field << ",\"plane_count\":" << actual.plane_count
                 << ",\"packed_format\":\"NV12\",\"frame_interval_numerator\":"
                 << actual.frame_interval_numerator
                 << ",\"frame_interval_denominator\":" << actual.frame_interval_denominator
                 << ",\"io_mode\":\"MMAP\",\"isp_status\":"
                 << isp::status_json(isp::query_readiness_status(*coordinator, camera_id)) << "}\n";
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
        auto next_status = std::chrono::steady_clock::now();
        std::vector<uint8_t> packed;
        while (captured < frames && !interrupted && std::chrono::steady_clock::now() < deadline) {
            if (std::chrono::steady_clock::now() >= next_status) {
                require(isp::evaluate_runtime_status(camera_id,
                                                     isp::query_readiness_status(*coordinator, camera_id),
                                                     config.allow_unverified_isp)
                            .result);
                next_status = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            error.clear();
            const auto before_dequeue = camera.stats();
            auto frame = camera.dequeue(&error);
            if (!frame) {
                if (!camera.is_streaming())
                    throw std::runtime_error(error.empty() ? "Camera capture stopped" : error);
                if (camera.stats().poll_timeouts > before_dequeue.poll_timeouts)
                    continue;
                if (!error.empty())
                    throw std::runtime_error(error);
                continue;
            }
            if (!camera::copy_nv12_packed(frame->desc, &packed, &error))
                throw std::runtime_error(error);
            const auto &d = frame->desc;
            raw.write(reinterpret_cast<const char *>(packed.data()),
                      static_cast<std::streamsize>(packed.size()));
            metadata << "{\"record\":\"frame\",\"camera_id\":" << d.camera_id << ",\"index\":" << d.index
                     << ",\"sequence\":" << d.sequence << ",\"timestamp_ns\":" << d.timestamp_ns
                     << ",\"monotonic_timestamp_ns\":" << d.monotonic_timestamp_ns
                     << ",\"clock_domain\":" << static_cast<unsigned>(d.clock_domain)
                     << ",\"timestamp_flags\":" << d.timestamp_flags
                     << ",\"timestamp_source_flags\":" << d.timestamp_source_flags << ",\"flags\":" << d.flags
                     << ",\"plane_count\":" << d.plane_count << ",\"stride_y\":" << d.stride_y
                     << ",\"stride_uv\":" << d.stride_uv << ",\"data_offset\":" << d.data_offset
                     << ",\"data_offset_uv\":" << d.data_offset_uv << ",\"bytesused\":" << d.bytesused
                     << ",\"bytesused_uv\":" << d.bytesused_uv << ",\"sizeimage\":" << d.sizeimage
                     << ",\"sizeimage_uv\":" << d.sizeimage_uv << ",\"packed_file_offset\":" << packed_bytes
                     << ",\"packed_bytes\":" << packed.size() << "}\n";
            if (!raw || !metadata)
                throw std::runtime_error("capture evidence write failed; partial files retained");
            packed_bytes += packed.size();
            ++captured;
            // The frame's final lease returns its slot only after both copies have finished.
        }
        const bool stopped = cleanup();
        const bool complete = captured == frames && stopped;
        metadata << "{\"record\":\"end\",\"capture_complete\":" << (complete ? "true" : "false")
                 << ",\"frames\":" << captured << ",\"frames_requested\":" << frames
                 << ",\"interrupted\":" << (interrupted ? "true" : "false")
                 << ",\"shutdown_ok\":" << (stopped ? "true" : "false") << "}\n";
        raw.flush();
        metadata.flush();
        if (!raw || !metadata)
            throw std::runtime_error("capture evidence flush failed");
        std::cout << (complete ? "CAPTURE_COMPLETE" : "CAPTURE_INCOMPLETE") << " frames=" << captured
                  << " evidence=" << directory.string() << '\n';
        return complete ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "CAPTURE_FAILED: " << error.what() << '\n';
        const bool stopped = cleanup();
        if (metadata) {
            metadata << "{\"record\":\"end\",\"capture_complete\":false,\"frames\":" << captured
                     << ",\"shutdown_ok\":" << (stopped ? "true" : "false")
                     << ",\"error\":" << quoted(error.what()) << "}\n";
            metadata.flush();
        }
        return 1;
    }
}
