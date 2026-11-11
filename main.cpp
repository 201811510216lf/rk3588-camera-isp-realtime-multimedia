/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "app_options.h"
#include "detection_fusion_manager.h"
#include "dma_alloc.hpp"
#include "rknnPool.hpp"
#include "stream_loader.h"
#include "streaming_manager.h"

#ifdef __linux__
#include "mipi_pipeline.h"
#endif

#include "im2d.h"

namespace {

std::atomic<bool> g_stop{false};

void signal_handler(int) {
    g_stop.store(true);
}

StreamLoaderManager &manager = StreamLoaderManager::getInstance();
StreamingManager streaming_manager;
DetectionFusionManager fusion_manager;
std::vector<cv::Mat> images(6);
std::vector<std::mutex> image_mutexes(6);

void combineImage(StreamLoaderManager &stream_manager) {
    cv::Mat combined_image(1080, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat last_combined_image;
    bool has_last_frame = false;
    const int target_fps = 24;
    const int frame_interval_ms = 1000 / target_fps;
    const int tile_w = 640;
    const int tile_h = 360;
    const size_t tile_size = static_cast<size_t>(tile_w) * tile_h * 3;

    struct DmaTileBuffer {
        int fd = -1;
        void *va = nullptr;
        cv::Mat view;
    };
    std::vector<DmaTileBuffer> tile_buffers(stream_manager.num_stream);
    for (int i = 0; i < stream_manager.num_stream; ++i) {
        if (dma_buf_alloc(DMA_HEAP_PATH, tile_size, &tile_buffers[i].fd,
                          &tile_buffers[i].va) == 0 && tile_buffers[i].va) {
            tile_buffers[i].view = cv::Mat(tile_h, tile_w, CV_8UC3, tile_buffers[i].va);
        }
    }

    auto last_frame_time = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
        auto current_time = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_frame_time).count();
        if (elapsed < frame_interval_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(frame_interval_ms - elapsed));
            current_time = std::chrono::steady_clock::now();
        }
        last_frame_time = current_time;

        bool has_new_frame = false;
        for (int i = 0; i < stream_manager.num_stream; ++i) {
            cv::Mat local_image;
            {
                std::lock_guard<std::mutex> lock(image_mutexes[i]);
                if (images[i].empty()) continue;
                local_image = std::move(images[i]);
                images[i] = cv::Mat();
            }

            cv::Mat resized_image;
            const int source_w = local_image.cols;
            const int source_h = local_image.rows;
            if (!tile_buffers[i].va) {
                cv::resize(local_image, resized_image, cv::Size(tile_w, tile_h));
            } else {
                rga_buffer_t src_buf = wrapbuffer_virtualaddr(
                    local_image.data, source_w, source_h, RK_FORMAT_BGR_888);
                rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
                    tile_buffers[i].va, tile_w, tile_h, RK_FORMAT_BGR_888);
                if (imresize(src_buf, dst_buf) != IM_STATUS_SUCCESS) {
                    cv::resize(local_image, resized_image, cv::Size(tile_w, tile_h));
                } else {
                    resized_image = tile_buffers[i].view;
                }
            }

            const int row = i / 2;
            const int column = i % 2;
            resized_image.copyTo(combined_image(cv::Rect(column * tile_w, row * tile_h,
                                                         tile_w, tile_h)));
            has_new_frame = true;
        }

        cv::Mat frame_to_send;
        if (has_new_frame) {
            last_combined_image = combined_image.clone();
            frame_to_send = last_combined_image;
            has_last_frame = true;
        } else if (has_last_frame) {
            frame_to_send = last_combined_image;
        } else {
            frame_to_send = combined_image.clone();
        }

        StreamingData stream_data{};
        stream_data.stream_id = 0;
        stream_data.frame = frame_to_send;
        stream_data.timestamp = std::chrono::system_clock::now();
        streaming_manager.addStreamingData(stream_data);
    }

    for (auto &buffer : tile_buffers) {
        if (buffer.fd >= 0 && buffer.va) {
            dma_buf_free(tile_size, &buffer.fd, buffer.va);
            buffer.va = nullptr;
        }
    }
}

void rknn_infer(rknn_lite *person_model, rknn_lite *helmet_model,
                rknn_lite *callplay_model, int stream_id) {
    dpool::ThreadPool pool(4);
    detect_result_group_t person_results{};
    detect_result_group_t helmet_results{};
    detect_result_group_t callplay_results{};
    detect_result_group_t empty_results{};
    std::vector<FusedDetection> last_fused;
    int frame_count = 0;
    constexpr int infer_interval = 2;

    while (!g_stop.load() && !manager.stream_loaders[stream_id]->stopFlag.load()) {
        cv::Mat source_image;
        {
            std::lock_guard<std::mutex> lock(manager.stream_loaders[stream_id]->buffer.mtx);
            if (!manager.stream_loaders[stream_id]->buffer.img.empty()) {
                source_image = manager.stream_loaders[stream_id]->buffer.img.clone();
            }
        }
        if (source_image.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        ++frame_count;
        const bool do_infer = (frame_count % infer_interval == 1) || last_fused.empty();
        if (do_infer) {
            person_model->ori_img = source_image;
            helmet_model->ori_img = source_image;
            callplay_model->ori_img = source_image;
            auto person_future = pool.submit([&] { return person_model->interf(person_results); });
            auto helmet_future = pool.submit([&] { return helmet_model->interf(helmet_results); });
            auto callplay_future = pool.submit([&] { return callplay_model->interf(callplay_results); });
            (void)person_future.get();
            (void)helmet_future.get();
            (void)callplay_future.get();
            last_fused = fusion_manager.fuseDetections(person_results, helmet_results,
                                                       empty_results, callplay_results);
        }

        cv::Mat display_image = source_image;
        fusion_manager.drawFusedDetections(display_image, last_fused);
        {
            std::lock_guard<std::mutex> lock(image_mutexes[stream_id]);
            images[stream_id] = std::move(display_image);
        }
    }
}

void cleanup_legacy(std::vector<std::unique_ptr<rknn_lite>> *models) {
    g_stop.store(true);
    for (auto *loader : manager.stream_loaders) {
        if (loader) loader->stopFlag.store(true);
    }
    for (size_t i = 0; i < manager.stream_loaders.size(); ++i) {
        manager.unload_stream(static_cast<int>(i));
    }
    for (auto &thread : manager.threads) {
        if (thread.joinable()) thread.join();
    }
    manager.stream_loaders.clear();
    manager.threads.clear();
    if (models) models->clear();
    streaming_manager.stopStreaming();
}

int run_legacy(const AppOptions &options) {
    if (options.legacy_streams < 1 || options.legacy_streams > 6) {
        std::cerr << "legacy stream count must be in [1, 6]\n";
        return 2;
    }
    manager.num_stream = options.legacy_streams;
    g_stop.store(false);

    StreamingConfig stream_config;
    stream_config.rtmp_url = "rtmp://192.168.1.25/live/livestream";
    stream_config.width = 1280;
    stream_config.height = 720;
    stream_config.fps = 24;
    stream_config.bitrate = 2000000;
    stream_config.enable_rtmp = true;
    stream_config.draw_detections = true;
    if (!streaming_manager.initialize(stream_config)) {
        std::cerr << "Failed to initialize legacy streaming manager\n";
        return 1;
    }
    streaming_manager.startStreaming();

    std::vector<std::unique_ptr<rknn_lite>> models;
    std::vector<std::thread> inference_threads;
    const char *person_model_path = "../../model/person_relu.rknn";
    const char *helmet_model_path = "../../model/helmet_relu.rknn";
    const char *callplay_model_path = "../../model/callplay_relu.rknn";

    for (int i = 0; i < manager.num_stream; ++i) {
        manager.load_stream(i);
        models.emplace_back(std::make_unique<rknn_lite>(person_model_path, 0, 1, i * 3));
        models.emplace_back(std::make_unique<rknn_lite>(helmet_model_path, 1, 2, i * 3 + 1));
        models.emplace_back(std::make_unique<rknn_lite>(callplay_model_path, 2, 2, i * 3 + 2));
        if (!models[models.size() - 3]->valid() || !models[models.size() - 2]->valid() ||
            !models[models.size() - 1]->valid()) {
            std::cerr << "legacy RKNN model initialization failed\n";
            cleanup_legacy(&models);
            return 1;
        }
        inference_threads.emplace_back(
            rknn_infer, models[models.size() - 3].get(), models[models.size() - 2].get(),
            models[models.size() - 1].get(), i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::thread reader_thread(combineImage, std::ref(manager));
    reader_thread.join();
    g_stop.store(true);
    for (auto &thread : inference_threads) {
        if (thread.joinable()) thread.join();
    }
    cleanup_legacy(&models);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    AppOptions options;
    bool show_help = false;
    std::string parse_error;
    if (!parse_app_options(argc, argv, &options, &show_help, &parse_error)) {
        std::cerr << "Argument error: " << parse_error << "\n";
        print_app_help(argv[0]);
        return 2;
    }
    if (show_help) {
        print_app_help(argv[0]);
        return 0;
    }

    if (options.source == AppOptions::Source::Legacy) {
        return run_legacy(options);
    }

#ifdef __linux__
    MipiPipelineConfig pipeline_config;
    pipeline_config.camera.device = options.device;
    pipeline_config.camera.width = options.width;
    pipeline_config.camera.height = options.height;
    pipeline_config.camera.fps = options.fps;
    pipeline_config.camera.poll_timeout_ms = options.v4l2_timeout_ms;
    pipeline_config.bitrate = options.bitrate;
    pipeline_config.output_path = options.output;
    pipeline_config.events_path = options.events_path;
    pipeline_config.model_path = options.model_path;
    pipeline_config.model_index = options.model_index;
    pipeline_config.duration_sec = options.duration_sec;
    pipeline_config.ai_enable = options.ai_enable;
    pipeline_config.ai_required = options.ai_required;
    pipeline_config.ai_fps = options.ai_fps;
    pipeline_config.simulate_ai_ms = options.simulate_ai_ms;
    pipeline_config.external_stop = &g_stop;

    MipiPipeline pipeline(std::move(pipeline_config));
    std::string pipeline_error;
    const bool success = pipeline.run(&pipeline_error);
    const MipiPipelineStats stats = pipeline.stats();
    std::cout << "mipi stats: capture=" << stats.capture_frames
              << " encode=" << stats.encoded_frames
              << " capture_drop=" << stats.capture_drop
              << " encode_drop=" << stats.encode_drop
              << " ai=" << stats.ai_processed
              << " ai_skip=" << stats.ai_skip
              << " ai_drop_oldest=" << stats.ai_drop_oldest
              << " capture_errors=" << stats.capture_errors
              << " encode_errors=" << stats.encode_errors
              << " capture_fps=" << stats.capture_fps
              << " encode_fps=" << stats.encode_fps
              << " ai_fps=" << stats.ai_fps << '\n';
    if (!success) {
        std::cerr << "MIPI pipeline failed: " << pipeline_error << '\n';
        return 1;
    }
    return 0;
#else
    std::cerr << "--source=mipi requires a Linux RK3588 target with V4L2 support\n";
    return 2;
#endif
}
