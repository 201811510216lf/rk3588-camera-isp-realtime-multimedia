#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace camera {

// A V4L2 slot is owned by the capture pool. Consumers only hold a lease; they
// must never close the DMA-BUF or mmap address themselves.
enum class BufferSlotState {
    Queued,
    Dequeued,
    InUse,
    Stopped,
};

struct CameraBufferDesc {
    uint32_t index = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourcc = 0;
    uint32_t plane_count = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    uint32_t offset_uv = 0;
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint32_t flags = 0;
    int dma_fd = -1;
    int dma_fd_uv = -1;
    void *data = nullptr;
    void *data_uv = nullptr;
    size_t data_length = 0;
    size_t data_uv_length = 0;
};

// Shared by all copies of a CameraFrame. The final lease release performs the
// only legal return-to-driver operation (QBUF) for the slot.
class BufferLeaseState : public std::enable_shared_from_this<BufferLeaseState> {
public:
    using ReturnSlot = std::function<void(uint32_t)>;

    explicit BufferLeaseState(ReturnSlot return_slot = {})
        : return_slot_(std::move(return_slot)) {}

    BufferLeaseState(const BufferLeaseState &) = delete;
    BufferLeaseState &operator=(const BufferLeaseState &) = delete;

    bool acquire(uint32_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) {
            return false;
        }
        ++in_flight_;
        (void)index;
        return true;
    }

    void release(uint32_t index) noexcept {
        ReturnSlot return_slot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
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
    bool accepting_ = true;
};

class FrameLease {
public:
    FrameLease(std::shared_ptr<BufferLeaseState> state, uint32_t index)
        : state_(std::move(state)), index_(index) {}

    ~FrameLease() { reset(); }

    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;

    void reset() noexcept {
        if (state_) {
            state_->release(index_);
            state_.reset();
        }
    }

private:
    std::shared_ptr<BufferLeaseState> state_;
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
