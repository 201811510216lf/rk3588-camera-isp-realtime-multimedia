// Test-only syscall wrapping exercises the real Linux V4L2IspCamera source.
// It is never linked into a product target and supplies no Camera input mode.
#include "camera_frame.h"
#include "v4l2_isp_camera.h"

#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <map>
#include <string>

namespace {
constexpr int camera_fd = 7123;
struct Driver {
    bool mplane = false;
    bool nv12m = false;
    bool caps_flag = true;
    bool stale_device_caps = false;
    bool export_dma = false;
    bool no_parm = false;
    bool no_enumeration = false;
    bool get_format_fails = false;
    bool bad_layout = false;
    bool small_sizeimage = false;
    bool interlaced = false;
    bool query_mismatch = false;
    bool short_payload = false;
    bool offset_overrun = false;
    bool invalid_index = false;
    bool wrong_dq_planes = false;
    bool buffer_error = false;
    bool bad_timestamp = false;
    bool timeout = false;
    bool poll_error = false;
    bool streamon_error = false;
    bool streamoff_error = false;
    int dq_error = 0;
    int mmap_fail_after = -1;
    int qbuf_fail_after = -1;
    int opened = 0;
    int closed = 0;
    int maps = 0;
    int unmaps = 0;
    int exported = 0;
    int dma_closed = 0;
    int qbufs = 0;
    int dqbufs = 0;
    int streamons = 0;
    int streamoffs = 0;
    int get_formats = 0;
    int size_enums = 0;
    int interval_enums = 0;
    bool streaming = false;
    std::array<bool, 2> queued{};
    std::map<void *, size_t> mappings;
    uint32_t fourcc() const { return nv12m ? V4L2_PIX_FMT_NV12M : V4L2_PIX_FMT_NV12; }
    void reset() {
        assert(mappings.empty());
        assert(opened == closed);
        assert(exported == dma_closed);
        *this = Driver{};
    }
} driver;
std::promise<void> *poll_entered = nullptr;
std::shared_future<void> poll_release;
int fail(int error) {
    errno = error;
    return -1;
}
V4L2IspCamera::Config config() {
    V4L2IspCamera::Config c;
    c.device = "/dev/configured-isp-output";
    c.width = 8;
    c.height = 6;
    c.buffer_count = 2;
    c.poll_timeout_ms = 20;
    return c;
}
void format(v4l2_format *f, bool actual) {
    const uint32_t width = actual ? 4U : 8U;
    const uint32_t height = actual ? 2U : 6U;
    const uint32_t field = driver.interlaced ? V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
    if (driver.mplane) {
        f->fmt.pix_mp.width = width;
        f->fmt.pix_mp.height = height;
        f->fmt.pix_mp.pixelformat = driver.fourcc();
        f->fmt.pix_mp.field = field;
        f->fmt.pix_mp.num_planes = driver.nv12m ? 2 : 1;
        for (uint32_t p = 0; p < f->fmt.pix_mp.num_planes; ++p) {
            f->fmt.pix_mp.plane_fmt[p].bytesperline = driver.bad_layout ? 2 : 6;
            f->fmt.pix_mp.plane_fmt[p].sizeimage = driver.small_sizeimage ? 1 : (p ? 16 : 32);
        }
    } else {
        f->fmt.pix.width = width;
        f->fmt.pix.height = height;
        f->fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        f->fmt.pix.field = field;
        f->fmt.pix.bytesperline = driver.bad_layout ? 2 : 6;
        f->fmt.pix.sizeimage = driver.small_sizeimage ? 1 : 32;
    }
}
} // namespace

extern "C" {
int __real_open(const char *, int, ...);
int __real_close(int);
int __real_ioctl(int, unsigned long, ...);
void *__real_mmap(void *, size_t, int, int, int, off_t);
int __real_munmap(void *, size_t);
int __real_poll(pollfd *, nfds_t, int);

int __wrap_open(const char *path, int flags, ...) {
    if (std::strcmp(path, "/dev/configured-isp-output") == 0) {
        ++driver.opened;
        return camera_fd;
    }
    // Only the Camera source calls open in this test's linked objects.
    return __real_open(path, flags, 0600);
}
int __wrap_close(int fd) {
    if (fd == camera_fd) {
        ++driver.closed;
        driver.streaming = false;
        driver.queued = {};
        return 0;
    }
    if (fd >= 8000 && fd < 8100) {
        ++driver.dma_closed;
        return 0;
    }
    return __real_close(fd);
}
void *__wrap_mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    if (fd != camera_fd)
        return __real_mmap(address, size, prot, flags, fd, offset);
    if (driver.mmap_fail_after >= 0 && driver.maps >= driver.mmap_fail_after) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void *p = std::malloc(size);
    assert(p);
    std::memset(p, 0x5a, size);
    driver.mappings[p] = size;
    ++driver.maps;
    return p;
}
int __wrap_munmap(void *address, size_t size) {
    const auto found = driver.mappings.find(address);
    if (found == driver.mappings.end())
        return __real_munmap(address, size);
    assert(found->second == size);
    driver.mappings.erase(found);
    ++driver.unmaps;
    std::free(address);
    return 0;
}
int __wrap_poll(pollfd *fds, nfds_t count, int timeout) {
    if (count != 1 || fds[0].fd != camera_fd)
        return __real_poll(fds, count, timeout);
    assert(timeout == 20);
    if (poll_entered) {
        poll_entered->set_value();
        poll_release.wait();
    }
    if (driver.timeout)
        return 0;
    fds[0].revents = driver.poll_error ? POLLERR : POLLIN;
    return 1;
}
int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *argument = va_arg(ap, void *);
    va_end(ap);
    if (fd != camera_fd)
        return __real_ioctl(fd, request, argument);
    switch (request) {
    case VIDIOC_QUERYCAP: {
        auto *c = static_cast<v4l2_capability *>(argument);
        const uint32_t capture = driver.mplane ? V4L2_CAP_VIDEO_CAPTURE_MPLANE : V4L2_CAP_VIDEO_CAPTURE;
        c->capabilities = V4L2_CAP_STREAMING | capture | (driver.caps_flag ? V4L2_CAP_DEVICE_CAPS : 0);
        c->device_caps = driver.stale_device_caps ? 0U : (V4L2_CAP_STREAMING | capture);
        return 0;
    }
    case VIDIOC_ENUM_FMT: {
        auto *f = static_cast<v4l2_fmtdesc *>(argument);
        if (f->index)
            return fail(EINVAL);
        f->pixelformat = driver.fourcc();
        return 0;
    }
    case VIDIOC_S_FMT:
        format(static_cast<v4l2_format *>(argument), false);
        return 0;
    case VIDIOC_G_FMT:
        ++driver.get_formats;
        if (driver.get_format_fails)
            return fail(EIO);
        format(static_cast<v4l2_format *>(argument), true);
        return 0;
    case VIDIOC_S_PARM:
        return driver.no_parm ? fail(ENOTTY) : 0;
    case VIDIOC_G_PARM: {
        if (driver.no_parm)
            return fail(ENOTTY);
        auto *p = static_cast<v4l2_streamparm *>(argument);
        p->parm.capture.timeperframe.numerator = 1001;
        p->parm.capture.timeperframe.denominator = 30000;
        return 0;
    }
    case VIDIOC_ENUM_FRAMESIZES: {
        ++driver.size_enums;
        auto *s = static_cast<v4l2_frmsizeenum *>(argument);
        assert(s->pixel_format == driver.fourcc());
        if (driver.no_enumeration || s->index)
            return fail(EINVAL);
        s->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        s->discrete.width = 4;
        s->discrete.height = 2;
        return 0;
    }
    case VIDIOC_ENUM_FRAMEINTERVALS: {
        ++driver.interval_enums;
        auto *i = static_cast<v4l2_frmivalenum *>(argument);
        assert(i->pixel_format == driver.fourcc() && i->width == 4 && i->height == 2);
        if (driver.no_enumeration || i->index)
            return fail(EINVAL);
        i->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        i->discrete.numerator = 1001;
        i->discrete.denominator = 30000;
        return 0;
    }
    case VIDIOC_REQBUFS: {
        auto *r = static_cast<v4l2_requestbuffers *>(argument);
        assert(r->memory == V4L2_MEMORY_MMAP && r->count == 2);
        return 0;
    }
    case VIDIOC_QUERYBUF: {
        auto *b = static_cast<v4l2_buffer *>(argument);
        if (driver.mplane) {
            b->length = driver.query_mismatch ? VIDEO_MAX_PLANES + 1 : (driver.nv12m ? 2 : 1);
            for (uint32_t p = 0; p < b->length && p < VIDEO_MAX_PLANES; ++p) {
                b->m.planes[p].length = p ? 16 : 32;
                b->m.planes[p].m.mem_offset = b->index * 8192 + p * 4096;
            }
        } else {
            b->length = 32;
            b->m.offset = b->index * 4096;
        }
        return 0;
    }
    case VIDIOC_EXPBUF: {
        if (!driver.export_dma)
            return fail(ENOTTY);
        auto *b = static_cast<v4l2_exportbuffer *>(argument);
        b->fd = 8000 + b->index * 2 + b->plane;
        ++driver.exported;
        return 0;
    }
    case VIDIOC_QBUF: {
        auto *b = static_cast<v4l2_buffer *>(argument);
        assert(b->memory == V4L2_MEMORY_MMAP && b->index < 2);
        if (driver.qbuf_fail_after >= 0 && driver.qbufs >= driver.qbuf_fail_after)
            return fail(EIO);
        assert(!driver.queued[b->index]);
        driver.queued[b->index] = true;
        ++driver.qbufs;
        return 0;
    }
    case VIDIOC_STREAMON:
        ++driver.streamons;
        if (driver.streamon_error)
            return fail(EIO);
        assert(driver.queued[0] && driver.queued[1]);
        driver.streaming = true;
        return 0;
    case VIDIOC_STREAMOFF:
        ++driver.streamoffs;
        if (driver.streamoff_error)
            return fail(EIO);
        driver.streaming = false;
        driver.queued = {};
        return 0;
    case VIDIOC_DQBUF: {
        if (driver.dq_error)
            return fail(driver.dq_error);
        auto *b = static_cast<v4l2_buffer *>(argument);
        assert(driver.streaming);
        b->index = driver.invalid_index ? 9 : 0;
        if (!driver.invalid_index) {
            assert(driver.queued[0]);
            driver.queued[0] = false;
        }
        ++driver.dqbufs;
        b->sequence = 77;
        b->timestamp.tv_sec = driver.bad_timestamp ? -1 : 123;
        b->timestamp.tv_usec = 456;
        b->flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC | V4L2_BUF_FLAG_TSTAMP_SRC_SOE |
                   (driver.buffer_error ? V4L2_BUF_FLAG_ERROR : 0);
        if (driver.mplane) {
            b->length = driver.wrong_dq_planes ? 3 : (driver.nv12m ? 2 : 1);
            for (uint32_t p = 0; p < b->length; ++p) {
                b->m.planes[p].length = p ? 16 : 32;
                b->m.planes[p].data_offset = driver.offset_overrun ? 100 : (p ? 4 : 2);
                const uint32_t payload = driver.short_payload ? 1 : (driver.nv12m ? (p ? 4 : 10) : 16);
                b->m.planes[p].bytesused = payload + b->m.planes[p].data_offset;
            }
        } else {
            b->bytesused = driver.short_payload ? 1 : 16;
        }
        return 0;
    }
    default:
        assert(false && "unexpected ioctl");
        return fail(EINVAL);
    }
}
} // extern C

namespace {
void negotiated_and_leases(bool mplane, bool nv12m, bool export_dma) {
    driver.reset();
    driver.mplane = mplane;
    driver.nv12m = nv12m;
    driver.export_dma = export_dma;
    V4L2IspCamera c(config());
    std::string error;
    assert(c.open(&error));
    const auto &caps = c.capabilities();
    assert(caps.width == 4 && caps.height == 2 && caps.stride_y == 6 && caps.stride_uv == 6);
    assert(caps.frame_interval_numerator == 1001 && caps.frame_interval_denominator == 30000 &&
           caps.fps == 30);
    assert(caps.plane_count == (nv12m ? 2 : 1) && caps.dma_buf_export == export_dma);
    assert(caps.sizeimage[0] == 32 && caps.field == V4L2_FIELD_NONE);
    assert(!caps.supported_formats.empty() && caps.supported_frame_sizes.size() == 1 &&
           caps.supported_frame_intervals.size() == 1 && caps.enumeration_warnings.empty());
    assert(driver.get_formats > 0 && driver.size_enums == 2 && driver.interval_enums == 2);
    assert(c.start(&error) && c.start(&error) && driver.qbufs == 2 && driver.streamons == 1);
    auto frame = c.dequeue(&error);
    assert(frame && error.empty() && c.in_flight() == 1 && driver.qbufs == 2);
    assert(frame->desc.sequence == 77 && frame->desc.timestamp_ns == 123000456000ULL);
    assert(frame->desc.clock_domain == camera::CameraClockDomain::Monotonic);
    assert(frame->desc.data_offset == (mplane ? 2U : 0U));
    assert(frame->desc.bytesused == (nv12m ? 10U : 16U));
    if (nv12m)
        assert(frame->desc.bytesused_uv == 4 && frame->desc.data_offset_uv == 4);
    std::vector<uint8_t> packed;
    assert(camera::copy_nv12_packed(frame->desc, &packed, &error) && packed.size() == 12);
    auto retained = frame->lease;
    frame.reset();
    assert(driver.qbufs == 2 && c.in_flight() == 1);
    retained.reset();
    assert(driver.qbufs == 3 && c.in_flight() == 0);
    assert(c.stop(&error) && c.stop(&error) && !c.is_open());
    assert(driver.unmaps == driver.maps && driver.exported == driver.dma_closed && driver.closed == 1);
}
void optional_queries() {
    driver.reset();
    driver.no_parm = true;
    driver.no_enumeration = true;
    driver.caps_flag = false;
    driver.stale_device_caps = true;
    V4L2IspCamera c(config());
    assert(c.open());
    assert(c.capabilities().fps == 0 && c.capabilities().frame_interval_denominator == 0);
    assert(c.capabilities().supported_frame_sizes.empty() && !c.capabilities().enumeration_warnings.empty());
    assert(c.stop());
}
void open_failures() {
    for (int mode = 0; mode < 7; ++mode) {
        driver.reset();
        driver.mplane = driver.nv12m = true;
        driver.export_dma = true;
        driver.bad_layout = mode == 0;
        driver.get_format_fails = mode == 1;
        driver.interlaced = mode == 2;
        driver.query_mismatch = mode == 3;
        driver.mmap_fail_after = mode == 4 ? 1 : -1;
        driver.small_sizeimage = mode == 5;
        driver.stale_device_caps = mode == 6;
        V4L2IspCamera c(config());
        std::string error;
        assert(!c.open(&error) && !error.empty() && !c.is_open());
        assert(driver.opened == driver.closed && driver.maps == driver.unmaps &&
               driver.exported == driver.dma_closed);
    }
}
void startup_failures() {
    for (int mode = 0; mode < 2; ++mode) {
        driver.reset();
        driver.streamon_error = mode == 0;
        driver.qbuf_fail_after = mode == 1 ? 1 : -1;
        V4L2IspCamera c(config());
        assert(c.open());
        std::string error;
        assert(!c.start(&error) && !error.empty());
        const int queued = driver.qbufs;
        assert(!c.start(&error) && driver.qbufs == queued);
        assert(c.stop());
    }
}
void malformed_frames() {
    for (int mode = 0; mode < 7; ++mode) {
        driver.reset();
        driver.mplane = driver.nv12m = true;
        driver.short_payload = mode == 0;
        driver.offset_overrun = mode == 1;
        driver.invalid_index = mode == 2;
        driver.wrong_dq_planes = mode == 3;
        driver.buffer_error = mode == 4;
        driver.bad_timestamp = mode == 5;
        driver.dq_error = mode == 6 ? EIO : 0;
        V4L2IspCamera c(config());
        assert(c.open() && c.start());
        std::string error;
        assert(!c.dequeue(&error) && !error.empty() && c.in_flight() == 0);
        if (mode == 2 || mode == 3 || mode == 6)
            assert(!c.is_streaming() && driver.streamoffs == 1);
        else
            assert(c.is_streaming() && driver.qbufs == 3);
        assert(c.stop());
    }
}
void runtime_and_shutdown() {
    driver.reset();
    V4L2IspCamera c(config());
    assert(c.open() && c.start());
    driver.timeout = true;
    std::string error;
    assert(!c.dequeue(&error) && c.stats().poll_timeouts == 1 && c.is_streaming());
    driver.timeout = false;
    driver.dq_error = EAGAIN;
    error = "stale";
    assert(!c.dequeue(&error) && error.empty() && c.is_streaming());
    driver.dq_error = 0;
    auto frame = c.dequeue();
    assert(frame);
    auto stopped = std::async(std::launch::async, [&] { return c.stop(); });
    // stop must wait for the final consumer instead of unmapping early.
    assert(stopped.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    frame.reset();
    assert(stopped.get() && !c.is_open() && driver.maps == driver.unmaps);

    driver.reset();
    V4L2IspCamera q(config());
    assert(q.open() && q.start());
    auto f = q.dequeue();
    driver.qbuf_fail_after = driver.qbufs;
    f.reset();
    assert(q.stats().qbuf_errors == 1 && !q.is_streaming());
    assert(!q.start());
    assert(q.stop());

    driver.reset();
    V4L2IspCamera p(config());
    assert(p.open() && p.start());
    driver.poll_error = true;
    assert(!p.dequeue(&error) && !p.is_streaming() && !error.empty());
    assert(p.stop());

    driver.reset();
    V4L2IspCamera pending(config());
    assert(pending.open() && pending.start());
    std::promise<void> entered, release;
    poll_entered = &entered;
    poll_release = release.get_future().share();
    auto dequeued = std::async(std::launch::async, [&] { return pending.dequeue(); });
    entered.get_future().wait();
    assert(pending.stop() && driver.maps == driver.unmaps);
    release.set_value();
    assert(!dequeued.get());
    poll_entered = nullptr;

    driver.reset();
    V4L2IspCamera uncertain(config());
    assert(uncertain.open() && uncertain.start());
    driver.poll_error = true;
    driver.streamoff_error = true;
    assert(!uncertain.dequeue(&error) && uncertain.is_streaming());
    assert(!uncertain.start());
    assert(!uncertain.stop(&error) && !uncertain.is_open() && !error.empty());
    assert(driver.streamoffs == 3 && driver.maps == driver.unmaps);

    driver.reset();
    V4L2IspCamera s(config());
    assert(s.open() && s.start());
    driver.streamoff_error = true;
    assert(!s.stop(&error) && !s.is_open() && !error.empty());
    assert(driver.streamoffs == 2 && driver.maps == driver.unmaps && driver.closed == 1);
}
} // namespace

int main() {
    negotiated_and_leases(false, false, false);
    negotiated_and_leases(true, false, false);
    negotiated_and_leases(true, true, true);
    optional_queries();
    open_failures();
    startup_failures();
    malformed_frames();
    runtime_and_shutdown();
    driver.reset();
    std::cout << "v4l2_capture_test: PASS (real Camera source, wrapped Linux ioctls; no hardware claim)\n";
}
