#pragma once

#include "camera_frame.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class V4L2IspCamera {
public:
    struct Config {
        std::string device = "/dev/video0";
        uint32_t width = 1920;
        uint32_t height = 1080;
        uint32_t fps = 30;
        uint32_t buffer_count = 6;
        uint32_t poll_timeout_ms = 1000;
        bool prefer_multiplanar = true;
    };

    struct Capabilities {
        bool multi_planar = false;
        bool dma_buf_export = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t fps = 0;
        uint32_t fourcc = 0;
        uint32_t plane_count = 0;
        uint32_t stride_y = 0;
        uint32_t stride_uv = 0;
        std::string format_name;
    };

    struct Stats {
        uint64_t captured = 0;
        uint64_t qbuf_errors = 0;
        uint64_t dqbuf_errors = 0;
        uint64_t poll_timeouts = 0;
        uint64_t error_flags = 0;
    };

    V4L2IspCamera();
    explicit V4L2IspCamera(Config config);
    ~V4L2IspCamera();

    V4L2IspCamera(const V4L2IspCamera &) = delete;
    V4L2IspCamera &operator=(const V4L2IspCamera &) = delete;

    bool open(std::string *error = nullptr);
    bool start(std::string *error = nullptr);
    std::optional<camera::CameraFrame> dequeue(std::string *error = nullptr);
    bool stop(std::string *error = nullptr);

    bool is_open() const { return static_cast<bool>(state_); }
    bool is_streaming() const;
    size_t in_flight() const;
    const Config &config() const { return config_; }
    const Capabilities &capabilities() const { return capabilities_; }
    Stats stats() const;

private:
    struct CameraState;

    Config config_;
    Capabilities capabilities_;
    std::shared_ptr<CameraState> state_;
};
