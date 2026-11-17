#include "dual_camera_pipeline.h"

#include "ai/model_contract.h"
#include "behavior/event_manager.h"
#include "bounded_frame_queue.h"
#include "camera_frame.h"
#include "composite_processor.h"
#include "encoded_packet_fanout.h"
#include "frame_pairer.h"
#include "mpp_encoder.h"
#include "output/packet_sink.h"
#include "output/recording_index.h"
#include "rknnPool.hpp"
#include "runtime/monitor.h"
#include "v4l2_isp_camera.h"

#include "RgaUtils.h"
#include "im2d.h"
#include <opencv2/imgproc.hpp>

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
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

bool copy_nv12(const camera::CameraFrame &frame, std::vector<uint8_t> *out) {
    return frame.valid() && camera::copy_nv12_packed(frame.desc, out, nullptr);
}

bool nv12_to_bgr_rga(const std::vector<uint8_t> &nv12, uint32_t width, uint32_t height, cv::Mat *bgr) {
    if (!bgr || nv12.size() < static_cast<size_t>(width) * height * 3U / 2U)
        return false;
    bgr->create(static_cast<int>(height), static_cast<int>(width), CV_8UC3);
    rga_buffer_t src = wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(nv12.data()), width, height, width,
                                                height, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(bgr->data, width, height, width, height, RK_FORMAT_BGR_888);
    if (imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888, IM_COLOR_SPACE_DEFAULT) ==
        IM_STATUS_SUCCESS)
        return true;
    cv::Mat yuv(static_cast<int>(height + height / 2U), static_cast<int>(width), CV_8UC1,
                const_cast<uint8_t *>(nv12.data()));
    cv::cvtColor(yuv, *bgr, cv::COLOR_YUV2BGR_NV12);
    return !bgr->empty();
}

bool bgr_to_nv12(const cv::Mat &bgr, std::vector<uint8_t> *nv12) {
    if (!nv12 || bgr.empty() || bgr.type() != CV_8UC3 || (bgr.cols & 1) || (bgr.rows & 1))
        return false;
    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    const size_t y_size = static_cast<size_t>(bgr.cols) * bgr.rows;
    const size_t uv_size = y_size / 2U;
    nv12->assign(y_size + uv_size, 0);
    std::memcpy(nv12->data(), i420.data, y_size);
    const uint8_t *u = i420.data + y_size;
    const uint8_t *v = u + y_size / 4U;
    uint8_t *uv = nv12->data() + y_size;
    for (size_t i = 0; i < y_size / 4U; ++i) {
        uv[2U * i] = u[i];
        uv[2U * i + 1U] = v[i];
    }
    return true;
}

bool has_h264_idr(const uint8_t *data, size_t size) {
    if (!data)
        return false;
    for (size_t i = 0; i + 4 < size; ++i) {
        size_t nal = i;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            nal = i + 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1)
            nal = i + 4;
        else
            continue;
        if (nal < size && (data[nal] & 0x1FU) == 5U)
            return true;
    }
    return false;
}

class FfmpegSink {
  public:
    ~FfmpegSink() { close(); }
    bool open_rtmp(const std::string &url, uint32_t fps) {
        fps_ = fps;
        network_ = true;
        arguments_ = {"-loglevel",   "error",  "-fflags", "+genpts", "-framerate", std::to_string(fps),
                      "-f",          "h264",   "-i",      "pipe:0",  "-c",         "copy",
                      "-rw_timeout", "1000000"};
        if (url.rfind("rtsp://", 0) == 0)
            arguments_.insert(arguments_.end(), {"-rtsp_transport", "tcp", "-f", "rtsp", url});
        else if (url.rfind("rtmp://", 0) == 0 || url.rfind("rtmps://", 0) == 0)
            arguments_.insert(arguments_.end(), {"-f", "flv", url});
        else
            return false;
        return fps != 0;
    }
    bool open_mp4(const std::string &dir, uint32_t segment_sec, uint32_t fps, int camera_id,
                  uint64_t minimum_free_bytes) {
        minimum_free_bytes_ = minimum_free_bytes;
        fps_ = fps;
        segment_sec_ = segment_sec;
        camera_id_ = camera_id;
        directory_ = dir;
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        if (ec || !fps || !segment_sec)
            return false;
        index_.open(std::filesystem::path(dir) / (camera_id < 0
                                                      ? "joint-index.jsonl"
                                                      : "cam" + std::to_string(camera_id) + "-index.jsonl"),
                    std::ios::app);
        return index_.good();
    }
    bool consume(const EncodedPacket &packet) {
        const auto time = output::packet_time_ns(packet.pts, packet.time_base_num, packet.time_base_den);
        if (!time || !packet.bytes)
            return false;
        if (network_) {
            if (!active_) {
                if (!packet.keyframe || std::chrono::steady_clock::now() < retry_at_ || reconnects_ >= 5)
                    return false;
                ++reconnects_;
                ++startup_attempts_;
                if (!process_.open(arguments_)) {
                    retry_at_ =
                        std::chrono::steady_clock::now() + std::chrono::seconds(1U << (reconnects_ - 1));
                    return false;
                }
                active_ = true;
            }
            if (process_.write(packet.bytes->data(), packet.bytes->size()))
                return true;
            (void)process_.close(250);
            active_ = false;
            retry_at_ = std::chrono::steady_clock::now() +
                        std::chrono::seconds(1U << std::min<unsigned>(reconnects_, 5));
            return false;
        }
        if (active_ && packet.keyframe &&
            *time - first_pts_ns_ >= static_cast<uint64_t>(segment_sec_) * 1'000'000'000ULL) {
            if (!finish_segment())
                return false;
        }
        if (!active_) {
            if (!packet.keyframe)
                return false;
            const auto disk = runtime::sample_system(directory_, minimum_free_bytes_);
            if (!disk.disk_known || disk.low_disk)
                return false;
            const uint64_t wall =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
            segment_id_ = output::segment_name(wall, next_segment_++, camera_id_);
            final_path_ = std::filesystem::path(directory_) / segment_id_;
            partial_path_ = final_path_.string() + ".partial.mp4";
            arguments_ = {"-loglevel",
                          "error",
                          "-n",
                          "-fflags",
                          "+genpts",
                          "-framerate",
                          std::to_string(fps_),
                          "-f",
                          "h264",
                          "-i",
                          "pipe:0",
                          "-c",
                          "copy",
                          "-movflags",
                          "+faststart",
                          "-f",
                          "mp4",
                          partial_path_.string()};
            if (!process_.open(arguments_))
                return false;
            active_ = true;
            first_pts_ns_ = *time;
        }
        if (!process_.write(packet.bytes->data(), packet.bytes->size())) {
            (void)process_.close(250);
            active_ = false;
            index_ << output::finalized_index_json(segment_id_, false) << '\n';
            index_.flush();
            return false;
        }
        index_ << output::packet_index_json(segment_id_, packet, first_pts_ns_) << '\n';
        index_.flush();
        return index_.good();
    }
    unsigned startup_attempts() const { return startup_attempts_.load(); }
    bool close() {
        if (network_) {
            const bool ok = process_.close();
            active_ = false;
            return ok;
        }
        return !active_ || finish_segment();
    }

  private:
    bool finish_segment() {
        bool ok = process_.close();
        active_ = false;
        if (ok) {
            std::error_code ec;
            std::filesystem::rename(partial_path_, final_path_, ec);
            ok = !ec;
        }
        index_ << output::finalized_index_json(segment_id_, ok) << '\n';
        index_.flush();
        return ok && index_.good();
    }
    output::FfmpegProcess process_;
    std::vector<std::string> arguments_;
    bool network_ = false, active_ = false;
    uint32_t fps_ = 0, segment_sec_ = 300;
    int camera_id_ = -1;
    unsigned reconnects_ = 0;
    std::atomic<unsigned> startup_attempts_{0};
    std::chrono::steady_clock::time_point retry_at_{};
    std::string directory_, segment_id_;
    std::filesystem::path partial_path_, final_path_;
    uint64_t first_pts_ns_ = 0, next_segment_ = 0, minimum_free_bytes_ = 1024ULL * 1024ULL * 1024ULL;
    std::ofstream index_;
};

} // namespace

struct DualCameraPipeline::Impl {
    struct AiItem {
        camera::CameraFrame frame;
        uint64_t sequence = 0;
        uint64_t timestamp_ns = 0;
        uint64_t monotonic_ns = 0;
    };
    struct AiSnapshot {
        uint64_t sequence = 0;
        uint64_t timestamp_ns = 0;
        std::vector<DetectionBox> boxes;
    };

    struct AiQueues {
        std::unique_ptr<BoundedFrameQueue<AiItem>> values[2];
        AiQueues()
            : values{std::make_unique<BoundedFrameQueue<AiItem>>(kAiQueueDepth),
                     std::make_unique<BoundedFrameQueue<AiItem>>(kAiQueueDepth)} {}
    };

    explicit Impl(DualCameraPipelineConfig value)
        : config(std::move(value)), pairer(FramePairerConfig{}), pair_queue(kPairQueueDepth), ai_queue() {}

    DualCameraPipelineConfig config;
    V4L2IspCamera camera[2];
    FramePairer pairer;
    BoundedFrameQueue<FramePair> pair_queue;
    AiQueues ai_queue;
    struct VideoOutput {
        MppEncoder encoder;
        std::ofstream raw_output;
        std::unique_ptr<FfmpegSink> network, recorder;
        std::unique_ptr<output::AsyncPacketSink> raw_worker, network_worker, recorder_worker;
        EncodedPacketDistributor distributor;
        std::vector<uint8_t> header;
        bool closed = false, close_ok = true;
        ~VideoOutput() { close(); }
        bool close() {
            if (closed)
                return close_ok;
            closed = true;
            if (raw_worker)
                raw_worker->close();
            if (network_worker)
                network_worker->close(false);
            if (recorder_worker)
                recorder_worker->close();
            if (network)
                (void)network->close();
            if (recorder && !recorder->close())
                close_ok = false;
            if (raw_worker && raw_worker->stats().failed)
                close_ok = false;
            if (recorder_worker && recorder_worker->stats().failed)
                close_ok = false;
            if (raw_output.is_open()) {
                raw_output.flush();
                if (!raw_output.good())
                    close_ok = false;
                raw_output.close();
            }
            encoder.Release();
            return close_ok;
        }
    };
    VideoOutput video[2];
    std::unique_ptr<BoundedFrameQueue<FrameSample>> video_queue[2] = {
        std::make_unique<BoundedFrameQueue<FrameSample>>(4),
        std::make_unique<BoundedFrameQueue<FrameSample>>(4)};
    runtime::HealthMonitor health;
    std::unique_ptr<runtime::RotatingLog> health_log;
    std::atomic<bool> camera_done[2]{};
    bool independent() const { return config.video_mode == AppOptions::VideoMode::Independent; }
    std::unique_ptr<rknn_lite> models[2];
    AiSnapshot ai_snapshot[2];
    ai::ModelContract model_contract;
    std::mutex ai_mutex;
    std::condition_variable ai_cv;
    std::ofstream events;
    std::mutex events_mutex;
    std::ofstream sync_diag;
    std::mutex sync_diag_mutex;
    CompositeLayout layout;
    std::thread capture_threads[2];
    std::thread ai_threads[2];
    std::thread encode_threads[2];
    std::atomic<bool> stop{false};
    std::mutex stats_mutex;
    std::mutex error_mutex;
    DualCameraPipelineStats counters;
    std::string failure;
    std::chrono::steady_clock::time_point started;
    bool run_started = false;

    bool stopping() const { return stop.load() || (config.external_stop && config.external_stop->load()); }
    void fail(const std::string &message) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (failure.empty())
            failure = message;
        stop.store(true);
    }

    void queue_pairs(std::vector<FramePair> pairs) {
        for (FramePair &pair : pairs) {
            if (sync_diag.is_open()) {
                std::lock_guard<std::mutex> lock(sync_diag_mutex);
                sync_diag << pair.pair_id << ',' << pair.cam0.sequence << ',' << pair.cam1.sequence << ','
                          << pair.cam0.timestamp_ns << ',' << pair.cam1.timestamp_ns << ',' << pair.delta_ns
                          << ',' << (pair.clock_domain_verified ? "verified" : "unverified") << ','
                          << (pair.is_stale() ? "stale" : "matched") << '\n';
            }
            bool dropped = false;
            if (!pair_queue.push_drop_oldest(std::move(pair), &dropped)) {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.pair_drops;
            } else if (dropped) {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.pair_drops;
            }
        }
    }

    void capture_loop(uint32_t id, std::chrono::steady_clock::time_point deadline) {
        uint64_t errors = 0;
        auto next_ai = std::chrono::steady_clock::now();
        auto next_stats = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max<uint32_t>(1U, config.stats_interval_sec));
        const auto ai_period =
            std::chrono::milliseconds(config.ai_fps ? std::max<uint32_t>(1U, 1000U / config.ai_fps) : 1000U);
        while (!stopping() && std::chrono::steady_clock::now() < deadline) {
            std::string error;
            auto frame = camera[id].dequeue(&error);
            if (!frame) {
                if (stopping())
                    break;
                if (error.find("timeout") != std::string::npos) {
                    queue_pairs(pairer.tick(steady_now_ns()));
                    continue;
                }
                ++errors;
                {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    if (id == 0)
                        ++counters.camera0_errors;
                    else
                        ++counters.camera1_errors;
                }
                health.error("camera" + std::to_string(id));
                if (errors >= 5) {
                    if (!independent())
                        fail("camera" + std::to_string(id) + " repeated V4L2 dequeue failures: " + error);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100U * errors));
                continue;
            }
            errors = 0;
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                if (id == 0)
                    ++counters.camera0_frames;
                else
                    ++counters.camera1_frames;
            }
            FrameSample sample;
            sample.camera_id = id;
            sample.sequence = frame->desc.sequence;
            sample.timestamp_ns = frame->desc.timestamp_ns;
            sample.clock_domain = static_cast<FrameClockDomain>(frame->desc.clock_domain);
            sample.frame = *frame;
            health.heartbeat("camera" + std::to_string(id), steady_now_ns(),
                             independent() ? video_queue[id]->size() : pairer.pending(id));
            if (independent()) {
                // Recording pressure must be explicit; a full camera queue is
                // a reported acquisition loss rather than silent drop-oldest.
                if (!video_queue[id]->tryPush(std::move(sample))) {
                    if (!stopping())
                        fail("camera video queue full: recording throughput is insufficient");
                    break;
                }
            } else
                queue_pairs(pairer.push(std::move(sample), steady_now_ns()));
            if (models[id] && config.ai_mode != AppOptions::AiMode::Off) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_ai) {
                    AiItem item;
                    item.frame = *frame;
                    item.sequence = frame->desc.sequence;
                    item.timestamp_ns = frame->desc.timestamp_ns;
                    item.monotonic_ns = frame->desc.monotonic_timestamp_ns;
                    bool dropped = false;
                    if (!ai_queue.values[id]->push_drop_oldest(std::move(item), &dropped)) {
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        if (id == 0)
                            ++counters.ai0_drops;
                        else
                            ++counters.ai1_drops;
                    } else if (dropped) {
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        if (id == 0)
                            ++counters.ai0_drops;
                        else
                            ++counters.ai1_drops;
                    }
                    next_ai = now + ai_period;
                }
            }
            if (id == 0 && std::chrono::steady_clock::now() >= next_stats) {
                const FramePairerStats pair_stats = pairer.stats();
                const auto elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                std::lock_guard<std::mutex> lock(stats_mutex);
                std::cerr << "stats elapsed_sec=" << std::fixed << std::setprecision(1) << elapsed
                          << " cam0=" << counters.camera0_frames << " cam1=" << counters.camera1_frames
                          << " pairs=" << pair_stats.matched_pairs << " pair_drops=" << pair_stats.pair_drops
                          << " delta_p95_ms=" << pair_stats.p95_ms() << '\n';
                next_stats = std::chrono::steady_clock::now() +
                             std::chrono::seconds(std::max<uint32_t>(1U, config.stats_interval_sec));
            }
        }
        if (!independent())
            queue_pairs(
                pairer.tick(steady_now_ns() + static_cast<uint64_t>(config.sync_max_wait_ms) * 1'000'000ULL));
        camera_done[id].store(true);
        video_queue[id]->close();
        ai_queue.values[id]->close();
    }

    void ai_loop(uint32_t id) {
        behavior::RatTracker tracker;
        behavior::EventConfig event_config;
        event_config.session_id = std::to_string(steady_now_ns()) + "-cam" + std::to_string(id);
        behavior::EventManager manager(event_config);
        auto emit_events = [&](const std::vector<behavior::BehaviorEvent> &completed) {
            if (!events.is_open())
                return;
            std::lock_guard<std::mutex> lock(events_mutex);
            for (const auto &event : completed)
                events << behavior::event_json(event) << '\n';
            events.flush();
        };
        AiItem item;
        for (;;) {
            if (!ai_queue.values[id]->waitPop(item, std::chrono::milliseconds(100))) {
                if (ai_queue.values[id]->closed())
                    break;
                emit_events(manager.tick(id, steady_now_ns()));
                continue;
            }
            if (stopping())
                continue;
            const auto begin = std::chrono::steady_clock::now();
            std::vector<uint8_t> nv12;
            cv::Mat bgr;
            if (!copy_nv12(item.frame, &nv12) ||
                !nv12_to_bgr_rga(nv12, config.input_width, config.input_height, &bgr)) {
                continue;
            }
            ai::PreparedTensor prepared;
            std::string preprocess_error;
            if (!ai::preprocess_nv12(item.frame, model_contract, {}, &prepared, &preprocess_error)) {
                health.error("ai" + std::to_string(id));
                continue;
            }
            item.frame = camera::CameraFrame{};
            if (config.simulate_ai_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(config.simulate_ai_ms));
            detect_result_group_t group{};
            int result = 0;
            if (models[id]) {
                models[id]->ori_img = std::move(bgr);
                result = models[id]->interf(group, &prepared);
            }
            if (result < 0) {
                health.error("ai" + std::to_string(id));
                continue;
            }
            health.heartbeat("ai" + std::to_string(id), steady_now_ns(), ai_queue.values[id]->size());
            AiSnapshot snapshot;
            snapshot.sequence = item.sequence;
            snapshot.timestamp_ns = item.timestamp_ns;
            for (int i = 0; i < group.count; ++i) {
                const auto &d = group.results[i];
                DetectionBox box;
                box.box = {static_cast<float>(d.box.left), static_cast<float>(d.box.top),
                           static_cast<float>(d.box.right - d.box.left),
                           static_cast<float>(d.box.bottom - d.box.top)};
                box.confidence = d.prop;
                box.label = d.name;
                box.class_id = d.class_id;
                snapshot.boxes.push_back(std::move(box));
            }
            behavior::DetectionResult detection;
            detection.frame.camera_id = id;
            detection.frame.sequence = item.sequence;
            detection.frame.source_timestamp_ns = item.timestamp_ns;
            detection.frame.monotonic_timestamp_ns = item.monotonic_ns;
            detection.frame.model_id = model_contract.model_id;
            detection.frame.model_version = model_contract.model_version;
            for (const auto &box : snapshot.boxes)
                detection.detections.push_back(
                    {{box.box.x, box.box.y, box.box.width, box.box.height}, box.class_id, box.confidence});
            const auto tracked = tracker.update(detection);
            emit_events(manager.update(
                behavior::classify_behavior(detection.frame, tracked, model_contract.behavior_classes)));
            {
                std::lock_guard<std::mutex> lock(ai_mutex);
                ai_snapshot[id] = snapshot;
            }
            ai_cv.notify_all();
            const auto end = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(stats_mutex);
            if (id == 0) {
                ++counters.ai0_processed;
                counters.ai0_fps = counters.ai0_processed /
                                   std::max(0.001, std::chrono::duration<double>(end - started).count());
            } else {
                ++counters.ai1_processed;
                counters.ai1_fps = counters.ai1_processed /
                                   std::max(0.001, std::chrono::duration<double>(end - started).count());
            }
            if (events.is_open()) {
                std::lock_guard<std::mutex> lock(events_mutex);
                events << "{\"camera_id\":" << id << ",\"sequence\":" << item.sequence
                       << ",\"capture_timestamp_ns\":" << item.timestamp_ns << ",\"infer_ms\":" << std::fixed
                       << std::setprecision(3)
                       << std::chrono::duration<double, std::milli>(end - begin).count()
                       << ",\"boxes\":" << group.count << "}\n";
                events.flush();
            }
        }
        emit_events(manager.finish());
    }

    bool compose_pair(const FramePair &pair, std::vector<uint8_t> *output) {
        std::vector<uint8_t> nv120, nv121;
        if (!copy_nv12(pair.cam0.frame, &nv120) || !copy_nv12(pair.cam1.frame, &nv121))
            return false;
        cv::Mat bgr0, bgr1;
        if (!nv12_to_bgr_rga(nv120, config.input_width, config.input_height, &bgr0) ||
            !nv12_to_bgr_rga(nv121, config.input_width, config.input_height, &bgr1))
            return false;
        cv::Mat canvas(static_cast<int>(config.output_height), static_cast<int>(config.output_width), CV_8UC3,
                       cv::Scalar(16, 16, 16));
        auto draw_view = [&](const cv::Mat &source, const CompositeView &view, uint32_t id) {
            cv::Mat scaled(static_cast<int>(view.dst.height), static_cast<int>(view.dst.width), CV_8UC3);
            rga_buffer_t rga_src =
                wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(source.data), source.cols, source.rows,
                                         source.cols, source.rows, RK_FORMAT_BGR_888);
            rga_buffer_t rga_dst = wrapbuffer_virtualaddr_t(scaled.data, scaled.cols, scaled.rows,
                                                            scaled.cols, scaled.rows, RK_FORMAT_BGR_888);
            const double scale_x = static_cast<double>(scaled.cols) / static_cast<double>(source.cols);
            const double scale_y = static_cast<double>(scaled.rows) / static_cast<double>(source.rows);
            if (imresize(rga_src, rga_dst, scale_x, scale_y, IM_INTERP_DEFAULT) != IM_STATUS_SUCCESS)
                cv::resize(source, scaled, scaled.size());
            const cv::Rect roi(static_cast<int>(view.dst.x), static_cast<int>(view.dst.y), scaled.cols,
                               scaled.rows);
            scaled.copyTo(canvas(roi));
            const uint64_t frame_ts = id == 0 ? pair.cam0.timestamp_ns : pair.cam1.timestamp_ns;
            const uint64_t frame_seq = id == 0 ? pair.cam0.sequence : pair.cam1.sequence;
            AiSnapshot snapshot;
            {
                std::lock_guard<std::mutex> lock(ai_mutex);
                snapshot = ai_snapshot[id];
            }
            const bool timestamp_usable = snapshot.timestamp_ns != 0 && snapshot.timestamp_ns == frame_ts;
            // Only exact source-frame results may be drawn as detections.
            // A latest snapshot from an older frame is identified as waiting.
            const bool sequence_usable = snapshot.sequence == frame_seq;
            const bool usable =
                config.ai_mode != AppOptions::AiMode::Off && timestamp_usable && sequence_usable;
            for (const DetectionBox &box : snapshot.boxes) {
                if (!usable)
                    break;
                const RectF clipped = CompositeProcessor::clip_box(CompositeProcessor::map_box(view, box.box),
                                                                   config.output_width, config.output_height);
                if (clipped.width <= 0.0F || clipped.height <= 0.0F)
                    continue;
                cv::rectangle(canvas,
                              cv::Rect(static_cast<int>(clipped.x), static_cast<int>(clipped.y),
                                       std::max(1, static_cast<int>(clipped.width)),
                                       std::max(1, static_cast<int>(clipped.height))),
                              id == 0 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);
                std::ostringstream label;
                label << box.label << " " << std::fixed << std::setprecision(2) << box.confidence;
                cv::putText(
                    canvas, label.str(),
                    cv::Point(static_cast<int>(clipped.x), std::max(12, static_cast<int>(clipped.y) - 3)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, id == 0 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255),
                    1);
            }
            const std::string ai_status =
                config.ai_mode == AppOptions::AiMode::Off ? " AI_OFF" : (usable ? " AI_OK" : " AI_WAIT");
            const uint64_t age_ns = (snapshot.timestamp_ns != 0 && frame_ts >= snapshot.timestamp_ns)
                                        ? frame_ts - snapshot.timestamp_ns
                                        : 0;
            std::ostringstream camera_label;
            camera_label << "CAM" << id << " seq=" << frame_seq << " ts_ns=" << frame_ts
                         << " BEHAVIOR_UNAVAILABLE" << (pair.is_stale() ? " STALE" : "") << ai_status;
            if (snapshot.timestamp_ns != 0)
                camera_label << " age_ms=" << std::fixed << std::setprecision(1)
                             << static_cast<double>(age_ns) / 1'000'000.0;
            cv::putText(canvas, camera_label.str(),
                        cv::Point(static_cast<int>(view.dst.x) + 8, static_cast<int>(view.dst.y) + 24),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        };
        draw_view(bgr0, layout.cam0, 0);
        draw_view(bgr1, layout.cam1, 1);
        std::ostringstream sync;
        sync << "delta_ms=" << std::fixed << std::setprecision(3)
             << static_cast<double>(pair.delta_ns) / 1'000'000.0
             << (pair.clock_domain_verified ? " SYNC" : " UNVERIFIED");
        cv::putText(canvas, sync.str(), cv::Point(12, static_cast<int>(config.output_height) - 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 0), 1);
        return bgr_to_nv12(canvas, output);
    }

    bool compose_single(const FrameSample &sample, std::vector<uint8_t> *output) {
        std::vector<uint8_t> packed;
        cv::Mat source;
        if (!copy_nv12(sample.frame, &packed) ||
            !nv12_to_bgr_rga(packed, sample.frame.desc.width, sample.frame.desc.height, &source))
            return false;
        cv::Mat canvas(static_cast<int>(config.output_height), static_cast<int>(config.output_width), CV_8UC3,
                       cv::Scalar(16, 16, 16));
        const double scale = std::min(static_cast<double>(canvas.cols) / source.cols,
                                      static_cast<double>(canvas.rows) / source.rows);
        const int width = std::max(2, static_cast<int>(source.cols * scale) & ~1);
        const int height = std::max(2, static_cast<int>(source.rows * scale) & ~1);
        const int x = (canvas.cols - width) / 2, y = (canvas.rows - height) / 2;
        cv::Mat resized;
        cv::resize(source, resized, cv::Size(width, height)); // Explicit CPU resize path.
        resized.copyTo(canvas(cv::Rect(x, y, width, height)));
        AiSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(ai_mutex);
            snapshot = ai_snapshot[sample.camera_id];
        }
        if (snapshot.sequence == sample.sequence && snapshot.timestamp_ns == sample.timestamp_ns) {
            for (const auto &box : snapshot.boxes) {
                const RectF mapped = CompositeProcessor::clip_box(
                    {static_cast<float>(x + box.box.x * scale), static_cast<float>(y + box.box.y * scale),
                     static_cast<float>(box.box.width * scale), static_cast<float>(box.box.height * scale)},
                    config.output_width, config.output_height);
                if (mapped.width < 1 || mapped.height < 1)
                    continue;
                cv::rectangle(canvas,
                              cv::Rect(static_cast<int>(mapped.x), static_cast<int>(mapped.y),
                                       static_cast<int>(mapped.width), static_cast<int>(mapped.height)),
                              cv::Scalar(0, 255, 0), 2);
                std::ostringstream text;
                text << box.label << ' ' << std::fixed << std::setprecision(2) << box.confidence;
                cv::putText(canvas, text.str(),
                            cv::Point(static_cast<int>(mapped.x), std::max(12, static_cast<int>(mapped.y))),
                            cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 0));
            }
        }
        std::ostringstream label;
        label << "CAM" << sample.camera_id << " seq=" << sample.sequence << " ts_ns=" << sample.timestamp_ns
              << " BEHAVIOR_UNAVAILABLE";
        cv::putText(canvas, label.str(), cv::Point(8, 24), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255));
        return bgr_to_nv12(canvas, output);
    }

    void encode_loop(uint32_t id) {
        FramePair pair;
        FrameSample single;
        VideoOutput &sink = video[id];
        std::vector<uint8_t> composite,
            packet(static_cast<size_t>(config.output_width) * config.output_height * 2U);
        uint64_t pts = 0;
        while (independent() ? video_queue[id]->pop(single) : pair_queue.pop(pair)) {
            if (!(independent() ? compose_single(single, &composite) : compose_pair(pair, &composite))) {
                health.error("encoder" + std::to_string(id));
                fail("invalid video frame layout; recording cannot continue without loss");
                break;
            }
            EncodedPacket encoded;
            encoded.pts = encoded.dts = static_cast<int64_t>(pts);
            encoded.time_base_num = 1;
            encoded.time_base_den = config.fps;
            if (independent()) {
                encoded.sources[0] = {id, single.sequence, single.timestamp_ns};
                encoded.source_count = 1;
                single = FrameSample{};
            } else {
                encoded.sources[0] = {0, pair.cam0.sequence, pair.cam0.timestamp_ns};
                encoded.sources[1] = {1, pair.cam1.sequence, pair.cam1.timestamp_ns};
                encoded.source_count = 2;
                pair = FramePair{};
            }
            int packet_size = static_cast<int>(packet.size());
            const int result = sink.encoder.EncodeNV12(
                composite.data(), static_cast<int>(config.output_width),
                static_cast<int>(config.output_height), static_cast<int>(config.output_width),
                static_cast<int>(config.output_width), static_cast<int>(composite.size()), packet.data(),
                &packet_size);
            if (result != 0 || packet_size <= 0) {
                {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    ++counters.encode_errors;
                }
                health.error("encoder" + std::to_string(id));
                fail("MPP H.264 encode failed or returned no packet");
                break;
            }
            encoded.keyframe = has_h264_idr(packet.data(), static_cast<size_t>(packet_size));
            auto bytes = std::make_shared<std::vector<uint8_t>>();
            if (encoded.keyframe)
                bytes->insert(bytes->end(), sink.header.begin(), sink.header.end());
            bytes->insert(bytes->end(), packet.begin(), packet.begin() + packet_size);
            encoded.bytes = bytes;
            (void)sink.distributor.publish(encoded);
            health.heartbeat("encoder" + std::to_string(id), steady_now_ns(),
                             independent() ? video_queue[id]->size() : pair_queue.size());
            ++pts;
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                ++counters.encoded_frames;
                counters.pair_frames = independent() ? 0 : counters.encoded_frames;
                const double elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                counters.encode_fps = counters.encoded_frames / std::max(0.001, elapsed);
                counters.pair_fps = independent() ? 0 : counters.encode_fps;
            }
        }
    }
};

DualCameraPipeline::DualCameraPipeline(DualCameraPipelineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

DualCameraPipeline::~DualCameraPipeline() {
    request_stop();
    if (!impl_)
        return;
    for (auto &thread : impl_->capture_threads)
        if (thread.joinable())
            thread.join();
    for (auto &thread : impl_->ai_threads)
        if (thread.joinable())
            thread.join();
    for (auto &thread : impl_->encode_threads)
        if (thread.joinable())
            thread.join();
    impl_->pairer.clear();
    FramePair pair;
    while (impl_->pair_queue.tryPop(pair))
        pair = FramePair{};
    for (auto &queue : impl_->video_queue) {
        FrameSample frame;
        while (queue->tryPop(frame))
            frame = FrameSample{};
    }
    for (auto &queue : impl_->ai_queue.values) {
        Impl::AiItem item;
        while (queue->tryPop(item))
            item = Impl::AiItem{};
    }
    for (auto &output : impl_->video)
        output.close();
    for (auto &cam : impl_->camera) {
        std::string ignored;
        (void)cam.stop(&ignored);
    }
}
void DualCameraPipeline::request_stop() {
    if (!impl_)
        return;
    impl_->stop.store(true);
    impl_->pair_queue.close();
    for (auto &queue : impl_->video_queue)
        queue->close();
    for (auto &queue : impl_->ai_queue.values)
        queue->close();
    impl_->ai_cv.notify_all();
}

bool DualCameraPipeline::run(std::string *error) {
    if (error)
        error->clear();
    if (!impl_) {
        if (error)
            *error = "pipeline implementation unavailable";
        return false;
    }
    Impl &p = *impl_;
    if (p.run_started) {
        if (error)
            *error = "DualCameraPipeline instances are single-use";
        return false;
    }
    p.run_started = true;
    if (p.config.camera0.empty() || p.config.camera1.empty() || p.config.camera0 == p.config.camera1) {
        if (error)
            *error = "two distinct verified RKISP output nodes are required";
        return false;
    }
    if (p.config.ai_mode == AppOptions::AiMode::Strict) {
        if (error)
            *error = "strict AI mode would block required recording; use async";
        return false;
    }
    if (p.config.sync_mode == AppOptions::SyncMode::Hardware) {
        if (error)
            *error = "hardware sync requested but no verified Sensor FSIN/trigger BSP backend is installed";
        return false;
    }
    FramePairerConfig pair_cfg;
    pair_cfg.tolerance_ns = static_cast<uint64_t>(p.config.sync_tolerance_ms) * 1'000'000ULL;
    pair_cfg.max_wait_ns = static_cast<uint64_t>(p.config.sync_max_wait_ms) * 1'000'000ULL;
    pair_cfg.policy = p.config.sync_policy == AppOptions::SyncPolicy::HoldLast ? FramePairPolicy::HoldLast
                                                                               : FramePairPolicy::DropOlder;
    p.pairer.configure(pair_cfg);
    p.started = std::chrono::steady_clock::now();
    p.stop.store(false);
    bool enabled[2] = {false, false};
    V4L2IspCamera::Capabilities caps[2];
    for (int i = 0; i < 2; ++i) {
        V4L2IspCamera::Config cfg;
        cfg.camera_id = static_cast<uint32_t>(i);
        cfg.device = i == 0 ? p.config.camera0 : p.config.camera1;
        cfg.width = p.config.input_width;
        cfg.height = p.config.input_height;
        cfg.fps = p.config.fps;
        cfg.poll_timeout_ms = p.config.v4l2_timeout_ms;
        cfg.buffer_count = p.config.capture_buffer_count;
        cfg.pixel_format = p.config.pixel_format;
        std::string why;
        enabled[i] = p.camera[i].set_config(cfg, &why) && p.camera[i].open(&why) && p.camera[i].start(&why);
        if (!enabled[i]) {
            p.health.error("camera" + std::to_string(i));
            p.camera_done[i].store(true);
            if (i == 0)
                ++p.counters.camera0_errors;
            else
                ++p.counters.camera1_errors;
            if (!p.independent()) {
                if (error)
                    *error = why;
                return false;
            }
            std::cerr << "camera" << i << " unavailable: " << why << '\n';
            continue;
        }
        caps[i] = p.camera[i].capabilities();
        if (!caps[i].width || !caps[i].height) {
            if (error)
                *error = "camera negotiated invalid dimensions";
            return false;
        }
    }
    if (!enabled[0] && !enabled[1]) {
        if (error)
            *error = "both cameras are unavailable";
        return false;
    }
    if (enabled[0] && enabled[1] &&
        (caps[0].width != caps[1].width || caps[0].height != caps[1].height ||
         (caps[0].fps && caps[1].fps && caps[0].fps != caps[1].fps))) {
        if (error)
            *error = "camera negotiation mismatch; configure matching dimensions and frame rates";
        return false;
    }
    const auto &negotiated = caps[enabled[0] ? 0 : 1];
    p.config.input_width = negotiated.width;
    p.config.input_height = negotiated.height;
    if (negotiated.fps)
        p.config.fps = negotiated.fps;
    else
        std::cerr << "camera FPS unavailable from V4L2: using requested " << p.config.fps
                  << " only for CFR mux timing; actual capture rate unverified\n";
    std::string why;
    if (!p.independent() && !CompositeProcessor::make_side_by_side(
                                p.config.input_width, p.config.input_height, p.config.output_width,
                                p.config.output_height, &p.layout, &why)) {
        if (error)
            *error = why;
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(p.config.output_dir, ec);
    if (ec) {
        if (error)
            *error = "cannot create output directory";
        return false;
    }
    p.health_log = std::make_unique<runtime::RotatingLog>(
        std::filesystem::path(p.config.output_dir) / "health.jsonl", 4U * 1024U * 1024U, 3);
    const int output_count = p.independent() ? 2 : 1;
    for (int i = 0; i < output_count; ++i) {
        if (p.independent() && !enabled[i])
            continue;
        auto &out = p.video[i];
        const std::string suffix = p.independent() ? ".cam" + std::to_string(i) : "";
        if (!p.config.no_record && !p.config.output_path.empty()) {
            const std::filesystem::path raw = p.config.output_path + suffix;
            if (std::filesystem::exists(raw)) {
                if (error)
                    *error = "refusing to overwrite existing recording: " + raw.string();
                return false;
            }
            out.raw_output.open(raw, std::ios::binary);
            if (!out.raw_output) {
                if (error)
                    *error = "cannot open raw H.264 recording";
                return false;
            }
            out.raw_worker = std::make_unique<output::AsyncPacketSink>(
                64, output::AsyncPacketSink::Overflow::FailRecording, [&p, i](const EncodedPacket &packet) {
                    auto &stream = p.video[i].raw_output;
                    stream.write(reinterpret_cast<const char *>(packet.bytes->data()),
                                 static_cast<std::streamsize>(packet.bytes->size()));
                    const bool ok = stream.good();
                    if (!ok) {
                        p.health.error("raw" + std::to_string(i));
                        p.fail("raw H.264 recording write failed");
                    } else
                        p.health.heartbeat("raw" + std::to_string(i), steady_now_ns());
                    return ok;
                });
            out.distributor.add_sink("raw", [&p, i](const EncodedPacket &packet) {
                const bool ok = p.video[i].raw_worker->enqueue(packet);
                if (!ok)
                    p.fail("raw recording queue overflow or write failure");
                return ok;
            });
        }
        if (!p.config.rtmp_url.empty() && !p.config.no_stream) {
            out.network = std::make_unique<FfmpegSink>();
            if (!out.network->open_rtmp(
                    (p.independent() ? output::camera_publish_url(p.config.rtmp_url, static_cast<uint32_t>(i))
                                     : p.config.rtmp_url),
                    p.config.fps)) {
                if (error)
                    *error = "invalid RTMP configuration";
                return false;
            }
            out.network_worker = std::make_unique<output::AsyncPacketSink>(
                8, output::AsyncPacketSink::Overflow::ResyncAtKeyframe, [&p, i](const EncodedPacket &packet) {
                    const bool ok = p.video[i].network->consume(packet);
                    if (!ok) {
                        p.health.error("network" + std::to_string(i));
                        std::lock_guard<std::mutex> lock(p.stats_mutex);
                        ++p.counters.rtmp_errors;
                    } else
                        p.health.heartbeat("network" + std::to_string(i), steady_now_ns());
                    return ok;
                });
            out.distributor.add_sink("network", [&p, i](const EncodedPacket &packet) {
                return p.video[i].network_worker->enqueue(packet);
            });
        }
        if (p.config.record && !p.config.no_record) {
            out.recorder = std::make_unique<FfmpegSink>();
            if (!out.recorder->open_mp4(p.config.output_dir, p.config.segment_sec, p.config.fps,
                                        p.independent() ? i : -1, p.config.minimum_free_bytes)) {
                if (error)
                    *error = "cannot prepare MP4 recorder and index";
                return false;
            }
            out.recorder_worker = std::make_unique<output::AsyncPacketSink>(
                64, output::AsyncPacketSink::Overflow::FailRecording, [&p, i](const EncodedPacket &packet) {
                    const bool ok = p.video[i].recorder->consume(packet);
                    if (!ok) {
                        p.health.error("recorder" + std::to_string(i));
                        {
                            std::lock_guard<std::mutex> lock(p.stats_mutex);
                            ++p.counters.mp4_errors;
                        }
                        p.fail("MP4 recording failed or disk below threshold");
                    } else
                        p.health.heartbeat("recorder" + std::to_string(i), steady_now_ns());
                    return ok;
                });
            out.distributor.add_sink("recorder", [&p, i](const EncodedPacket &packet) {
                const bool ok = p.video[i].recorder_worker->enqueue(packet);
                if (!ok)
                    p.fail("MP4 recording queue overflow or write failure");
                return ok;
            });
        }
        if (out.encoder.Init(static_cast<int>(p.config.output_width),
                             static_cast<int>(p.config.output_height), static_cast<int>(p.config.fps),
                             static_cast<int>(p.config.bitrate), 264) != 0) {
            if (error)
                *error = "MPP encoder initialization failed";
            return false;
        }
        std::array<uint8_t, 8192> header{};
        int header_size = static_cast<int>(header.size());
        if (out.encoder.GetHeader(header.data(), &header_size) != 0 || header_size <= 0) {
            if (error)
                *error = "MPP H.264 SPS/PPS header unavailable";
            return false;
        }
        out.header.assign(header.begin(), header.begin() + header_size);
    }
    if (p.config.ai_mode != AppOptions::AiMode::Off) {
        ai::ModelContract contract;
        const bool configured = !p.config.model_contract_path.empty() &&
                                ai::load_model_contract(p.config.model_contract_path, &contract, &why) &&
                                contract.supports_legacy_rknn(&why);
        if (!configured && p.config.ai_required) {
            if (error)
                *error = "RKNN_MODEL_CONTRACT_REQUIRED: " + why;
            return false;
        }
        p.model_contract = contract;
        const std::string paths[2] = {p.config.model0_path, p.config.model1_path};
        const int cores[2] = {p.config.npu_core0, p.config.npu_core1};
        for (int i = 0; i < 2; ++i) {
            if (!enabled[i])
                continue;
            if (configured && !paths[i].empty())
                p.models[i] = std::make_unique<rknn_lite>(
                    paths[i].c_str(), cores[i], static_cast<int>(contract.class_names.size()), i, &contract);
            if (!p.models[i] || !p.models[i]->valid()) {
                p.models[i].reset();
                p.health.error("ai" + std::to_string(i));
                if (p.config.ai_required) {
                    if (error)
                        *error = "RKNN model initialization failed for camera" + std::to_string(i);
                    return false;
                }
                std::cerr << "camera" << i
                          << " AI disabled: model or compatible explicit contract unavailable\n";
            }
        }
    }
    if (!p.config.events_path.empty()) {
        p.events.open(p.config.events_path, std::ios::app);
        if (!p.events) {
            if (error)
                *error = "cannot open requested event output";
            return false;
        }
    }
    if (!p.config.sync_diagnostics_path.empty()) {
        p.sync_diag.open(p.config.sync_diagnostics_path, std::ios::app);
        if (!p.sync_diag) {
            if (error)
                *error = "cannot open requested sync diagnostic output";
            return false;
        }
        p.sync_diag << "pair_id,cam0_sequence,cam1_sequence,cam0_timestamp_ns,cam1_timestamp_ns,delta_ns,"
                       "clock_domain,status\n";
    }
    const auto deadline = p.config.duration_sec ? p.started + std::chrono::seconds(p.config.duration_sec)
                                                : std::chrono::steady_clock::time_point::max();
    for (int i = 0; i < output_count; ++i)
        if (!p.independent() || enabled[i])
            p.encode_threads[i] = std::thread(&Impl::encode_loop, &p, static_cast<uint32_t>(i));
    for (int i = 0; i < 2; ++i)
        if (p.models[i])
            p.ai_threads[i] = std::thread(&Impl::ai_loop, &p, static_cast<uint32_t>(i));
    for (int i = 0; i < 2; ++i)
        if (enabled[i])
            p.capture_threads[i] = std::thread(&Impl::capture_loop, &p, static_cast<uint32_t>(i), deadline);
    auto next_health = std::chrono::steady_clock::now();
    while (!(p.camera_done[0].load() && p.camera_done[1].load()) && !p.stopping()) {
        if (std::chrono::steady_clock::now() >= next_health) {
            const auto metrics = runtime::sample_system(p.config.output_dir, p.config.minimum_free_bytes);
            std::ostringstream line;
            line << "{\"monotonic_ns\":" << steady_now_ns() << ",\"rss_bytes\":" << metrics.rss_bytes
                 << ",\"cpu_seconds\":" << metrics.cpu_seconds
                 << ",\"disk_available_bytes\":" << metrics.disk_available_bytes
                 << ",\"disk_known\":" << (metrics.disk_known ? "true" : "false")
                 << ",\"temperature_millidegrees\":" << metrics.temperature_millidegrees
                 << ",\"frequency_khz\":" << metrics.frequency_khz << ",\"components\":[";
            bool first = true;
            for (const auto &health : p.health.snapshot(steady_now_ns())) {
                if (!first)
                    line << ',';
                first = false;
                line << "{\"name\":\"" << health.name << "\",\"heartbeat_ns\":" << health.heartbeat_ns
                     << ",\"errors\":" << health.errors << ",\"queue_depth\":" << health.queue_depth
                     << ",\"stale\":" << (health.stale ? "true" : "false") << '}';
            }
            line << "],\"outputs\":[";
            for (int i = 0; i < output_count; ++i) {
                if (i)
                    line << ',';
                const auto network = p.video[i].network_worker ? p.video[i].network_worker->stats()
                                                               : output::AsyncPacketSink::Stats{};
                const auto recorder = p.video[i].recorder_worker ? p.video[i].recorder_worker->stats()
                                                                 : output::AsyncPacketSink::Stats{};
                const auto raw =
                    p.video[i].raw_worker ? p.video[i].raw_worker->stats() : output::AsyncPacketSink::Stats{};
                line << "{\"id\":" << i << ",\"network_depth\":" << network.depth
                     << ",\"network_dropped\":" << network.dropped << ",\"network_errors\":" << network.errors
                     << ",\"network_high_water\":" << network.high_water << ",\"network_start_attempts\":"
                     << (p.video[i].network ? p.video[i].network->startup_attempts() : 0)
                     << ",\"recorder_depth\":" << recorder.depth << ",\"recorder_errors\":" << recorder.errors
                     << ",\"raw_depth\":" << raw.depth << ",\"raw_errors\":" << raw.errors << '}';
            }
            line << "]}";
            if (!p.health_log->append(line.str()))
                p.health.error("health_log");
            if (!p.config.no_record && metrics.disk_known && metrics.low_disk)
                p.fail("disk below configured threshold; recordings retained, acquisition stopped");
            next_health = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max(1U, p.config.stats_interval_sec));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (p.stopping())
        request_stop();
    for (auto &thread : p.capture_threads)
        if (thread.joinable())
            thread.join();
    p.pair_queue.close();
    for (auto &queue : p.video_queue)
        queue->close();
    for (auto &queue : p.ai_queue.values)
        queue->close();
    for (auto &thread : p.ai_threads)
        if (thread.joinable())
            thread.join();
    for (auto &thread : p.encode_threads)
        if (thread.joinable())
            thread.join();
    for (auto &out : p.video)
        if (!out.close())
            p.fail("recording finalize failed; partial recording retained");
    p.pairer.clear();
    FramePair pending_pair;
    while (p.pair_queue.tryPop(pending_pair))
        pending_pair = FramePair{};
    for (auto &queue : p.video_queue) {
        FrameSample frame;
        while (queue->tryPop(frame))
            frame = FrameSample{};
    }
    for (auto &queue : p.ai_queue.values) {
        Impl::AiItem item;
        while (queue->tryPop(item))
            item = Impl::AiItem{};
    }
    p.events.flush();
    p.events.close();
    p.sync_diag.flush();
    p.sync_diag.close();
    bool stop_ok = true;
    for (auto &cam : p.camera) {
        std::string stop_error;
        if (!cam.stop(&stop_error)) {
            stop_ok = false;
            if (error && error->empty())
                *error = stop_error;
        }
    }
    const FramePairerStats ps = p.pairer.stats();
    {
        std::lock_guard<std::mutex> lock(p.stats_mutex);
        p.counters.pair_drops += ps.pair_drops;
        p.counters.sync_p50_ms = ps.p50_ms();
        p.counters.sync_p95_ms = ps.p95_ms();
        p.counters.sync_p99_ms = ps.p99_ms();
        p.counters.last_delta_ms = static_cast<double>(ps.last_delta_ns) / 1'000'000.0;
        p.counters.max_delta_ms = static_cast<double>(ps.max_delta_ns) / 1'000'000.0;
    }
    std::lock_guard<std::mutex> lock(p.error_mutex);
    if (error && error->empty())
        *error = p.failure;
    return p.failure.empty() && stop_ok;
}

DualCameraPipelineStats DualCameraPipeline::stats() const {
    if (!impl_)
        return {};
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    DualCameraPipelineStats out = impl_->counters;
    const FramePairerStats ps = impl_->pairer.stats();
    out.sync_p50_ms = ps.p50_ms();
    out.sync_p95_ms = ps.p95_ms();
    out.sync_p99_ms = ps.p99_ms();
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - impl_->started).count();
    if (elapsed > 0.0 && !impl_->independent())
        out.pair_fps = static_cast<double>(ps.matched_pairs) / elapsed;
    return out;
}
