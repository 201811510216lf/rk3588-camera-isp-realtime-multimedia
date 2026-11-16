#include "mipi_pipeline.h"

#include "bounded_frame_queue.h"
#include "mpp_encoder.h"
#include "rknnPool.hpp"
#include "im2d.h"
#include "RgaUtils.h"

#include <opencv2/imgproc.hpp>

#include <array>
#include <algorithm>
#include <chrono>
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

constexpr size_t kRecorderQueueCapacity = 4;
constexpr size_t kAiQueueCapacity = 2;

const char *default_model_path() { return "../../model/person_relu.rknn"; }

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
        frame.desc.width == 0 || frame.desc.height == 0 ||
        (frame.desc.width & 1U) != 0 || (frame.desc.height & 1U) != 0) {
        return false;
    }
    const uint32_t width = frame.desc.width;
    const uint32_t height = frame.desc.height;
    const uint32_t stride_y = frame.desc.stride_y ? frame.desc.stride_y : width;
    const uint32_t stride_uv = frame.desc.stride_uv ? frame.desc.stride_uv : width;
    if (stride_y < width || stride_uv < width) return false;
    const size_t required_y = static_cast<size_t>(stride_y) * height;
    const size_t required_uv = static_cast<size_t>(stride_uv) * (height / 2);
    if (frame.desc.data_length < required_y || frame.desc.data_uv_length < required_uv) {
        return false;
    }
    const size_t required = static_cast<size_t>(width) * height * 3 / 2;
    output->assign(required, 0);
    const auto *src_y = static_cast<const uint8_t *>(frame.desc.data);
    auto *dst_y = output->data();
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst_y + static_cast<size_t>(row) * width,
                    src_y + static_cast<size_t>(row) * stride_y, width);
    }
    const auto *src_uv = static_cast<const uint8_t *>(frame.desc.data_uv);
    auto *dst_uv = output->data() + static_cast<size_t>(width) * height;
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
        uint64_t capture_timestamp_ns = 0;
        uint32_t capture_timestamp_flags = 0;
    };

    explicit Impl(MipiPipelineConfig value)
        : config(std::move(value)), camera(config.camera), record_queue(kRecorderQueueCapacity),
          ai_queue(kAiQueueCapacity) {}

    MipiPipelineConfig config;
    V4L2IspCamera camera;
    MppEncoder encoder;
    std::ofstream output;
    std::ofstream events;
    std::unique_ptr<rknn_lite> model;
    std::string model_identifier;
    BoundedFrameQueue<camera::CameraFrame> record_queue;
    BoundedFrameQueue<AiItem> ai_queue;
    std::thread capture_thread;
    std::thread encode_thread;
    std::thread ai_thread;
    std::atomic<bool> stop{false};
    bool ai_active = false;
    bool header_written = false;
    mutable std::mutex stats_mutex;
    mutable std::mutex error_mutex;
    MipiPipelineStats counters;
    std::string failure;
    std::chrono::steady_clock::time_point started;

    bool stop_requested() const {
        return stop.load() || (config.external_stop && config.external_stop->load());
    }

    void fail(std::string message) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (failure.empty()) failure = std::move(message);
        stop.store(true);
    }

    void add_capture_error() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        ++counters.capture_errors;
    }

    void ai_loop() {
        AiItem item;
        while (ai_queue.pop(item)) {
            if (stop_requested()) continue;
            const auto infer_start = std::chrono::steady_clock::now();
            std::vector<uint8_t> nv12;
            cv::Mat bgr;
            const bool copied = copy_nv12(item.frame, &nv12);
            // RGA and RKNN only receive owned CPU data; they never retain the camera lease.
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
                const double elapsed = std::chrono::duration<double>(infer_end - started).count();
                if (elapsed > 0.0) counters.ai_fps = counters.ai_processed / elapsed;
            }
            if (events.is_open()) {
                const char *label = (results.count > 0) ? results.results[0].name : "";
                const float confidence = (results.count > 0) ? results.results[0].prop : 0.0f;
                const auto relative_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    infer_end - started).count();
                events << "{\"capture_timestamp_ns\":" << item.capture_timestamp_ns
                       << ",\"capture_timestamp_flags\":" << item.capture_timestamp_flags
                       << ",\"timestamp_monotonic_ns\":" << relative_ns
                       << ",\"frame_sequence\":" << item.sequence
                       << ",\"model\":\"" << json_escape(model_identifier.c_str())
                       << "\",\"label\":\"" << json_escape(label)
                       << "\",\"confidence\":" << std::fixed << std::setprecision(6)
                       << confidence << ",\"infer_ms\":" << infer_ms << "}\n";
                events.flush();
            }
        }
    }

    void encode_loop() {
        camera::CameraFrame frame;
        std::vector<uint8_t> nv12;
        std::vector<uint8_t> packet;
        while (record_queue.pop(frame)) {
            if (!copy_nv12(frame, &nv12)) {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.encode_errors;
                continue;
            }
            packet.resize(static_cast<size_t>(config.camera.width) * config.camera.height * 2);
            int packet_size = static_cast<int>(packet.size());
            const auto encode_start = std::chrono::steady_clock::now();
            const int result = encoder.EncodeNV12(nv12.data(), static_cast<int>(config.camera.width),
                                                  static_cast<int>(config.camera.height),
                                                  static_cast<int>(config.camera.width),
                                                  static_cast<int>(config.camera.width),
                                                  static_cast<int>(nv12.size()), packet.data(), &packet_size);
            const auto encode_end = std::chrono::steady_clock::now();
            frame = camera::CameraFrame{};
            if (result != 0) {
                {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    ++counters.encode_errors;
                }
                fail("MPP encode failed; input buffer completion was not reported");
                break;
            }
            if (packet_size <= 0) {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.encode_drop;
                continue;
            }
            if (!header_written) {
                std::array<uint8_t, 4096> header{};
                int header_size = static_cast<int>(header.size());
                if (encoder.GetHeader(header.data(), &header_size) != 0 || header_size <= 0) {
                    fail("MPP H.264 header is unavailable; refusing to write undecodable output");
                    break;
                }
                output.write(reinterpret_cast<const char *>(header.data()), header_size);
                if (!output.good()) {
                    fail("H.264 header write failed");
                    break;
                }
                header_written = true;
            }
            output.write(reinterpret_cast<const char *>(packet.data()), packet_size);
            if (!output.good()) {
                fail("H.264 output write failed");
                break;
            }
            std::lock_guard<std::mutex> lock(stats_mutex);
            ++counters.encoded_frames;
            counters.last_encode_latency_ms =
                std::chrono::duration<double, std::milli>(encode_end - encode_start).count();
            const double elapsed = std::chrono::duration<double>(encode_end - started).count();
            if (elapsed > 0.0) counters.encode_fps = counters.encoded_frames / elapsed;
        }
    }

    void capture_loop(std::chrono::steady_clock::time_point deadline) {
        uint64_t consecutive_errors = 0;
        auto next_ai = std::chrono::steady_clock::now();
        const auto ai_period = std::chrono::milliseconds(
            config.ai_fps ? std::max<uint32_t>(1U, 1000U / config.ai_fps) : 1000U);
        while (!stop_requested() && std::chrono::steady_clock::now() < deadline) {
            std::string dequeue_error;
            auto frame = camera.dequeue(&dequeue_error);
            if (!frame) {
                if (stop_requested()) break;
                if (dequeue_error.find("timeout") != std::string::npos) continue;
                ++consecutive_errors;
                add_capture_error();
                if (consecutive_errors >= 5) {
                    fail("repeated V4L2 dequeue failures: " + dequeue_error);
                    break;
                }
                continue;
            }
            consecutive_errors = 0;
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.capture_frames;
                counters.v4l2_inflight = camera.in_flight();
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                if (elapsed > 0.0) counters.capture_fps = counters.capture_frames / elapsed;
            }
            bool record_dropped = false;
            if (record_queue.push_drop_oldest(*frame, &record_dropped)) {
                if (record_dropped) {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    ++counters.record_queue_drop;
                    ++counters.capture_drop;
                }
            } else {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.capture_drop;
            }
            if (ai_active) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_ai) {
                    AiItem item;
                    item.frame = *frame;
                    item.sequence = frame->desc.sequence;
                    item.capture_timestamp_ns = frame->desc.timestamp_ns;
                    item.capture_timestamp_flags = frame->desc.timestamp_flags;
                    bool ai_dropped = false;
                    if (ai_queue.push_drop_oldest(std::move(item), &ai_dropped)) {
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        if (ai_dropped) ++counters.ai_drop_oldest;
                    } else {
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        ++counters.ai_skip;
                    }
                    next_ai = now + ai_period;
                } else {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    ++counters.ai_skip;
                }
                std::lock_guard<std::mutex> lock(stats_mutex);
                counters.ai_queue_depth = ai_queue.size();
            }
        }
        record_queue.close();
        ai_queue.close();
    }

    void close_queues() {
        record_queue.close();
        ai_queue.close();
    }
};

MipiPipeline::MipiPipeline(MipiPipelineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MipiPipeline::~MipiPipeline() {
    request_stop();
    if (impl_) {
        if (impl_->capture_thread.joinable()) impl_->capture_thread.join();
        impl_->close_queues();
        if (impl_->encode_thread.joinable()) impl_->encode_thread.join();
        if (impl_->ai_thread.joinable()) impl_->ai_thread.join();
        std::string ignored;
        (void)impl_->camera.stop(&ignored);
    }
}

void MipiPipeline::request_stop() {
    if (impl_) {
        impl_->stop.store(true);
        impl_->close_queues();
    }
}

bool MipiPipeline::run(std::string *error) {
    if (error) error->clear();
    if (!impl_) {
        if (error) *error = "pipeline implementation is unavailable";
        return false;
    }
    Impl &pipeline = *impl_;
    if (pipeline.capture_thread.joinable() || pipeline.encode_thread.joinable() ||
        pipeline.ai_thread.joinable() || pipeline.camera.is_open()) {
        if (error) *error = "MipiPipeline instances are single-use";
        return false;
    }
    pipeline.started = std::chrono::steady_clock::now();
    pipeline.stop.store(false);

    std::string camera_error;
    if (!pipeline.camera.open(&camera_error) || !pipeline.camera.start(&camera_error)) {
        if (error) *error = camera_error;
        (void)pipeline.camera.stop(nullptr);
        return false;
    }
    const auto caps = pipeline.camera.capabilities();
    if (caps.width == 0 || caps.height == 0 || caps.fps == 0) {
        if (error) *error = "camera did not report a valid negotiated size/fps";
        (void)pipeline.camera.stop(nullptr);
        return false;
    }
    pipeline.config.camera.width = caps.width;
    pipeline.config.camera.height = caps.height;
    pipeline.config.camera.fps = caps.fps;
    pipeline.output.open(pipeline.config.output_path, std::ios::binary | std::ios::trunc);
    if (!pipeline.output.is_open()) {
        if (error) *error = "cannot open H.264 output: " + pipeline.config.output_path;
        (void)pipeline.camera.stop(nullptr);
        return false;
    }
    if (pipeline.encoder.Init(static_cast<int>(caps.width), static_cast<int>(caps.height),
                              static_cast<int>(caps.fps), static_cast<int>(pipeline.config.bitrate), 264) != 0) {
        if (error) *error = "MPP H.264 encoder initialization failed";
        pipeline.output.close();
        (void)pipeline.camera.stop(nullptr);
        return false;
    }

    if (pipeline.config.ai_enable) {
        pipeline.model_identifier = pipeline.config.model_path.empty() ? default_model_path()
                                                                         : pipeline.config.model_path;
        pipeline.model = std::make_unique<rknn_lite>(pipeline.model_identifier.c_str(), 0, 1, 0);
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
            pipeline.ai_active = true;
            if (!pipeline.config.events_path.empty()) {
                pipeline.events.open(pipeline.config.events_path, std::ios::out | std::ios::trunc);
                if (!pipeline.events.is_open()) {
                    std::cerr << "cannot open AI event output; continuing without JSONL: "
                              << pipeline.config.events_path << "\n";
                }
            }
        }
    }

    const auto deadline = pipeline.config.duration_sec
                              ? pipeline.started + std::chrono::seconds(pipeline.config.duration_sec)
                              : std::chrono::steady_clock::time_point::max();
    pipeline.encode_thread = std::thread(&Impl::encode_loop, &pipeline);
    if (pipeline.ai_active) pipeline.ai_thread = std::thread(&Impl::ai_loop, &pipeline);
    pipeline.capture_thread = std::thread(&Impl::capture_loop, &pipeline, deadline);

    pipeline.capture_thread.join();
    pipeline.close_queues();
    if (pipeline.encode_thread.joinable()) pipeline.encode_thread.join();
    if (pipeline.ai_thread.joinable()) pipeline.ai_thread.join();
    pipeline.stop.store(true);
    pipeline.encoder.Release();
    pipeline.output.flush();
    pipeline.output.close();
    pipeline.events.flush();
    pipeline.events.close();
    const auto camera_stats = pipeline.camera.stats();
    std::string stop_error;
    if (!pipeline.camera.stop(&stop_error)) {
        if (error) *error = stop_error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(pipeline.stats_mutex);
        pipeline.counters.v4l2_inflight = 0;
        pipeline.counters.capture_drop += camera_stats.dqbuf_errors;
    }
    {
        std::lock_guard<std::mutex> lock(pipeline.error_mutex);
        if (error && error->empty()) *error = pipeline.failure;
    }
    std::lock_guard<std::mutex> lock(pipeline.error_mutex);
    return pipeline.failure.empty();
}

MipiPipelineStats MipiPipeline::stats() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    MipiPipelineStats snapshot = impl_->counters;
    snapshot.ai_queue_depth = impl_->ai_queue.size();
    return snapshot;
}
