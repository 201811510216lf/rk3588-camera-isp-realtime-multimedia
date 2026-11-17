#include "output/packet_sink.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef __linux__
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace output {

std::string camera_publish_url(const std::string &base, uint32_t camera_id) {
    std::string result = base;
    const size_t marker = result.find_first_of("?#");
    const size_t end = marker == std::string::npos ? result.size() : marker;
    const size_t scheme = result.find("://");
    const size_t path = scheme == std::string::npos ? std::string::npos : result.find('/', scheme + 3);
    result.insert(end,
                  (path == std::string::npos || path >= end ? "/cam" : ".cam") + std::to_string(camera_id));
    return result;
}

AsyncPacketSink::AsyncPacketSink(size_t capacity, Overflow overflow, EncodedPacketDistributor::Sink consume)
    : capacity_(capacity), overflow_(overflow), consume_(std::move(consume)) {
    if (!capacity_ || !consume_)
        throw std::invalid_argument("sink requires capacity and callback");
    worker_ = std::thread(&AsyncPacketSink::run, this);
}
AsyncPacketSink::~AsyncPacketSink() { close(); }

bool AsyncPacketSink::enqueue(const EncodedPacket &packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || stats_.failed || !packet.bytes || packet.bytes->empty())
        return false;
    if (queue_.size() == capacity_) {
        if (overflow_ == Overflow::FailRecording) {
            stats_.failed = true;
            ++stats_.errors;
            ++stats_.dropped;
            return false;
        }
        stats_.dropped += queue_.size();
        queue_.clear();
        waiting_keyframe_ = true;
    }
    if (waiting_keyframe_ && !packet.keyframe) {
        ++stats_.dropped;
        return true; // Network loss is counted and isolated from recording.
    }
    if (packet.keyframe)
        waiting_keyframe_ = false;
    queue_.push_back(packet);
    ++stats_.accepted;
    stats_.high_water = std::max(stats_.high_water, queue_.size());
    ready_.notify_one();
    return true;
}

void AsyncPacketSink::run() {
    for (;;) {
        EncodedPacket packet;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
            if (queue_.empty())
                return;
            packet = std::move(queue_.front());
            queue_.pop_front();
            if (waiting_keyframe_ && !packet.keyframe) {
                ++stats_.dropped;
                continue;
            }
            if (packet.keyframe)
                waiting_keyframe_ = false;
        }
        bool ok = false;
        try {
            ok = consume_(packet);
        } catch (...) {
            ok = false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (ok)
            ++stats_.consumed;
        else {
            ++stats_.errors;
            if (overflow_ == Overflow::FailRecording) {
                stats_.failed = true;
                stats_.dropped += queue_.size();
                queue_.clear();
            } else {
                // Remove dependent P frames immediately. A producer accepting
                // a later IDR must not make these older packets usable again.
                while (!queue_.empty() && !queue_.front().keyframe) {
                    queue_.pop_front();
                    ++stats_.dropped;
                }
                waiting_keyframe_ = queue_.empty();
            }
        }
    }
}

void AsyncPacketSink::close(bool drain) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        if (!drain) {
            stats_.dropped += queue_.size();
            queue_.clear();
        }
    }
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
AsyncPacketSink::Stats AsyncPacketSink::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats result = stats_;
    result.depth = queue_.size();
    return result;
}

FfmpegProcess::FfmpegProcess() = default;
FfmpegProcess::~FfmpegProcess() { close(); }
bool FfmpegProcess::open(const std::vector<std::string> &arguments, std::string *error) {
    if (error)
        error->clear();
    if (fd_ >= 0 || pid_ >= 0 || arguments.empty()) {
        if (error)
            *error = "invalid or already running ffmpeg process";
        return false;
    }
#ifdef __linux__
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        return false;
    std::vector<char *> args;
    args.push_back(const_cast<char *>("ffmpeg"));
    for (const auto &arg : arguments)
        args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) {
        ::close(sockets[0]);
        ::close(sockets[1]);
        return false;
    }
    result = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
    if (result == 0 && sockets[0] != STDIN_FILENO)
        result = posix_spawn_file_actions_addclose(&actions, sockets[0]);
    if (result == 0 && sockets[1] != STDIN_FILENO)
        result = posix_spawn_file_actions_addclose(&actions, sockets[1]);
    pid_t child = -1;
    if (result == 0)
        result = posix_spawnp(&child, "ffmpeg", &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(sockets[1]);
    if (result != 0) {
        ::close(sockets[0]);
        if (error)
            *error = "ffmpeg spawn failed: " + std::to_string(result);
        return false;
    }
    fd_ = sockets[0];
    pid_ = static_cast<int>(child);
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(0);
        return false;
    }
    return true;
#else
    (void)arguments;
    if (error)
        *error = "BLOCKED_HARDWARE: ffmpeg pipe adapter requires Linux";
    return false;
#endif
}

bool FfmpegProcess::write(const uint8_t *data, size_t size, uint32_t timeout_ms) {
#ifdef __linux__
    if (fd_ < 0 || !data || !size)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    size_t offset = 0;
    while (offset < size) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd pfd{fd_, POLLOUT, 0};
        const int result = ::poll(&pfd, 1, static_cast<int>(std::min<int64_t>(left + 1, 1000)));
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return false;
        const ssize_t count = ::send(fd_, data + offset, size - offset, MSG_NOSIGNAL);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        if (count <= 0)
            return false;
        offset += static_cast<size_t>(count);
    }
    return true;
#else
    (void)data;
    (void)size;
    (void)timeout_ms;
    return false;
#endif
}

bool FfmpegProcess::close(uint32_t grace_ms) {
#ifdef __linux__
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (pid_ < 0)
        return true;
    int status = 0;
    auto wait_until = [&](uint32_t milliseconds) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        for (;;) {
            const pid_t result = ::waitpid(pid_, &status, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD))
                return true;
            if (result < 0 && errno != EINTR)
                return false;
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    bool graceful = wait_until(grace_ms);
    if (!graceful) {
        ::kill(pid_, SIGTERM);
        if (!wait_until(250)) {
            ::kill(pid_, SIGKILL);
            if (!wait_until(250))
                return false;
        }
    }
    pid_ = -1;
    return graceful && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)grace_ms;
    return true;
#endif
}

} // namespace output
