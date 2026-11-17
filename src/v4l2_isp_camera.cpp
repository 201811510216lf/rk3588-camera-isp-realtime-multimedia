#include "v4l2_isp_camera.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <utility>

#ifdef __linux__

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <mutex>
#include <vector>

namespace {

template <typename T> int xioctl(int fd, unsigned long request, T *argument) {
    int result;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string ioctl_error(const std::string &device, const char *operation) {
    std::ostringstream stream;
    stream << operation << " failed for " << device << ": errno=" << errno << " (" << std::strerror(errno)
           << ")";
    return stream.str();
}

bool is_nv12(uint32_t fourcc) { return fourcc == V4L2_PIX_FMT_NV12 || fourcc == V4L2_PIX_FMT_NV12M; }

const char *fourcc_name(uint32_t fourcc) {
    if (fourcc == V4L2_PIX_FMT_NV12)
        return "NV12";
    if (fourcc == V4L2_PIX_FMT_NV12M)
        return "NV12M";
    return "unsupported";
}

} // namespace

struct V4L2IspCamera::CameraState : public std::enable_shared_from_this<CameraState> {
    struct Plane {
        void *address = MAP_FAILED;
        size_t length = 0;
        int dma_fd = -1;
    };

    struct Slot {
        camera::BufferSlotState state = camera::BufferSlotState::Stopped;
        std::array<Plane, VIDEO_MAX_PLANES> planes{};
        uint32_t plane_count = 0;
    };

    V4L2IspCamera::Config config;
    int fd = -1;
    v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool multi_planar = false;
    bool streaming = false;
    bool cleaned = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourcc = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    uint32_t plane_count = 0;
    std::array<size_t, VIDEO_MAX_PLANES> sizeimage{};
    uint32_t fps = 0;
    std::vector<Slot> slots;
    std::shared_ptr<camera::BufferLeaseState> lease_state;
    mutable std::mutex ioctl_mutex;
    mutable std::mutex stats_mutex;
    V4L2IspCamera::Stats counters;

    ~CameraState() { shutdown(); }

    void set_qbuf_error() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        ++counters.qbuf_errors;
    }

    void set_dqbuf_error() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        ++counters.dqbuf_errors;
    }

    bool queue_slot(uint32_t index) noexcept {
        std::lock_guard<std::mutex> lock(ioctl_mutex);
        if (fd < 0 || cleaned || index >= slots.size() ||
            slots[index].state == camera::BufferSlotState::Queued) {
            return false;
        }

        v4l2_buffer buffer{};
        buffer.type = buffer_type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
        if (multi_planar) {
            buffer.length = slots[index].plane_count;
            buffer.m.planes = planes.data();
            for (uint32_t plane = 0; plane < buffer.length; ++plane) {
                planes[plane].length = slots[index].planes[plane].length;
            }
        }
        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            set_qbuf_error();
            return false;
        }
        slots[index].state = camera::BufferSlotState::Queued;
        return true;
    }

    void stop_stream_on_error() noexcept {
        std::lock_guard<std::mutex> lock(ioctl_mutex);
        if (fd < 0 || !streaming) {
            return;
        }
        v4l2_buf_type type = buffer_type;
        (void)xioctl(fd, VIDIOC_STREAMOFF, &type);
        streaming = false;
    }

    void close_dma_fds_and_maps() {
        for (Slot &slot : slots) {
            for (uint32_t plane = 0; plane < slot.plane_count; ++plane) {
                if (slot.planes[plane].address != MAP_FAILED) {
                    ::munmap(slot.planes[plane].address, slot.planes[plane].length);
                    slot.planes[plane].address = MAP_FAILED;
                }
                if (slot.planes[plane].dma_fd >= 0) {
                    ::close(slot.planes[plane].dma_fd);
                    slot.planes[plane].dma_fd = -1;
                }
            }
            slot.state = camera::BufferSlotState::Stopped;
        }
        slots.clear();
    }

    bool shutdown() {
        if (cleaned)
            return true;
        bool drained = true;
        if (lease_state) {
            lease_state->stop_accepting();
            // The normal owner stops consumers before calling stop(). The
            // timeout prevents a broken consumer from hanging process exit.
            drained = lease_state->wait_for_zero(std::chrono::milliseconds(3000));
        }
        if (!drained) {
            // Keep the fd and mappings alive through CameraFrame::owner. The
            // final shared owner will call this destructor again after release.
            return false;
        }
        std::lock_guard<std::mutex> lock(ioctl_mutex);
        if (fd >= 0 && streaming) {
            v4l2_buf_type type = buffer_type;
            (void)xioctl(fd, VIDIOC_STREAMOFF, &type);
            streaming = false;
        }
        close_dma_fds_and_maps();
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        cleaned = true;
        return true;
    }
};

V4L2IspCamera::V4L2IspCamera() : config_{} {}
V4L2IspCamera::V4L2IspCamera(Config config) : config_(std::move(config)) {}

V4L2IspCamera::~V4L2IspCamera() {
    std::string ignored;
    (void)stop(&ignored);
}

bool V4L2IspCamera::set_config(Config config, std::string *error) {
    if (state_) {
        if (error)
            *error = "cannot change V4L2 configuration while camera is open";
        return false;
    }
    if (config.device.empty() || config.camera_id > 1 || config.width == 0 || config.height == 0 ||
        (config.width & 1U) || (config.height & 1U) || config.fps == 0 || config.buffer_count < 2 ||
        (config.pixel_format != "auto" && config.pixel_format != "NV12" && config.pixel_format != "NV12M")) {
        if (error)
            *error = "invalid V4L2 configuration";
        return false;
    }
    config_ = std::move(config);
    return true;
}

bool V4L2IspCamera::open(std::string *error) {
    if (state_) {
        if (error)
            *error = "camera is already open";
        return false;
    }
    if (config_.device.empty() || config_.camera_id > 1 || config_.width == 0 || config_.height == 0 ||
        (config_.width & 1U) || (config_.height & 1U) || config_.fps == 0 || config_.buffer_count < 2 ||
        (config_.pixel_format != "auto" && config_.pixel_format != "NV12" &&
         config_.pixel_format != "NV12M")) {
        if (error)
            *error = "invalid V4L2 configuration";
        return false;
    }

    auto state = std::make_shared<CameraState>();
    state->config = config_;
    state->fd = ::open(config_.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (state->fd < 0) {
        if (error)
            *error = ioctl_error(config_.device, "open");
        return false;
    }

    v4l2_capability capability{};
    if (xioctl(state->fd, VIDIOC_QUERYCAP, &capability) < 0) {
        if (error)
            *error = ioctl_error(config_.device, "VIDIOC_QUERYCAP");
        state->shutdown();
        return false;
    }
    const uint32_t device_caps = capability.device_caps ? capability.device_caps : capability.capabilities;
    if (!(device_caps & V4L2_CAP_STREAMING)) {
        if (error)
            *error = "V4L2 node does not support streaming: " + config_.device;
        state->shutdown();
        return false;
    }

    const bool can_mplane = (device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    const bool can_single = (device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
    bool configured = false;
    const auto acceptable_format = [&](uint32_t format) {
        return is_nv12(format) &&
               (config_.pixel_format == "auto" ||
                format == (config_.pixel_format == "NV12M" ? V4L2_PIX_FMT_NV12M : V4L2_PIX_FMT_NV12));
    };

    auto configure_mplane = [&]() -> bool {
        if (!can_mplane)
            return false;
        bool has_nv12 = false;
        for (uint32_t index = 0;; ++index) {
            v4l2_fmtdesc description{};
            description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            description.index = index;
            if (xioctl(state->fd, VIDIOC_ENUM_FMT, &description) < 0)
                break;
            if (acceptable_format(description.pixelformat))
                has_nv12 = true;
        }
        if (!has_nv12)
            return false;

        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        format.fmt.pix_mp.width = config_.width;
        format.fmt.pix_mp.height = config_.height;
        format.fmt.pix_mp.pixelformat =
            config_.pixel_format == "NV12M" ? V4L2_PIX_FMT_NV12M : V4L2_PIX_FMT_NV12;
        format.fmt.pix_mp.field = V4L2_FIELD_ANY;
        if (xioctl(state->fd, VIDIOC_S_FMT, &format) < 0 ||
            !acceptable_format(format.fmt.pix_mp.pixelformat)) {
            if (config_.pixel_format != "auto")
                return false;
            format = {};
            format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            format.fmt.pix_mp.width = config_.width;
            format.fmt.pix_mp.height = config_.height;
            format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
            format.fmt.pix_mp.field = V4L2_FIELD_ANY;
            if (xioctl(state->fd, VIDIOC_S_FMT, &format) < 0 ||
                !acceptable_format(format.fmt.pix_mp.pixelformat)) {
                return false;
            }
        }
        state->buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        state->multi_planar = true;
        state->width = format.fmt.pix_mp.width;
        state->height = format.fmt.pix_mp.height;
        state->fourcc = format.fmt.pix_mp.pixelformat;
        state->plane_count = format.fmt.pix_mp.num_planes;
        if (state->plane_count != (state->fourcc == V4L2_PIX_FMT_NV12M ? 2U : 1U))
            return false;
        state->stride_y = format.fmt.pix_mp.plane_fmt[0].bytesperline;
        state->stride_uv =
            state->plane_count > 1 ? format.fmt.pix_mp.plane_fmt[1].bytesperline : state->stride_y;
        for (uint32_t plane = 0; plane < state->plane_count; ++plane) {
            state->sizeimage[plane] = format.fmt.pix_mp.plane_fmt[plane].sizeimage;
        }
        return true;
    };

    auto configure_single = [&]() -> bool {
        if (!can_single || config_.pixel_format == "NV12M")
            return false;
        bool has_nv12 = false;
        for (uint32_t index = 0;; ++index) {
            v4l2_fmtdesc description{};
            description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            description.index = index;
            if (xioctl(state->fd, VIDIOC_ENUM_FMT, &description) < 0)
                break;
            if (description.pixelformat == V4L2_PIX_FMT_NV12)
                has_nv12 = true;
        }
        if (!has_nv12)
            return false;
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = config_.width;
        format.fmt.pix.height = config_.height;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(state->fd, VIDIOC_S_FMT, &format) < 0 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12) {
            return false;
        }
        state->buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        state->multi_planar = false;
        state->width = format.fmt.pix.width;
        state->height = format.fmt.pix.height;
        state->fourcc = format.fmt.pix.pixelformat;
        state->plane_count = 1;
        state->stride_y = format.fmt.pix.bytesperline;
        state->stride_uv = state->stride_y;
        state->sizeimage[0] = format.fmt.pix.sizeimage;
        return true;
    };

    if (config_.prefer_multiplanar) {
        configured = configure_mplane() || configure_single();
    } else {
        configured = configure_single() || configure_mplane();
    }
    if (!configured) {
        if (error) {
            *error = "no supported NV12/NV12M capture format on " + config_.device;
        }
        state->shutdown();
        return false;
    }

    // Ask the driver for the actual frame interval. Some capture nodes do not
    // implement S_PARM/G_PARM; in that case keep fps=0 so callers can report
    // the negotiated rate as unknown instead of presenting the request as a
    // confirmed value.
    auto read_actual_fps = [&]() {
        v4l2_streamparm parm{};
        parm.type = state->buffer_type;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = config_.fps;
        // Request first, then read back; G_PARM alone merely reports the old rate.
        (void)xioctl(state->fd, VIDIOC_S_PARM, &parm);
        parm = {};
        parm.type = state->buffer_type;
        if (xioctl(state->fd, VIDIOC_G_PARM, &parm) == 0 && parm.parm.capture.timeperframe.numerator != 0 &&
            parm.parm.capture.timeperframe.denominator != 0) {
            const uint32_t numerator = parm.parm.capture.timeperframe.numerator;
            const uint32_t denominator = parm.parm.capture.timeperframe.denominator;
            state->fps = (denominator + numerator / 2U) / numerator;
        }
    };
    read_actual_fps();
    bool valid_sizeimage = state->plane_count > 0 && state->plane_count <= VIDEO_MAX_PLANES;
    if (valid_sizeimage) {
        for (uint32_t plane = 0; plane < state->plane_count; ++plane) {
            if (state->sizeimage[plane] == 0)
                valid_sizeimage = false;
        }
    }
    if (state->width == 0 || state->height == 0 || (state->width & 1U) != 0 || (state->height & 1U) != 0 ||
        state->plane_count == 0 || state->plane_count > VIDEO_MAX_PLANES || state->stride_y < state->width ||
        state->stride_uv < state->width || !valid_sizeimage ||
        (state->fourcc == V4L2_PIX_FMT_NV12M && state->plane_count < 2)) {
        if (error)
            *error = "V4L2 returned an invalid NV12 plane/stride layout";
        state->shutdown();
        return false;
    }

    v4l2_requestbuffers request{};
    request.count = config_.buffer_count;
    request.type = state->buffer_type;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(state->fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
        if (error)
            *error = ioctl_error(config_.device, "VIDIOC_REQBUFS");
        state->shutdown();
        return false;
    }

    state->slots.resize(request.count);
    bool all_dma_exported = true;
    for (uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = state->buffer_type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
        if (state->multi_planar) {
            buffer.length = VIDEO_MAX_PLANES;
            buffer.m.planes = planes.data();
        }
        if (xioctl(state->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            if (error)
                *error = ioctl_error(config_.device, "VIDIOC_QUERYBUF");
            state->shutdown();
            return false;
        }

        state->slots[index].plane_count = state->multi_planar ? buffer.length : 1;
        if (state->slots[index].plane_count == 0 || state->slots[index].plane_count != state->plane_count) {
            if (error)
                *error = "VIDIOC_QUERYBUF returned an invalid plane count";
            state->shutdown();
            return false;
        }
        for (uint32_t plane = 0; plane < state->slots[index].plane_count; ++plane) {
            const size_t length = state->multi_planar ? planes[plane].length : buffer.length;
            if (length == 0 || (state->sizeimage[plane] != 0 && length < state->sizeimage[plane])) {
                if (error)
                    *error = "V4L2 buffer is smaller than negotiated sizeimage";
                state->shutdown();
                return false;
            }
            const off_t offset =
                state->multi_planar ? planes[plane].m.mem_offset : static_cast<off_t>(buffer.m.offset);
            void *address = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, state->fd, offset);
            if (address == MAP_FAILED) {
                if (error)
                    *error = ioctl_error(config_.device, "mmap");
                state->shutdown();
                return false;
            }
            state->slots[index].planes[plane].address = address;
            state->slots[index].planes[plane].length = length;

            v4l2_exportbuffer export_buffer{};
            export_buffer.type = state->buffer_type;
            export_buffer.index = index;
            export_buffer.plane = plane;
            export_buffer.flags = O_CLOEXEC | O_RDWR;
            if (xioctl(state->fd, VIDIOC_EXPBUF, &export_buffer) == 0) {
                state->slots[index].planes[plane].dma_fd = export_buffer.fd;
            } else {
                all_dma_exported = false;
            }
        }
        state->slots[index].state = camera::BufferSlotState::Stopped;
    }

    state->lease_state = std::make_shared<camera::BufferLeaseState>(
        [weak_state = std::weak_ptr<CameraState>(state)](uint32_t index) {
            if (auto locked = weak_state.lock()) {
                (void)locked->queue_slot(index);
            }
        });

    capabilities_.multi_planar = state->multi_planar;
    capabilities_.dma_buf_export = all_dma_exported;
    capabilities_.width = state->width;
    capabilities_.height = state->height;
    capabilities_.fps = state->fps;
    capabilities_.fourcc = state->fourcc;
    capabilities_.plane_count = state->plane_count;
    capabilities_.stride_y = state->stride_y;
    capabilities_.stride_uv = state->stride_uv;
    capabilities_.format_name = fourcc_name(state->fourcc);
    std::cout << "V4L2 " << config_.device << ": " << capabilities_.format_name << " " << capabilities_.width
              << "x" << capabilities_.height << " planes=" << capabilities_.plane_count
              << " stride_y=" << capabilities_.stride_y << " stride_uv=" << capabilities_.stride_uv
              << " memory=MMAP dma_buf_export=" << (all_dma_exported ? "yes" : "no") << '\n';
    if (!all_dma_exported) {
        std::cerr << "V4L2 node " << config_.device
                  << " does not export DMA-BUF for every plane; using mmap + copy, not zero-copy\n";
    }

    state_ = std::move(state);
    return true;
}

bool V4L2IspCamera::start(std::string *error) {
    if (!state_) {
        if (error)
            *error = "camera is not open";
        return false;
    }
    if (state_->streaming)
        return true;
    for (uint32_t index = 0; index < state_->slots.size(); ++index) {
        if (!state_->queue_slot(index)) {
            if (error)
                *error = "VIDIOC_QBUF failed while starting camera";
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
    v4l2_buf_type type = state_->buffer_type;
    if (xioctl(state_->fd, VIDIOC_STREAMON, &type) < 0) {
        if (error)
            *error = ioctl_error(config_.device, "VIDIOC_STREAMON");
        return false;
    }
    state_->streaming = true;
    return true;
}

std::optional<camera::CameraFrame> V4L2IspCamera::dequeue(std::string *error) {
    if (!state_ || !state_->streaming) {
        if (error)
            *error = "camera is not streaming";
        return std::nullopt;
    }

    pollfd descriptor{};
    descriptor.fd = state_->fd;
    descriptor.events = POLLIN | POLLERR | POLLHUP;
    const int poll_result = ::poll(&descriptor, 1, static_cast<int>(config_.poll_timeout_ms));
    if (poll_result == 0) {
        std::lock_guard<std::mutex> lock(state_->stats_mutex);
        ++state_->counters.poll_timeouts;
        if (error)
            *error = "poll timeout waiting for " + config_.device;
        return std::nullopt;
    }
    if (poll_result < 0) {
        if (errno == EINTR)
            return std::nullopt;
        if (error)
            *error = ioctl_error(config_.device, "poll");
        return std::nullopt;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
        (descriptor.revents & (POLLIN | POLLRDNORM)) == 0) {
        state_->set_dqbuf_error();
        state_->stop_stream_on_error();
        if (error) {
            std::ostringstream stream;
            stream << "poll returned capture error events: revents=0x" << std::hex << descriptor.revents;
            *error = stream.str();
        }
        return std::nullopt;
    }

    v4l2_buffer buffer{};
    buffer.type = state_->buffer_type;
    buffer.memory = V4L2_MEMORY_MMAP;
    std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
    if (state_->multi_planar) {
        buffer.length = VIDEO_MAX_PLANES;
        buffer.m.planes = planes.data();
    }
    int dequeue_result = 0;
    {
        std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
        dequeue_result = xioctl(state_->fd, VIDIOC_DQBUF, &buffer);
    }
    if (dequeue_result < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return std::nullopt;
        state_->set_dqbuf_error();
        if (error)
            *error = ioctl_error(config_.device, "VIDIOC_DQBUF");
        return std::nullopt;
    }
    if (buffer.index >= state_->slots.size()) {
        state_->set_dqbuf_error();
        state_->stop_stream_on_error();
        if (error)
            *error = "VIDIOC_DQBUF returned an invalid buffer index";
        return std::nullopt;
    }
    if (state_->multi_planar && buffer.length != state_->plane_count) {
        state_->set_dqbuf_error();
        state_->stop_stream_on_error();
        if (error)
            *error = "VIDIOC_DQBUF returned an unexpected plane count";
        return std::nullopt;
    }
    bool slot_was_queued = false;
    {
        std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
        slot_was_queued = state_->slots[buffer.index].state == camera::BufferSlotState::Queued;
        if (slot_was_queued)
            state_->slots[buffer.index].state = camera::BufferSlotState::Dequeued;
    }
    if (!slot_was_queued) {
        state_->set_dqbuf_error();
        state_->stop_stream_on_error();
        if (error)
            *error = "VIDIOC_DQBUF returned a slot that was not queued";
        return std::nullopt;
    }
    if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
        {
            std::lock_guard<std::mutex> lock(state_->stats_mutex);
            ++state_->counters.error_flags;
        }
        (void)state_->queue_slot(buffer.index);
        if (error)
            *error = "V4L2 buffer returned with V4L2_BUF_FLAG_ERROR";
        return std::nullopt;
    }

    // Validate each payload before exposing its mapped address. data_offset
    // and bytesused are returned by the driver for every multi-planar buffer;
    // single-planar NV12 reports the combined payload in buffer.bytesused.
    const uint32_t plane_count = state_->slots[buffer.index].plane_count;
    std::array<size_t, VIDEO_MAX_PLANES> offsets{};
    std::array<size_t, VIDEO_MAX_PLANES> used{};
    for (uint32_t plane = 0; plane < plane_count; ++plane) {
        const size_t offset = state_->multi_planar ? planes[plane].data_offset : 0;
        const size_t bytesused = state_->multi_planar ? planes[plane].bytesused : buffer.bytesused;
        const size_t mapped_length = state_->slots[buffer.index].planes[plane].length;
        if (offset > mapped_length ||
            (state_->sizeimage[plane] != 0 &&
             (offset > state_->sizeimage[plane] || bytesused > state_->sizeimage[plane])) ||
            bytesused < offset || bytesused - offset > mapped_length - offset) {
            state_->set_dqbuf_error();
            (void)state_->queue_slot(buffer.index);
            if (error)
                *error = "V4L2 DQBUF returned an invalid plane offset/bytesused";
            return std::nullopt;
        }
        offsets[plane] = offset;
        used[plane] = bytesused - offset;
    }
    const size_t required_y = static_cast<size_t>(state_->stride_y) * state_->height;
    const size_t required_uv = static_cast<size_t>(state_->stride_uv) * (state_->height / 2);
    if (used[0] < required_y ||
        (plane_count > 1 ? used[1] < required_uv : used[0] < required_y + required_uv)) {
        state_->set_dqbuf_error();
        (void)state_->queue_slot(buffer.index);
        if (error)
            *error = "V4L2 DQBUF payload is smaller than negotiated NV12 layout";
        return std::nullopt;
    }
    if (!state_->lease_state->acquire(buffer.index)) {
        (void)state_->queue_slot(buffer.index);
        if (error)
            *error = "camera is stopping; frame lease rejected";
        return std::nullopt;
    }
    {
        std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
        state_->slots[buffer.index].state = camera::BufferSlotState::InUse;
    }

    camera::CameraFrame frame;
    frame.desc.camera_id = config_.camera_id;
    frame.desc.index = buffer.index;
    frame.desc.width = state_->width;
    frame.desc.height = state_->height;
    frame.desc.fourcc = state_->fourcc;
    frame.desc.plane_count = state_->slots[buffer.index].plane_count;
    frame.desc.stride_y = state_->stride_y;
    frame.desc.stride_uv = state_->stride_uv;
    frame.desc.offset_uv = state_->plane_count > 1 ? 0 : state_->stride_y * state_->height;
    frame.desc.sequence = buffer.sequence;
    frame.desc.timestamp_ns = static_cast<uint64_t>(buffer.timestamp.tv_sec) * 1000000000ULL +
                              static_cast<uint64_t>(buffer.timestamp.tv_usec) * 1000ULL;
    frame.desc.monotonic_timestamp_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());
    frame.desc.flags = buffer.flags;
    frame.desc.timestamp_flags = buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
    frame.desc.timestamp_source_flags = buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
#ifdef V4L2_BUF_FLAG_TSTAMP_SRC_MASK
    frame.desc.timestamp_source_flags |= buffer.flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
#endif
#ifdef V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
    frame.desc.clock_domain = (buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
                                  ? camera::CameraClockDomain::Monotonic
                                  : camera::CameraClockDomain::Unknown;
#else
    frame.desc.clock_domain = camera::CameraClockDomain::Unknown;
#endif
    frame.desc.data = static_cast<uint8_t *>(state_->slots[buffer.index].planes[0].address) + offsets[0];
    frame.desc.data_length = used[0];
    frame.desc.mapped_length = state_->slots[buffer.index].planes[0].length;
    frame.desc.bytesused = used[0];
    frame.desc.data_offset = offsets[0];
    frame.desc.sizeimage = state_->sizeimage[0] > offsets[0] ? state_->sizeimage[0] - offsets[0] : 0;
    frame.desc.dma_fd = state_->slots[buffer.index].planes[0].dma_fd;
    if (state_->slots[buffer.index].plane_count > 1) {
        frame.desc.data_uv =
            static_cast<uint8_t *>(state_->slots[buffer.index].planes[1].address) + offsets[1];
        frame.desc.data_uv_length = used[1];
        frame.desc.mapped_length_uv = state_->slots[buffer.index].planes[1].length;
        frame.desc.bytesused_uv = used[1];
        frame.desc.data_offset_uv = offsets[1];
        frame.desc.sizeimage_uv = state_->sizeimage[1] > offsets[1] ? state_->sizeimage[1] - offsets[1] : 0;
        frame.desc.dma_fd_uv = state_->slots[buffer.index].planes[1].dma_fd;
    } else {
        frame.desc.data_uv = static_cast<uint8_t *>(frame.desc.data) + frame.desc.offset_uv;
        frame.desc.data_uv_length = frame.desc.data_length - frame.desc.offset_uv;
        frame.desc.mapped_length_uv = frame.desc.mapped_length;
        frame.desc.bytesused_uv = frame.desc.data_uv_length;
        frame.desc.data_offset_uv = offsets[0] + frame.desc.offset_uv;
        frame.desc.sizeimage_uv =
            frame.desc.sizeimage > frame.desc.offset_uv ? frame.desc.sizeimage - frame.desc.offset_uv : 0;
        frame.desc.dma_fd_uv = frame.desc.dma_fd;
    }
    frame.owner = state_;
    try {
        frame.lease = std::make_shared<camera::FrameLease>(state_->lease_state, buffer.index, state_);
    } catch (const std::exception &exception) {
        state_->lease_state->release(buffer.index);
        if (error)
            *error = std::string("cannot allocate camera frame lease: ") + exception.what();
        return std::nullopt;
    }
    {
        std::lock_guard<std::mutex> lock(state_->stats_mutex);
        ++state_->counters.captured;
    }
    return frame;
}

bool V4L2IspCamera::stop(std::string *error) {
    if (!state_)
        return true;
    state_->lease_state->stop_accepting();
    if (!state_->lease_state->wait_for_zero(std::chrono::milliseconds(3000))) {
        if (error) {
            *error = "timed out waiting for camera consumers; buffers remain owned until leases release";
        }
        return false;
    }
    bool streamoff_ok = true;
    {
        std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
        if (state_->fd >= 0 && state_->streaming) {
            v4l2_buf_type type = state_->buffer_type;
            if (xioctl(state_->fd, VIDIOC_STREAMOFF, &type) < 0 && errno != EINVAL) {
                if (error)
                    *error = ioctl_error(config_.device, "VIDIOC_STREAMOFF");
                streamoff_ok = false;
                // Leave the state marked streaming so shutdown() retries the
                // ioctl before it force-closes mappings and the fd.
            } else {
                state_->streaming = false;
            }
        }
    }
    // STREAMOFF failure leaves the driver state uncertain, but userspace must
    // still release mappings and the fd. shutdown() repeats STREAMOFF only if
    // streaming is still marked true, then performs the unconditional close.
    (void)state_->shutdown();
    state_.reset();
    return streamoff_ok;
}

bool V4L2IspCamera::is_streaming() const {
    if (!state_)
        return false;
    std::lock_guard<std::mutex> lock(state_->ioctl_mutex);
    return state_->streaming;
}

size_t V4L2IspCamera::in_flight() const {
    return state_ && state_->lease_state ? state_->lease_state->in_flight() : 0;
}

V4L2IspCamera::Stats V4L2IspCamera::stats() const {
    if (!state_)
        return {};
    std::lock_guard<std::mutex> lock(state_->stats_mutex);
    return state_->counters;
}

#else

V4L2IspCamera::V4L2IspCamera() : config_{} {}
V4L2IspCamera::V4L2IspCamera(Config config) : config_(std::move(config)) {}
V4L2IspCamera::~V4L2IspCamera() = default;

bool V4L2IspCamera::set_config(Config config, std::string *error) {
    if (state_) {
        if (error)
            *error = "cannot change V4L2 configuration while camera is open";
        return false;
    }
    if (config.device.empty() || config.camera_id > 1 || config.width == 0 || config.height == 0 ||
        (config.width & 1U) || (config.height & 1U) || config.fps == 0 || config.buffer_count < 2 ||
        (config.pixel_format != "auto" && config.pixel_format != "NV12" && config.pixel_format != "NV12M")) {
        if (error)
            *error = "invalid V4L2 configuration";
        return false;
    }
    config_ = std::move(config);
    return true;
}

bool V4L2IspCamera::open(std::string *error) {
    if (error)
        *error = "V4L2 ISP camera requires a Linux target";
    return false;
}
bool V4L2IspCamera::start(std::string *error) {
    if (error)
        *error = "V4L2 ISP camera requires a Linux target";
    return false;
}
std::optional<camera::CameraFrame> V4L2IspCamera::dequeue(std::string *error) {
    if (error)
        *error = "V4L2 ISP camera requires a Linux target";
    return std::nullopt;
}
bool V4L2IspCamera::stop(std::string *) { return true; }
bool V4L2IspCamera::is_streaming() const { return false; }
size_t V4L2IspCamera::in_flight() const { return 0; }
V4L2IspCamera::Stats V4L2IspCamera::stats() const { return {}; }

#endif
