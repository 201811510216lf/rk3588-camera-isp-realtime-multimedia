#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

// No producer can exceed capacity. Closing wakes blocked producers/consumers;
// consumers may drain accepted values after close.
enum class QueuePolicy { BLOCK, DROP_OLDEST, DROP_NEWEST, LATEST_ONLY };

struct BoundedQueueStats {
    uint64_t pushed = 0;
    uint64_t popped = 0;
    uint64_t dropped = 0;
    uint64_t rejected_closed = 0;
    uint64_t timeouts = 0;
    size_t high_water_mark = 0;
};

template <typename T> class BoundedFrameQueue {
  public:
    explicit BoundedFrameQueue(size_t capacity, QueuePolicy policy = QueuePolicy::BLOCK)
        : capacity_(capacity), policy_(policy) {}

    BoundedFrameQueue(const BoundedFrameQueue &) = delete;
    BoundedFrameQueue &operator=(const BoundedFrameQueue &) = delete;

    bool push(T value, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (policy_ == QueuePolicy::BLOCK && !closed_ && capacity_ != 0 && queue_.size() >= capacity_) {
            const auto ready = [this] { return closed_ || queue_.size() < capacity_; };
            if (timeout == std::chrono::milliseconds::max())
                not_full_.wait(lock, ready);
            else if (!not_full_.wait_for(lock, timeout, ready)) {
                ++stats_.timeouts;
                return false;
            }
        }
        return enqueue_locked(std::move(value), policy_, nullptr);
    }

    bool tryPush(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return enqueue_locked(std::move(value), policy_, nullptr);
    }

    // Compatibility helper used by the existing AI/video branches.
    bool push_drop_oldest(T value, bool *dropped_oldest = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        return enqueue_locked(std::move(value), QueuePolicy::DROP_OLDEST, dropped_oldest);
    }

    bool pop(T &value) { return waitPop(value); }

    bool waitPop(T &value, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto ready = [this] { return closed_ || !queue_.empty(); };
        if (timeout == std::chrono::milliseconds::max())
            not_empty_.wait(lock, ready);
        else if (!not_empty_.wait_for(lock, timeout, ready)) {
            ++stats_.timeouts;
            return false;
        }
        return dequeue_locked(value);
    }

    bool tryPop(T &value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return dequeue_locked(value);
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    size_t capacity() const { return capacity_; }
    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }
    BoundedQueueStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

  private:
    bool enqueue_locked(T value, QueuePolicy policy, bool *dropped_oldest) {
        if (dropped_oldest)
            *dropped_oldest = false;
        if (closed_) {
            ++stats_.rejected_closed;
            return false;
        }
        if (capacity_ == 0) {
            ++stats_.dropped;
            return false;
        }
        if (policy == QueuePolicy::LATEST_ONLY && !queue_.empty()) {
            stats_.dropped += queue_.size();
            queue_.clear();
            if (dropped_oldest)
                *dropped_oldest = true;
        }
        if (queue_.size() >= capacity_) {
            if (policy != QueuePolicy::DROP_OLDEST) {
                ++stats_.dropped;
                return false;
            }
            queue_.pop_front();
            ++stats_.dropped;
            if (dropped_oldest)
                *dropped_oldest = true;
        }
        queue_.push_back(std::move(value));
        ++stats_.pushed;
        stats_.high_water_mark = std::max(stats_.high_water_mark, queue_.size());
        not_empty_.notify_one();
        return true;
    }

    bool dequeue_locked(T &value) {
        if (queue_.empty())
            return false;
        value = std::move(queue_.front());
        queue_.pop_front();
        ++stats_.popped;
        not_full_.notify_one();
        return true;
    }

    const size_t capacity_;
    const QueuePolicy policy_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    BoundedQueueStats stats_;
    bool closed_ = false;
};
