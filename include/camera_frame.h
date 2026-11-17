#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camera {

enum class CameraClockDomain : uint8_t {
    Unknown = 0,
    Monotonic = 1,
    Realtime = 2,
    Sensor = 3,
};

// A V4L2 slot is owned by the capture pool. Consumers only hold a lease; they
// must never close the DMA-BUF or mmap address themselves.
enum class BufferSlotState {
    Queued,
    Dequeued,
    InUse,
    Stopped,
};

struct CameraBufferDesc {
    uint32_t camera_id = 0;
    uint32_t index = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourcc = 0;
    uint32_t plane_count = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    uint32_t offset_uv = 0;
    // Bytes available from each data pointer after the driver's data_offset.
    // These are bounded by bytesused, not just the mmap length, so consumers
    // cannot accidentally read padding or an incomplete capture buffer.
    size_t bytesused = 0;
    size_t bytesused_uv = 0;
    size_t sizeimage = 0;
    size_t sizeimage_uv = 0;
    size_t data_offset = 0;
    size_t data_offset_uv = 0;
    uint32_t timestamp_flags = 0;
    // Raw V4L2 timestamp source bits retained for diagnostics. A value of
    // zero means that the driver did not expose a trustworthy source.
    uint32_t timestamp_source_flags = 0;
    CameraClockDomain clock_domain = CameraClockDomain::Unknown;
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0; // Driver source timestamp, kept for API compatibility.
    uint64_t source_timestamp_ns() const { return timestamp_ns; }
    // Local dequeue observation, never a replacement for the exposure timestamp.
    uint64_t monotonic_timestamp_ns = 0;
    uint32_t flags = 0;
    int dma_fd = -1;
    int dma_fd_uv = -1;
    void *data = nullptr;
    void *data_uv = nullptr;
    size_t data_length = 0;
    size_t data_uv_length = 0;
    size_t mapped_length = 0;
    size_t mapped_length_uv = 0;
};

constexpr uint32_t kNv12Fourcc = 0x3231564eU;
constexpr uint32_t kNv12MFourcc = 0x32314d4eU;

// Validate the actual payload spans, including stride and separate NV12M planes.
// This is also usable with memory fixtures; it never creates a Camera source.
bool validate_nv12_layout(const CameraBufferDesc &desc, std::string *error = nullptr);
bool copy_nv12_packed(const CameraBufferDesc &desc, std::vector<uint8_t> *packed,
                      std::string *error = nullptr);

// Shared by all copies of a CameraFrame. The final lease release performs the
// only legal return-to-driver operation (QBUF) for the slot.
class BufferLeaseState : public std::enable_shared_from_this<BufferLeaseState> {
  public:
    using ReturnSlot = std::function<void(uint32_t)>;

    explicit BufferLeaseState(ReturnSlot return_slot = {}) : return_slot_(std::move(return_slot)) {}

    BufferLeaseState(const BufferLeaseState &) = delete;
    BufferLeaseState &operator=(const BufferLeaseState &) = delete;

    bool acquire(uint32_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_ || !active_slots_.insert(index).second) {
            return false;
        }
        ++in_flight_;
        return true;
    }

    void release(uint32_t index) noexcept {
        ReturnSlot return_slot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_slots_.erase(index) == 0)
                return;
            if (accepting_) {
                return_slot = return_slot_;
            }
        }

        // QBUF must happen before the lease is counted as gone. This prevents
        // stop() from unmapping a slot while the driver is still being given it.
        if (return_slot) {
            try {
                return_slot(index);
            } catch (...) {
                // A noexcept lease destructor cannot propagate an ioctl error.
                // The owner records the error in its normal QBUF path.
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (in_flight_ > 0) {
                --in_flight_;
            }
        }
        condition_.notify_all();
    }

    void stop_accepting() {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
    }

    bool wait_for_zero(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return in_flight_ == 0; });
    }

    size_t in_flight() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return in_flight_;
    }

    bool accepting() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepting_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    ReturnSlot return_slot_;
    size_t in_flight_ = 0;
    std::unordered_set<uint32_t> active_slots_;
    bool accepting_ = true;
};

class FrameLease {
  public:
    FrameLease(std::shared_ptr<BufferLeaseState> state, uint32_t index, std::shared_ptr<void> owner = {})
        : state_(std::move(state)), owner_(std::move(owner)), index_(index) {}

    ~FrameLease() { reset(); }

    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;

    void reset() noexcept {
        if (state_) {
            state_->release(index_);
            state_.reset();
        }
        owner_.reset();
    }

  private:
    std::shared_ptr<BufferLeaseState> state_;
    // Keep the CameraState alive even when a caller retains only the lease.
    // This prevents owner/lease lifetime splitting from unmapping a slot while
    // a downstream queue still owns a frame.
    std::shared_ptr<void> owner_;
    uint32_t index_ = 0;
};

struct CameraFrame {
    CameraBufferDesc desc;
    // Keeps the mmap/DMA-BUF owner alive when a frame outlives the camera object.
    std::shared_ptr<void> owner;
    std::shared_ptr<FrameLease> lease;

    bool valid() const { return static_cast<bool>(lease) && desc.data != nullptr; }
};

} // namespace camera
