#include "mipi_pipeline.h"

#include "bounded_frame_queue.h"
#include "mpp_encoder.h"
#include "rknnPool.hpp"
#include "im2d.h"
#include "RgaUtils.h"

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {

const char *default_model_path(uint32_t index) {
    switch (index) {
    case 0: return "../../model/person_relu.rknn";
    case 1: return "../../model/helmet_relu.rknn";
    case 2: return "../../model/tired_relu.rknn";
    case 3: return "../../model/callplay_relu.rknn";
    default: return "";
    }
}

int model_class_count(uint32_t index) {
    return index == 0 ? 1 : 2;
}

std::string json_escape(const char *text) {
    std::string result;
    if (!text) return result;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor == '\\' || *cursor == '"') result.push_back('\\');
        result.push_back(*cursor);
    }
    return result;
}

bool copy_nv12(const camera::CameraFrame &frame, std::vector<uint8_t> *output) {
    if (!output || !frame.valid() || !frame.desc.data || !frame.desc.data_uv ||
        frame.desc.width == 0 || frame.desc.height == 0) {
        return false;
    }
    const uint32_t width = frame.desc.width;
    const uint32_t height = frame.desc.height;
    const uint32_t stride_y = frame.desc.stride_y ? frame.desc.stride_y : width;
    const uint32_t stride_uv = frame.desc.stride_uv ? frame.desc.stride_uv : width;
    const size_t required_y = static_cast<size_t>(stride_y) * height;
    const size_t required_uv = static_cast<size_t>(stride_uv) * (height / 2);
    if (frame.desc.data_length < required_y || frame.desc.data_uv_length < required_uv) {
        return false;
    }
    const size_t required = static_cast<size_t>(width) * height * 3 / 2;
    output->assign(required, 0);

    const uint8_t *src_y = static_cast<const uint8_t *>(frame.desc.data);
    uint8_t *dst_y = output->data();
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst_y + static_cast<size_t>(row) * width,
                    src_y + static_cast<size_t>(row) * stride_y, width);
    }

    const uint8_t *src_uv = static_cast<const uint8_t *>(frame.desc.data_uv);
    uint8_t *dst_uv = output->data() + static_cast<size_t>(width) * height;
    for (uint32_t row = 0; row < height / 2; ++row) {
        std::memcpy(dst_uv + static_cast<size_t>(row) * width,
                    src_uv + static_cast<size_t>(row) * stride_uv, width);
    }
    return true;
}

bool nv12_to_bgr(const std::vector<uint8_t> &nv12, uint32_t width, uint32_t height,
                 cv::Mat *bgr) {
    if (!bgr || width == 0 || height == 0 ||
        nv12.size() < static_cast<size_t>(width) * height * 3 / 2) {
        return false;
    }
    bgr->create(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
    rga_buffer_t src = wrapbuffer_virtualaddr_t(
        const_cast<uint8_t *>(nv12.data()), width, height, width, height,
        RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(
        bgr->data, width, height, width, height, RK_FORMAT_BGR_888);
    if (imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888,
                  IM_COLOR_SPACE_DEFAULT) != IM_STATUS_SUCCESS) {
        cv::Mat yuv(static_cast<int>(height + height / 2), static_cast<int>(width),
                    CV_8UC1, const_cast<uint8_t *>(nv12.data()));
        cv::cvtColor(yuv, *bgr, cv::COLOR_YUV2BGR_NV12);
    }
    return !bgr->empty();
}

} // namespace

struct MipiPipeline::Impl {
    struct AiItem {
        camera::CameraFrame frame;
        uint64_t sequence = 0;
        uint64_t timestamp_ns = 0;
    };

    explicit Impl(MipiPipelineConfig value)
        : config(std::move(value)), camera(config.camera), ai_queue(2) {}

    MipiPipelineConfig config;
    V4L2IspCamera camera;
    MppEncoder encoder;
    std::ofstream output;
    std::ofstream events;
    std::unique_ptr<rknn_lite> model;
    std::string model_identifier;
    BoundedFrameQueue<AiItem> ai_queue;
    std::thread ai_thread;
    std::atomic<bool> stop{false};
    bool ai_active = false;
    bool header_written = false;
    mutable std::mutex stats_mutex;
    MipiPipelineStats counters;
    std::chrono::steady_clock::time_point started;

    bool stop_requested() const {
        return stop.load() || (config.external_stop && config.external_stop->load());
    }

    void add_capture_error() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        ++counters.capture_errors;
    }

    void ai_loop() {
        AiItem item;
        while (ai_queue.pop(item)) {
            if (stop_requested()) {
                continue;
            }
            const auto infer_start = std::chrono::steady_clock::now();
            std::vector<uint8_t> nv12;
            cv::Mat bgr;
            // RGA/RKNN must not retain the camera lease. The copy/convert is
            // completed before the potentially slow model call begins.
            const bool copied = copy_nv12(item.frame, &nv12);
            item.frame = camera::CameraFrame{};
            if (!copied || !nv12_to_bgr(nv12, config.camera.width, config.camera.height, &bgr)) {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.ai_errors;
                continue;
            }
            if (config.simulate_ai_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.simulate_ai_ms));
            }

            detect_result_group_t results{};
            int infer_result = 0;
            if (model && model->valid()) {
                model->ori_img = std::move(bgr);
                infer_result = model->interf(results);
            }
            const auto infer_end = std::chrono::steady_clock::now();
            const double infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.ai_processed;
                counters.last_ai_infer_ms = infer_ms;
                if (infer_result < 0) ++counters.ai_errors;
                if (counters.ai_processed > 1) {
                    const double elapsed = std::chrono::duration<double>(infer_end - started).count();
                    if (elapsed > 0.0) counters.ai_fps = counters.ai_processed / elapsed;
                }
            }

            if (events.is_open()) {
                const char *label = (results.count > 0) ? results.results[0].name : "";
                const float confidence = (results.count > 0) ? results.results[0].prop : 0.0f;
                const auto monotonic_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    infer_end - started).count();
                events << "{\"timestamp_monotonic_ns\":" << monotonic_ns
                       << ",\"frame_sequence\":" << item.sequence
                       << ",\"model\":\"" << json_escape(model_identifier.c_str())
                       << "\",\"label\":\"" << json_escape(label)
                       << "\",\"confidence\":" << std::fixed << std::setprecision(6)
                       << confidence << ",\"infer_ms\":" << infer_ms << "}\n";
                events.flush();
            }
        }
    }

    void close_ai() {
        ai_queue.close();
        if (ai_thread.joinable()) ai_thread.join();
    }
};

MipiPipeline::MipiPipeline(MipiPipelineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MipiPipeline::~MipiPipeline() {
    request_stop();
    if (impl_) {
        impl_->close_ai();
        std::string ignored;
        (void)impl_->camera.stop(&ignored);
    }
}

void MipiPipeline::request_stop() {
    if (impl_) {
        impl_->stop.store(true);
        impl_->ai_queue.close();
    }
}

bool MipiPipeline::run(std::string *error) {
    if (error) error->clear();
    if (!impl_) {
        if (error) *error = "pipeline implementation is unavailable";
        return false;
    }
    Impl &pipeline = *impl_;
    pipeline.started = std::chrono::steady_clock::now();

    std::string camera_error;
    if (!pipeline.camera.open(&camera_error)) {
        if (error) *error = camera_error;
        return false;
    }
    if (!pipeline.camera.start(&camera_error)) {
        if (error) *error = camera_error;
        (void)pipeline.camera.stop(nullptr);
        return false;
    }

    const auto &caps = pipeline.camera.capabilities();
    if (caps.width != pipeline.config.camera.width || caps.height != pipeline.config.camera.height) {
        std::cerr << "V4L2 negotiated " << caps.width << "x" << caps.height
                  << " (requested " << pipeline.config.camera.width << "x"
                  << pipeline.config.camera.height << ")\n";
        // Use the actual negotiated dimensions for all downstream operations.
        pipeline.config.camera.width = caps.width;
        pipeline.config.camera.height = caps.height;
    }

    pipeline.output.open(pipeline.config.output_path, std::ios::binary | std::ios::trunc);
    if (!pipeline.output.is_open()) {
        if (error) *error = "cannot open H.264 output: " + pipeline.config.output_path;
        (void)pipeline.camera.stop(nullptr);
        return false;
    }
    if (pipeline.encoder.Init(static_cast<int>(caps.width), static_cast<int>(caps.height),
                              static_cast<int>(pipeline.config.camera.fps),
                              static_cast<int>(pipeline.config.bitrate), 264) != 0) {
        if (error) *error = "MPP H.264 encoder initialization failed";
        pipeline.output.close();
        (void)pipeline.camera.stop(nullptr);
        return false;
    }

    if (pipeline.config.ai_enable) {
        pipeline.model_identifier = pipeline.config.model_path.empty()
                                        ? default_model_path(pipeline.config.model_index)
                                        : pipeline.config.model_path;
        pipeline.model = std::make_unique<rknn_lite>(pipeline.model_identifier.c_str(),
                                                     pipeline.config.model_index % 3,
                                                     model_class_count(pipeline.config.model_index),
                                                     pipeline.config.model_index);
        if (!pipeline.model->valid()) {
            const std::string message = "RKNN model initialization failed: " + pipeline.model_identifier;
            if (pipeline.config.ai_required) {
                if (error) *error = message;
                pipeline.encoder.Release();
                pipeline.output.close();
                (void)pipeline.camera.stop(nullptr);
                return false;
            }
            std::cerr << message << "; recording continues with AI disabled\n";
            pipeline.model.reset();
        } else {
            if (!pipeline.config.events_path.empty()) {
                pipeline.events.open(pipeline.config.events_path, std::ios::out | std::ios::trunc);
                if (!pipeline.events.is_open()) {
                    std::cerr << "cannot open AI event output; continuing without JSONL: "
                              << pipeline.config.events_path << "\n";
                }
            }
            pipeline.ai_active = true;
            pipeline.ai_thread = std::thread(&Impl::ai_loop, &pipeline);
        }
    }

    const uint32_t ai_period_ms = pipeline.config.ai_fps
                                      ? (1000U / pipeline.config.ai_fps)
                                      : 1000U;
    const auto frame_period = std::chrono::milliseconds(ai_period_ms ? ai_period_ms : 1U);
    auto next_ai = std::chrono::steady_clock::now();
    const auto deadline = pipeline.config.duration_sec
                              ? pipeline.started + std::chrono::seconds(pipeline.config.duration_sec)
                              : std::chrono::steady_clock::time_point::max();
    uint64_t consecutive_capture_errors = 0;
    uint64_t consecutive_encode_errors = 0;
    std::vector<uint8_t> nv12;
    std::vector<uint8_t> packet(static_cast<size_t>(caps.width) * caps.height * 2);
    bool run_ok = true;

    while (!pipeline.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        std::string dequeue_error;
        auto frame = pipeline.camera.dequeue(&dequeue_error);
        if (!frame) {
            if (pipeline.stop_requested()) break;
            if (dequeue_error.find("timeout") != std::string::npos) continue;
            ++consecutive_capture_errors;
            pipeline.add_capture_error();
            std::cerr << dequeue_error << '\n';
            if (consecutive_capture_errors >= 5) {
                if (error) *error = "repeated V4L2 dequeue failures";
                run_ok = false;
                break;
            }
            continue;
        }
        consecutive_capture_errors = 0;
        {
            std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
            ++pipeline.counters.capture_frames;
            pipeline.counters.v4l2_inflight = pipeline.camera.in_flight();
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - pipeline.started).count();
            if (elapsed > 0.0) pipeline.counters.capture_fps = pipeline.counters.capture_frames / elapsed;
        }

        // Recorder first: AI submission is performed only after the encode
        // attempt, so an overloaded model cannot block the recording path.
        if (!copy_nv12(*frame, &nv12)) {
            std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
            ++pipeline.counters.encode_errors;
            if (++consecutive_encode_errors >= 5) {
                if (error) *error = "repeated NV12 frame layout/encode preparation failures";
                run_ok = false;
                break;
            }
        } else {
            const auto encode_start = std::chrono::steady_clock::now();
            int packet_size = static_cast<int>(packet.size());
            const int encode_result = pipeline.encoder.EncodeNV12(
                nv12.data(), static_cast<int>(caps.width), static_cast<int>(caps.height),
                static_cast<int>(caps.width), static_cast<int>(caps.width),
                static_cast<int>(nv12.size()), packet.data(), &packet_size);
            const auto encode_end = std::chrono::steady_clock::now();
            if (encode_result != 0) {
                std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                ++pipeline.counters.encode_errors;
                if (++consecutive_encode_errors >= 5) {
                    if (error) *error = "repeated MPP encode failures";
                    run_ok = false;
                    break;
                }
            } else if (packet_size > 0) {
                consecutive_encode_errors = 0;
                if (!pipeline.header_written) {
                    std::array<uint8_t, 4096> header{};
                    int header_size = static_cast<int>(header.size());
                    if (pipeline.encoder.GetHeader(header.data(), &header_size) == 0 && header_size > 0) {
                        pipeline.output.write(reinterpret_cast<const char *>(header.data()), header_size);
                    }
                    if (!pipeline.output.good()) {
                        if (error) *error = "H.264 header write failed";
                        run_ok = false;
                        break;
                    }
                    pipeline.header_written = true;
                }
                pipeline.output.write(reinterpret_cast<const char *>(packet.data()), packet_size);
                if (!pipeline.output.good()) {
                    if (error) *error = "H.264 output write failed";
                    run_ok = false;
                    break;
                }
                std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                ++pipeline.counters.encoded_frames;
                pipeline.counters.last_encode_latency_ms =
                    std::chrono::duration<double, std::milli>(encode_end - encode_start).count();
                const double elapsed = std::chrono::duration<double>(encode_end - pipeline.started).count();
                if (elapsed > 0.0) pipeline.counters.encode_fps = pipeline.counters.encoded_frames / elapsed;
            } else {
                consecutive_encode_errors = 0;
                std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                ++pipeline.counters.encode_drop;
            }
        }

        if (pipeline.ai_active) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_ai) {
                Impl::AiItem item;
                item.frame = *frame;
                item.sequence = frame->desc.sequence;
                item.timestamp_ns = frame->desc.timestamp_ns;
                bool dropped = false;
                if (pipeline.ai_queue.push_drop_oldest(std::move(item), &dropped)) {
                    std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                    if (dropped) ++pipeline.counters.ai_drop_oldest;
                } else {
                    std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                    ++pipeline.counters.ai_skip;
                }
                next_ai = now + frame_period;
            } else {
                std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                ++pipeline.counters.ai_skip;
            }
            {
                std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
                pipeline.counters.ai_queue_depth = pipeline.ai_queue.size();
            }
        }
    }

    pipeline.stop.store(true);
    pipeline.close_ai();
    pipeline.encoder.Release();
    pipeline.output.flush();
    pipeline.output.close();
    pipeline.events.flush();
    pipeline.events.close();
    const V4L2IspCamera::Stats camera_stats = pipeline.camera.stats();
    std::string stop_error;
    if (!pipeline.camera.stop(&stop_error)) {
        if (error && error->empty()) *error = stop_error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
        pipeline.counters.v4l2_inflight = 0;
        pipeline.counters.capture_drop = camera_stats.error_flags;
    }
    return run_ok && (!error || error->empty());
}

MipiPipelineStats MipiPipeline::stats() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    MipiPipelineStats snapshot = impl_->counters;
    snapshot.ai_queue_depth = impl_->ai_queue.size();
    return snapshot;
}
