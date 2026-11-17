#pragma once

#include "encoded_packet_fanout.h"

#include <condition_variable>
#include <deque>
#include <thread>

namespace output {

// Keep credentials and URL query/fragment intact when naming independent views.
std::string camera_publish_url(const std::string &base, uint32_t camera_id);

// Each adapter owns its worker. Callbacks must have bounded I/O or a documented
// filesystem limit; close joins them, never detaches a callback with stale state.
class AsyncPacketSink {
  public:
    enum class Overflow { FailRecording, ResyncAtKeyframe };
    struct Stats {
        uint64_t accepted = 0, consumed = 0, dropped = 0, errors = 0;
        size_t depth = 0, high_water = 0;
        bool failed = false;
    };
    AsyncPacketSink(size_t capacity, Overflow overflow, EncodedPacketDistributor::Sink consume);
    ~AsyncPacketSink();
    AsyncPacketSink(const AsyncPacketSink &) = delete;
    AsyncPacketSink &operator=(const AsyncPacketSink &) = delete;
    bool enqueue(const EncodedPacket &packet);
    void close(bool drain = true);
    Stats stats() const;

  private:
    void run();
    const size_t capacity_;
    const Overflow overflow_;
    EncodedPacketDistributor::Sink consume_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<EncodedPacket> queue_;
    bool closed_ = false, waiting_keyframe_ = false;
    Stats stats_;
    std::thread worker_;
};

// Linux child process with nonblocking input, per-packet deadline, and bounded
// graceful/TERM/KILL shutdown. Other platforms return unavailable, never success.
class FfmpegProcess {
  public:
    FfmpegProcess();
    ~FfmpegProcess();
    FfmpegProcess(const FfmpegProcess &) = delete;
    FfmpegProcess &operator=(const FfmpegProcess &) = delete;
    bool open(const std::vector<std::string> &arguments, std::string *error = nullptr);
    bool write(const uint8_t *data, size_t size, uint32_t timeout_ms = 1000);
    bool close(uint32_t grace_ms = 1500);

  private:
    int fd_ = -1;
    int pid_ = -1;
};

} // namespace output
