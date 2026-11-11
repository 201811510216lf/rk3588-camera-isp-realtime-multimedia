#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

// Small bounded queue used by the AI branch. It has an explicit stop state and
// an explicit drop-oldest operation, so backpressure is observable in stats.
template <typename T>
class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(size_t capacity) : capacity_(capacity) {}

    BoundedFrameQueue(const BoundedFrameQueue &) = delete;
    BoundedFrameQueue &operator=(const BoundedFrameQueue &) = delete;

    bool push_drop_oldest(T value, bool *dropped_oldest = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (dropped_oldest) {
            *dropped_oldest = false;
        }
        if (closed_ || capacity_ == 0) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
            if (dropped_oldest) {
                *dropped_oldest = true;
            }
        }
        queue_.push_back(std::move(value));
        condition_.notify_one();
        return true;
    }

    bool pop(T &value) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        condition_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<T> queue_;
    bool closed_ = false;
};
