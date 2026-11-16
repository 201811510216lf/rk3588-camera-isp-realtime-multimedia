#include "dual_camera_pipeline.h"

#include "bounded_frame_queue.h"
#include "camera_frame.h"
#include "composite_processor.h"
#include "encoded_packet_fanout.h"
#include "frame_pairer.h"
#include "rknnPool.hpp"
#include "v4l2_isp_camera.h"
#include "mpp_encoder.h"

#include <opencv2/imgproc.hpp>
#include "im2d.h"
#include "RgaUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/wait.h>
#endif

namespace {

constexpr size_t kPairQueueDepth = 4;
constexpr size_t kAiQueueDepth = 2;

uint64_t steady_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool copy_nv12(const camera::CameraFrame &frame, std::vector<uint8_t> *out) {
    if (!out || !frame.valid() || !frame.desc.data || frame.desc.width == 0 || frame.desc.height == 0 ||
        (frame.desc.width & 1U) != 0 || (frame.desc.height & 1U) != 0) return false;
    const uint32_t width = frame.desc.width;
    const uint32_t height = frame.desc.height;
    const uint32_t stride_y = frame.desc.stride_y ? frame.desc.stride_y : width;
    const uint32_t stride_uv = frame.desc.stride_uv ? frame.desc.stride_uv : width;
    if (stride_y < width || stride_uv < width) return false;
    const size_t packed_y = static_cast<size_t>(width) * height;
    const size_t packed_uv = static_cast<size_t>(width) * (height / 2U);
    out->assign(packed_y + packed_uv, 0);
    const auto *src_y = static_cast<const uint8_t *>(frame.desc.data);
    for (uint32_t row = 0; row < height; ++row) {
        if (static_cast<size_t>(row) * stride_y + width > frame.desc.data_length) return false;
        std::memcpy(out->data() + static_cast<size_t>(row) * width,
                    src_y + static_cast<size_t>(row) * stride_y, width);
    }
    const auto *src_uv = frame.desc.data_uv;
    if (!src_uv) src_uv = src_y + static_cast<size_t>(stride_y) * height;
    if (!frame.desc.data_uv && frame.desc.data_length < static_cast<size_t>(stride_y) * height) return false;
    const size_t uv_length = frame.desc.data_uv ? frame.desc.data_uv_length : frame.desc.data_length - static_cast<size_t>(stride_y) * height;
    for (uint32_t row = 0; row < height / 2U; ++row) {
        if (static_cast<size_t>(row) * stride_uv + width > uv_length) return false;
        std::memcpy(out->data() + packed_y + static_cast<size_t>(row) * width,
                    src_uv + static_cast<size_t>(row) * stride_uv, width);
    }
    return true;
}

bool nv12_to_bgr_rga(const std::vector<uint8_t> &nv12, uint32_t width, uint32_t height, cv::Mat *bgr) {
    if (!bgr || nv12.size() < static_cast<size_t>(width) * height * 3U / 2U) return false;
    bgr->create(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
    rga_buffer_t src = wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(nv12.data()), width, height, width, height, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(bgr->data, width, height, width, height, RK_FORMAT_BGR_888);
    if (imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888, IM_COLOR_SPACE_DEFAULT) == IM_STATUS_SUCCESS) return true;
    cv::Mat yuv(static_cast<int>(height + height / 2U), static_cast<int>(width), CV_8UC1, const_cast<uint8_t *>(nv12.data()));
    cv::cvtColor(yuv, *bgr, cv::COLOR_YUV2BGR_NV12);
    return !bgr->empty();
}

bool bgr_to_nv12(const cv::Mat &bgr, std::vector<uint8_t> *nv12) {
    if (!nv12 || bgr.empty() || bgr.type() != CV_8UC3 || (bgr.cols & 1) || (bgr.rows & 1)) return false;
    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    const size_t y_size = static_cast<size_t>(bgr.cols) * bgr.rows;
    const size_t uv_size = y_size / 2U;
    nv12->assign(y_size + uv_size, 0);
    std::memcpy(nv12->data(), i420.data, y_size);
    const uint8_t *u = i420.data + y_size;
    const uint8_t *v = u + y_size / 4U;
    uint8_t *uv = nv12->data() + y_size;
    for (size_t i = 0; i < y_size / 4U; ++i) { uv[2U * i] = u[i]; uv[2U * i + 1U] = v[i]; }
    return true;
}

std::string shell_quote(const std::string &value) {
    std::string out("'");
    for (char c : value) { if (c == '\'') out += "'\\''"; else out.push_back(c); }
    out.push_back('\'');
    return out;
}

bool has_h264_idr(const uint8_t *data, size_t size) {
    if (!data) return false;
    for (size_t i = 0; i + 4 < size; ++i) {
        size_t nal = i;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) nal = i + 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) nal = i + 4;
        else continue;
        if (nal < size && (data[nal] & 0x1FU) == 5U) return true;
    }
    return false;
}

class FfmpegPipe {
public:
    ~FfmpegPipe() { close(); }
    bool open_rtmp(const std::string &url, uint32_t fps) {
#ifdef __linux__
        command_ = "ffmpeg -loglevel error -framerate " + std::to_string(fps) + " -f h264 -i - -c copy -f flv " + shell_quote(url);
        pipe_ = popen(command_.c_str(), "w");
        return pipe_ != nullptr;
#else
        (void)url; (void)fps; return false;
#endif
    }
    bool open_mp4(const std::string &dir, uint32_t segment_sec, uint32_t fps) {
#ifdef __linux__
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return false;
        command_ = "ffmpeg -loglevel error -framerate " + std::to_string(fps) + " -f h264 -i - -c copy -f segment -segment_time " + std::to_string(segment_sec) +
                   " -reset_timestamps 1 -strftime 1 -segment_format mp4 " + shell_quote((std::filesystem::path(dir) / "dual-%Y%m%d-%H%M%S.mp4").string());
        pipe_ = popen(command_.c_str(), "w");
        return pipe_ != nullptr;
#else
        (void)dir; (void)segment_sec; (void)fps; return false;
#endif
    }
    bool write(const uint8_t *data, size_t size) {
        if (!data || size == 0 || !pipe_) return false;
        if (std::fwrite(data, 1, size, pipe_) == size) return true;
#ifdef __linux__
        // A broken RTMP/ffmpeg pipe is recoverable without touching the
        // encoder. Re-open the exact command once and retry the current
        // packet; callers still observe false if the reconnect also fails.
        (void)close();
        pipe_ = popen(command_.c_str(), "w");
        return pipe_ && std::fwrite(data, 1, size, pipe_) == size;
#else
        return false;
#endif
    }
    bool close() {
        if (!pipe_) return true;
#ifdef __linux__
        const int status = pclose(pipe_);
        pipe_ = nullptr;
        return status == 0;
#else
        pipe_ = nullptr; return true;
#endif
    }
private:
    FILE *pipe_ = nullptr;
    std::string command_;
};

} // namespace

struct DualCameraPipeline::Impl {
    struct AiItem { camera::CameraFrame frame; uint64_t sequence = 0; uint64_t timestamp_ns = 0; };
    struct AiSnapshot { uint64_t sequence = 0; uint64_t timestamp_ns = 0; std::vector<DetectionBox> boxes; };

    struct AiQueues {
        std::unique_ptr<BoundedFrameQueue<AiItem>> values[2];
        AiQueues() : values{std::make_unique<BoundedFrameQueue<AiItem>>(kAiQueueDepth), std::make_unique<BoundedFrameQueue<AiItem>>(kAiQueueDepth)} {}
    };

    explicit Impl(DualCameraPipelineConfig value)
        : config(std::move(value)), pairer(FramePairerConfig{}), pair_queue(kPairQueueDepth), ai_queue() {}

    DualCameraPipelineConfig config;
    V4L2IspCamera camera[2];
    FramePairer pairer;
    BoundedFrameQueue<FramePair> pair_queue;
    AiQueues ai_queue;
    MppEncoder encoder;
    std::unique_ptr<rknn_lite> models[2];
    AiSnapshot ai_snapshot[2];
    std::mutex ai_mutex;
    std::condition_variable ai_cv;
    std::ofstream raw_output;
    std::ofstream events;
    std::mutex events_mutex;
    std::ofstream sync_diag;
    std::mutex sync_diag_mutex;
    std::unique_ptr<FfmpegPipe> rtmp_sink;
    std::unique_ptr<FfmpegPipe> mp4_sink;
    EncodedPacketDistributor distributor;
    CompositeLayout layout;
    std::thread capture_threads[2];
    std::thread ai_threads[2];
    std::thread encode_thread;
    std::atomic<bool> stop{false};
    std::mutex stats_mutex;
    std::mutex error_mutex;
    DualCameraPipelineStats counters;
    std::string failure;
    std::chrono::steady_clock::time_point started;
    bool run_started = false;

    bool stopping() const { return stop.load() || (config.external_stop && config.external_stop->load()); }
    void fail(const std::string &message) { std::lock_guard<std::mutex> lock(error_mutex); if (failure.empty()) failure = message; stop.store(true); }

    void queue_pairs(std::vector<FramePair> pairs) {
        for (FramePair &pair : pairs) {
            if (sync_diag.is_open()) {
                std::lock_guard<std::mutex> lock(sync_diag_mutex);
                sync_diag << pair.pair_id << ',' << pair.cam0.sequence << ',' << pair.cam1.sequence << ','
                          << pair.cam0.timestamp_ns << ',' << pair.cam1.timestamp_ns << ',' << pair.delta_ns << ','
                          << (pair.clock_domain_verified ? "verified" : "unverified") << ','
                          << (pair.is_stale() ? "stale" : "matched") << '\n';
            }
            bool dropped = false;
            if (!pair_queue.push_drop_oldest(std::move(pair), &dropped)) { std::lock_guard<std::mutex> lock(stats_mutex); ++counters.pair_drops; }
            else if (dropped) { std::lock_guard<std::mutex> lock(stats_mutex); ++counters.pair_drops; }
        }
    }

    void capture_loop(uint32_t id, std::chrono::steady_clock::time_point deadline) {
        uint64_t errors = 0;
        auto next_ai = std::chrono::steady_clock::now();
        auto next_stats = std::chrono::steady_clock::now() + std::chrono::seconds(std::max<uint32_t>(1U, config.stats_interval_sec));
        const auto ai_period = std::chrono::milliseconds(config.ai_fps ? std::max<uint32_t>(1U, 1000U / config.ai_fps) : 1000U);
        while (!stopping() && std::chrono::steady_clock::now() < deadline) {
            std::string error;
            auto frame = camera[id].dequeue(&error);
            if (!frame) {
                if (stopping()) break;
                if (error.find("timeout") != std::string::npos) { queue_pairs(pairer.tick(steady_now_ns())); continue; }
                ++errors; { std::lock_guard<std::mutex> lock(stats_mutex); if (id == 0) ++counters.camera0_errors; else ++counters.camera1_errors; }
                if (errors >= 5) { fail("camera" + std::to_string(id) + " repeated V4L2 dequeue failures: " + error); break; }
                continue;
            }
            errors = 0;
            { std::lock_guard<std::mutex> lock(stats_mutex); if (id == 0) ++counters.camera0_frames; else ++counters.camera1_frames; }
            FrameSample sample;
            sample.camera_id = id;
            sample.sequence = frame->desc.sequence;
            sample.timestamp_ns = frame->desc.timestamp_ns;
            sample.clock_domain = static_cast<FrameClockDomain>(frame->desc.clock_domain);
            sample.frame = *frame;
            queue_pairs(pairer.push(std::move(sample), steady_now_ns()));
            if (models[id] && config.ai_mode != AppOptions::AiMode::Off) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_ai) {
                    AiItem item; item.frame = *frame; item.sequence = frame->desc.sequence; item.timestamp_ns = frame->desc.timestamp_ns;
                    bool dropped = false;
                    if (!ai_queue.values[id]->push_drop_oldest(std::move(item), &dropped)) { std::lock_guard<std::mutex> lock(stats_mutex); if (id == 0) ++counters.ai0_drops; else ++counters.ai1_drops; }
                    else if (dropped) { std::lock_guard<std::mutex> lock(stats_mutex); if (id == 0) ++counters.ai0_drops; else ++counters.ai1_drops; }
                    next_ai = now + ai_period;
                }
            }
            if (id == 0 && std::chrono::steady_clock::now() >= next_stats) {
                const FramePairerStats pair_stats = pairer.stats();
                const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                std::lock_guard<std::mutex> lock(stats_mutex);
                std::cerr << "stats elapsed_sec=" << std::fixed << std::setprecision(1) << elapsed
                          << " cam0=" << counters.camera0_frames << " cam1=" << counters.camera1_frames
                          << " pairs=" << pair_stats.matched_pairs << " pair_drops=" << pair_stats.pair_drops
                          << " delta_p95_ms=" << pair_stats.p95_ms() << '\n';
                next_stats = std::chrono::steady_clock::now() + std::chrono::seconds(std::max<uint32_t>(1U, config.stats_interval_sec));
            }
        }
        queue_pairs(pairer.tick(steady_now_ns() + static_cast<uint64_t>(config.sync_max_wait_ms) * 1'000'000ULL));
    }

    void ai_loop(uint32_t id) {
        AiItem item;
        while (ai_queue.values[id]->pop(item)) {
            if (stopping()) continue;
            const auto begin = std::chrono::steady_clock::now();
            std::vector<uint8_t> nv12; cv::Mat bgr;
            if (!copy_nv12(item.frame, &nv12) || !nv12_to_bgr_rga(nv12, config.input_width, config.input_height, &bgr)) { continue; }
            item.frame = camera::CameraFrame{};
            if (config.simulate_ai_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(config.simulate_ai_ms));
            detect_result_group_t group{};
            int result = 0;
            if (models[id]) { models[id]->ori_img = std::move(bgr); result = models[id]->interf(group); }
            if (result < 0) continue;
            AiSnapshot snapshot; snapshot.sequence = item.sequence; snapshot.timestamp_ns = item.timestamp_ns;
            for (int i = 0; i < group.count; ++i) {
                const auto &d = group.results[i];
                DetectionBox box; box.box = {static_cast<float>(d.box.left), static_cast<float>(d.box.top), static_cast<float>(d.box.right - d.box.left), static_cast<float>(d.box.bottom - d.box.top)};
                box.confidence = d.prop; box.label = d.name; box.class_id = -1; snapshot.boxes.push_back(std::move(box));
            }
            { std::lock_guard<std::mutex> lock(ai_mutex); ai_snapshot[id] = snapshot; }
            ai_cv.notify_all();
            const auto end = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(stats_mutex);
            if (id == 0) { ++counters.ai0_processed; counters.ai0_fps = counters.ai0_processed / std::max(0.001, std::chrono::duration<double>(end - started).count()); }
            else { ++counters.ai1_processed; counters.ai1_fps = counters.ai1_processed / std::max(0.001, std::chrono::duration<double>(end - started).count()); }
            if (events.is_open()) {
                std::lock_guard<std::mutex> lock(events_mutex);
                events << "{\"camera_id\":" << id << ",\"sequence\":" << item.sequence << ",\"capture_timestamp_ns\":" << item.timestamp_ns << ",\"infer_ms\":" << std::fixed << std::setprecision(3) << std::chrono::duration<double, std::milli>(end - begin).count() << ",\"boxes\":" << group.count << "}\n";
                events.flush();
            }
        }
    }

    bool compose_pair(const FramePair &pair, std::vector<uint8_t> *output) {
        std::vector<uint8_t> nv120, nv121;
        if (!copy_nv12(pair.cam0.frame, &nv120) || !copy_nv12(pair.cam1.frame, &nv121)) return false;
        cv::Mat bgr0, bgr1;
        if (!nv12_to_bgr_rga(nv120, config.input_width, config.input_height, &bgr0) || !nv12_to_bgr_rga(nv121, config.input_width, config.input_height, &bgr1)) return false;
        cv::Mat canvas(static_cast<int>(config.output_height), static_cast<int>(config.output_width), CV_8UC3, cv::Scalar(16, 16, 16));
        auto draw_view = [&](const cv::Mat &source, const CompositeView &view, uint32_t id) {
            cv::Mat scaled(static_cast<int>(view.dst.height), static_cast<int>(view.dst.width), CV_8UC3);
            rga_buffer_t rga_src = wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(source.data), source.cols, source.rows, source.cols, source.rows, RK_FORMAT_BGR_888);
            rga_buffer_t rga_dst = wrapbuffer_virtualaddr_t(scaled.data, scaled.cols, scaled.rows, scaled.cols, scaled.rows, RK_FORMAT_BGR_888);
            const double scale_x = static_cast<double>(scaled.cols) / static_cast<double>(source.cols);
            const double scale_y = static_cast<double>(scaled.rows) / static_cast<double>(source.rows);
            if (imresize(rga_src, rga_dst, scale_x, scale_y, IM_INTERP_DEFAULT) != IM_STATUS_SUCCESS) cv::resize(source, scaled, scaled.size());
            const cv::Rect roi(static_cast<int>(view.dst.x), static_cast<int>(view.dst.y), scaled.cols, scaled.rows);
            scaled.copyTo(canvas(roi));
            const uint64_t frame_ts = id == 0 ? pair.cam0.timestamp_ns : pair.cam1.timestamp_ns;
            const uint64_t frame_seq = id == 0 ? pair.cam0.sequence : pair.cam1.sequence;
            if (config.ai_mode == AppOptions::AiMode::Strict) {
                std::unique_lock<std::mutex> lock(ai_mutex);
                ai_cv.wait_for(lock, std::chrono::milliseconds(config.sync_max_wait_ms), [&] {
                    return stopping() || ai_snapshot[id].sequence == frame_seq;
                });
            }
            AiSnapshot snapshot; { std::lock_guard<std::mutex> lock(ai_mutex); snapshot = ai_snapshot[id]; }
            const bool timestamp_usable = snapshot.timestamp_ns != 0 && snapshot.timestamp_ns <= frame_ts;
            const bool sequence_usable = config.ai_mode != AppOptions::AiMode::Strict || snapshot.sequence == frame_seq;
            const bool usable = config.ai_mode != AppOptions::AiMode::Off && timestamp_usable && sequence_usable;
            for (const DetectionBox &box : snapshot.boxes) {
                if (!usable) break;
                const RectF clipped = CompositeProcessor::clip_box(CompositeProcessor::map_box(view, box.box), config.output_width, config.output_height);
                if (clipped.width <= 0.0F || clipped.height <= 0.0F) continue;
                cv::rectangle(canvas, cv::Rect(static_cast<int>(clipped.x), static_cast<int>(clipped.y), std::max(1, static_cast<int>(clipped.width)), std::max(1, static_cast<int>(clipped.height))), id == 0 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);
                std::ostringstream label; label << box.label << " " << std::fixed << std::setprecision(2) << box.confidence;
                cv::putText(canvas, label.str(), cv::Point(static_cast<int>(clipped.x), std::max(12, static_cast<int>(clipped.y) - 3)), cv::FONT_HERSHEY_SIMPLEX, 0.45, id == 0 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 1);
            }
            const std::string ai_status = config.ai_mode == AppOptions::AiMode::Off ? " AI_OFF" : (usable ? " AI_OK" : " AI_WAIT");
            const uint64_t age_ns = (snapshot.timestamp_ns != 0 && frame_ts >= snapshot.timestamp_ns) ? frame_ts - snapshot.timestamp_ns : 0;
            std::ostringstream camera_label; camera_label << "CAM" << id << (pair.is_stale() ? " STALE" : "") << ai_status;
            if (snapshot.timestamp_ns != 0) camera_label << " age_ms=" << std::fixed << std::setprecision(1) << static_cast<double>(age_ns) / 1'000'000.0;
            cv::putText(canvas, camera_label.str(), cv::Point(static_cast<int>(view.dst.x) + 8, static_cast<int>(view.dst.y) + 24), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        };
        draw_view(bgr0, layout.cam0, 0); draw_view(bgr1, layout.cam1, 1);
        std::ostringstream sync; sync << "delta_ms=" << std::fixed << std::setprecision(3) << static_cast<double>(pair.delta_ns) / 1'000'000.0 << (pair.clock_domain_verified ? " SYNC" : " UNVERIFIED");
        cv::putText(canvas, sync.str(), cv::Point(12, static_cast<int>(config.output_height) - 12), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 0), 1);
        return bgr_to_nv12(canvas, output);
    }

    void encode_loop() {
        FramePair pair; std::vector<uint8_t> composite; std::vector<uint8_t> packet(static_cast<size_t>(config.output_width) * config.output_height * 2U);
        uint64_t pts = 0;
        while (pair_queue.pop(pair)) {
            if (!compose_pair(pair, &composite)) { std::lock_guard<std::mutex> lock(stats_mutex); ++counters.encode_errors; continue; }
            int packet_size = static_cast<int>(packet.size());
            const int result = encoder.EncodeNV12(composite.data(), static_cast<int>(config.output_width), static_cast<int>(config.output_height), static_cast<int>(config.output_width), static_cast<int>(config.output_width), static_cast<int>(composite.size()), packet.data(), &packet_size);
            if (result != 0 || packet_size <= 0) { std::lock_guard<std::mutex> lock(stats_mutex); ++counters.encode_errors; fail("MPP H.264 encode failed or returned no packet"); break; }
            auto bytes = std::make_shared<const std::vector<uint8_t>>(packet.begin(), packet.begin() + packet_size);
            EncodedPacket encoded{bytes, static_cast<int64_t>(pts), static_cast<int64_t>(pts), has_h264_idr(packet.data(), static_cast<size_t>(packet_size))};
            if (!distributor.publish(encoded)) { /* individual sink failures are isolated */ }
            ++pts;
            { std::lock_guard<std::mutex> lock(stats_mutex); ++counters.encoded_frames; counters.pair_frames = counters.encoded_frames; const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); counters.encode_fps = counters.encoded_frames / std::max(0.001, elapsed); counters.pair_fps = counters.encode_fps; }
        }
    }
};

DualCameraPipeline::DualCameraPipeline(DualCameraPipelineConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
DualCameraPipeline::~DualCameraPipeline() { request_stop(); if (!impl_) return; for (auto &thread : impl_->capture_threads) if (thread.joinable()) thread.join(); impl_->pair_queue.close(); for (auto &queue : impl_->ai_queue.values) queue->close(); for (auto &thread : impl_->ai_threads) if (thread.joinable()) thread.join(); if (impl_->encode_thread.joinable()) impl_->encode_thread.join(); for (auto &cam : impl_->camera) { std::string ignored; (void)cam.stop(&ignored); } }
void DualCameraPipeline::request_stop() { if (impl_) { impl_->stop.store(true); impl_->pair_queue.close(); for (auto &queue : impl_->ai_queue.values) queue->close(); } }

bool DualCameraPipeline::run(std::string *error) {
    if (error) error->clear(); if (!impl_) { if (error) *error = "pipeline implementation unavailable"; return false; }
    Impl &p = *impl_;
    if (p.run_started) { if (error) *error = "DualCameraPipeline instances are single-use"; return false; }
    p.run_started = true;
    if (p.config.sync_mode == AppOptions::SyncMode::Hardware) { if (error) *error = "hardware sync requested but no verified Sensor FSIN/trigger BSP backend is installed"; return false; }
    FramePairerConfig pair_cfg; pair_cfg.tolerance_ns = static_cast<uint64_t>(p.config.sync_tolerance_ms) * 1'000'000ULL; pair_cfg.max_wait_ns = static_cast<uint64_t>(p.config.sync_max_wait_ms) * 1'000'000ULL; pair_cfg.policy = p.config.sync_policy == AppOptions::SyncPolicy::HoldLast ? FramePairPolicy::HoldLast : FramePairPolicy::DropOlder; p.pairer.configure(pair_cfg);
    p.started = std::chrono::steady_clock::now(); p.stop.store(false);
    V4L2IspCamera::Config cam_cfg[2];
    for (int i = 0; i < 2; ++i) {
        cam_cfg[i].device = i == 0 ? p.config.camera0 : p.config.camera1;
        cam_cfg[i].width = p.config.input_width; cam_cfg[i].height = p.config.input_height;
        cam_cfg[i].fps = p.config.fps; cam_cfg[i].poll_timeout_ms = p.config.v4l2_timeout_ms;
        std::string camera_error;
        if (!p.camera[i].set_config(cam_cfg[i], &camera_error) || !p.camera[i].open(&camera_error) || !p.camera[i].start(&camera_error)) {
            if (error) *error = camera_error;
            for (auto &cam : p.camera) { std::string ignored; (void)cam.stop(&ignored); }
            return false;
        }
    }
    const auto caps0 = p.camera[0].capabilities(); const auto caps1 = p.camera[1].capabilities(); if (caps0.width != caps1.width || caps0.height != caps1.height || caps0.fps != caps1.fps || caps0.width == 0 || caps0.height == 0 || caps0.fps == 0) { if (error) *error = "camera negotiation mismatch or invalid dimensions/fps"; return false; }
    p.config.input_width = caps0.width; p.config.input_height = caps0.height; p.config.fps = caps0.fps;
    std::string layout_error; if (!CompositeProcessor::make_side_by_side(p.config.input_width, p.config.input_height, p.config.output_width, p.config.output_height, &p.layout, &layout_error)) { if (error) *error = layout_error; return false; }
    if (!p.config.no_record) { p.raw_output.open(p.config.output_path, std::ios::binary | std::ios::trunc); if (!p.raw_output && p.config.record == false && p.config.rtmp_url.empty()) { if (error) *error = "cannot open raw H.264 output: " + p.config.output_path; return false; } }
    if (!p.config.rtmp_url.empty() && !p.config.no_stream) { p.rtmp_sink = std::make_unique<FfmpegPipe>(); if (!p.rtmp_sink->open_rtmp(p.config.rtmp_url, p.config.fps)) { if (error) *error = "cannot start ffmpeg RTMP publisher"; return false; } }
    if (p.config.record && !p.config.no_record) { p.mp4_sink = std::make_unique<FfmpegPipe>(); if (!p.mp4_sink->open_mp4(p.config.output_dir, p.config.segment_sec, p.config.fps)) { if (error) *error = "cannot start ffmpeg segmented MP4 writer"; return false; } }
    p.distributor.add_sink("raw-h264", [&p](const EncodedPacket &packet) {
        if (!p.raw_output.is_open()) return true;
        p.raw_output.write(reinterpret_cast<const char *>(packet.bytes->data()), static_cast<std::streamsize>(packet.bytes->size()));
        return p.raw_output.good();
    });
    if (p.rtmp_sink) p.distributor.add_sink("rtmp", [&p](const EncodedPacket &packet) {
        const bool ok = p.rtmp_sink->write(packet.bytes->data(), packet.bytes->size());
        if (!ok) { std::lock_guard<std::mutex> lock(p.stats_mutex); ++p.counters.rtmp_errors; }
        return ok;
    });
    if (p.mp4_sink) p.distributor.add_sink("mp4", [&p](const EncodedPacket &packet) {
        const bool ok = p.mp4_sink->write(packet.bytes->data(), packet.bytes->size());
        if (!ok) { std::lock_guard<std::mutex> lock(p.stats_mutex); ++p.counters.mp4_errors; }
        return ok;
    });
    if (p.encoder.Init(static_cast<int>(p.config.output_width), static_cast<int>(p.config.output_height), static_cast<int>(p.config.fps), static_cast<int>(p.config.bitrate), 264) != 0) { if (error) *error = "MPP encoder initialization failed"; return false; }
    std::array<uint8_t, 8192> header{}; int header_size = static_cast<int>(header.size());
    if (p.encoder.GetHeader(header.data(), &header_size) != 0 || header_size <= 0) { if (error) *error = "MPP H.264 SPS/PPS header unavailable"; return false; }
    if (p.raw_output.is_open()) p.raw_output.write(reinterpret_cast<const char *>(header.data()), header_size);
    if (p.rtmp_sink) (void)p.rtmp_sink->write(header.data(), static_cast<size_t>(header_size));
    if (p.mp4_sink) (void)p.mp4_sink->write(header.data(), static_cast<size_t>(header_size));
    if (p.raw_output.is_open() && !p.raw_output.good()) { if (error) *error = "H.264 SPS/PPS header write failed"; return false; }
    if (p.config.ai_mode != AppOptions::AiMode::Off) {
        const std::string paths[2] = {p.config.model0_path, p.config.model1_path};
        const int cores[2] = {p.config.npu_core0, p.config.npu_core1};
        for (int i = 0; i < 2; ++i) {
            if (!paths[i].empty()) {
                p.models[i] = std::make_unique<rknn_lite>(paths[i].c_str(), cores[i], 1, i);
            }
            if (!p.models[i] || !p.models[i]->valid()) {
                p.models[i].reset();
                if (p.config.ai_required) {
                    if (error) *error = "RKNN model initialization failed for camera" + std::to_string(i);
                    return false;
                }
                std::cerr << "camera" << i << " AI disabled: model unavailable\n";
            }
        }
    }
    if (!p.config.events_path.empty()) p.events.open(p.config.events_path, std::ios::out | std::ios::trunc);
    if (!p.config.sync_diagnostics_path.empty()) {
        p.sync_diag.open(p.config.sync_diagnostics_path, std::ios::out | std::ios::trunc);
        if (p.sync_diag.is_open()) p.sync_diag << "pair_id,cam0_sequence,cam1_sequence,cam0_timestamp_ns,cam1_timestamp_ns,delta_ns,clock_domain,status\n";
    }
    const auto deadline = p.config.duration_sec ? p.started + std::chrono::seconds(p.config.duration_sec) : std::chrono::steady_clock::time_point::max();
    p.encode_thread = std::thread(&Impl::encode_loop, &p);
    for (int i = 0; i < 2; ++i) if (p.models[i]) p.ai_threads[i] = std::thread(&Impl::ai_loop, &p, static_cast<uint32_t>(i));
    for (int i = 0; i < 2; ++i) p.capture_threads[i] = std::thread(&Impl::capture_loop, &p, static_cast<uint32_t>(i), deadline);
    for (auto &thread : p.capture_threads) if (thread.joinable()) thread.join();
    p.pair_queue.close(); for (auto &queue : p.ai_queue.values) queue->close(); for (auto &thread : p.ai_threads) if (thread.joinable()) thread.join(); if (p.encode_thread.joinable()) p.encode_thread.join();
    p.encoder.Release(); p.raw_output.flush(); p.raw_output.close(); if (p.rtmp_sink && !p.rtmp_sink->close()) ++p.counters.rtmp_errors; if (p.mp4_sink && !p.mp4_sink->close()) ++p.counters.mp4_errors; p.events.flush(); p.events.close(); p.sync_diag.flush(); p.sync_diag.close();
    for (auto &cam : p.camera) { std::string stop_error; if (!cam.stop(&stop_error) && error && error->empty()) *error = stop_error; }
    const FramePairerStats ps = p.pairer.stats(); { std::lock_guard<std::mutex> lock(p.stats_mutex); p.counters.pair_drops += ps.pair_drops; p.counters.sync_p50_ms = ps.p50_ms(); p.counters.sync_p95_ms = ps.p95_ms(); p.counters.sync_p99_ms = ps.p99_ms(); p.counters.last_delta_ms = static_cast<double>(ps.last_delta_ns) / 1'000'000.0; p.counters.max_delta_ms = static_cast<double>(ps.max_delta_ns) / 1'000'000.0; }
    std::lock_guard<std::mutex> lock(p.error_mutex); if (error && error->empty()) *error = p.failure; return p.failure.empty();
}

DualCameraPipelineStats DualCameraPipeline::stats() const { if (!impl_) return {}; std::lock_guard<std::mutex> lock(impl_->stats_mutex); DualCameraPipelineStats out = impl_->counters; const FramePairerStats ps = impl_->pairer.stats(); out.sync_p50_ms = ps.p50_ms(); out.sync_p95_ms = ps.p95_ms(); out.sync_p99_ms = ps.p99_ms(); const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl_->started).count(); if (elapsed > 0.0) out.pair_fps = static_cast<double>(ps.matched_pairs) / elapsed; return out; }
