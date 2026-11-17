#include "runtime/monitor.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef __linux__
#include <unistd.h>
#endif

namespace runtime {
HealthMonitor::HealthMonitor(uint64_t timeout_ns) : timeout_ns_(timeout_ns) {}
void HealthMonitor::heartbeat(const std::string &name, uint64_t now_ns, size_t depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &value = components_[name];
    value.name = name;
    value.heartbeat_ns = now_ns;
    value.queue_depth = depth;
    value.stale = false;
}
void HealthMonitor::error(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &value = components_[name];
    value.name = name;
    ++value.errors;
}
std::vector<Health> HealthMonitor::snapshot(uint64_t now_ns) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Health> result;
    for (const auto &entry : components_) {
        auto value = entry.second;
        value.stale = value.stale || now_ns < value.heartbeat_ns || now_ns - value.heartbeat_ns > timeout_ns_;
        result.push_back(std::move(value));
    }
    return result;
}
RetryBudget::RetryBudget(unsigned count, uint64_t initial, uint64_t maximum)
    : max_attempts_(count), initial_delay_(initial), max_delay_(maximum) {
    if (!initial || maximum < initial)
        throw std::invalid_argument("invalid retry backoff");
}
bool RetryBudget::allow(uint64_t now_ns) {
    if (attempts_ >= max_attempts_ || now_ns < next_ns_)
        return false;
    uint64_t delay = initial_delay_;
    for (unsigned i = 0; i < attempts_ && delay < max_delay_; ++i)
        delay = delay > max_delay_ / 2 ? max_delay_ : std::min(max_delay_, delay * 2);
    next_ns_ = now_ns > std::numeric_limits<uint64_t>::max() - delay ? std::numeric_limits<uint64_t>::max()
                                                                     : now_ns + delay;
    ++attempts_;
    return true;
}
void RetryBudget::reset() {
    attempts_ = 0;
    next_ns_ = 0;
}
unsigned RetryBudget::attempts() const { return attempts_; }
SystemMetrics sample_system(const std::filesystem::path &storage, uint64_t minimum) {
    SystemMetrics result;
    result.cpu_seconds = static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
    std::error_code ec;
    const auto space = std::filesystem::space(storage, ec);
    if (!ec) {
        result.disk_known = true;
        result.disk_available_bytes = space.available;
        result.low_disk = space.available < minimum;
    }
#ifdef __linux__
    uint64_t total_pages = 0, resident_pages = 0;
    std::ifstream statm("/proc/self/statm");
    if (statm >> total_pages >> resident_pages) {
        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0)
            result.rss_bytes = resident_pages * static_cast<uint64_t>(page_size);
    }
    std::ifstream temperature("/sys/class/thermal/thermal_zone0/temp");
    temperature >> result.temperature_millidegrees;
    std::ifstream frequency("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    frequency >> result.frequency_khz;
#endif
    return result;
}
RotatingLog::RotatingLog(std::filesystem::path path, uint64_t max_bytes, unsigned backups)
    : path_(std::move(path)), max_bytes_(max_bytes), backups_(backups) {
    if (!max_bytes || !backups)
        throw std::invalid_argument("log limits must be positive");
}
bool RotatingLog::append(const std::string &line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (line.size() >= max_bytes_)
        return false;
    std::error_code ec;
    if (!path_.parent_path().empty())
        std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec)
        return false;
    uint64_t size = std::filesystem::exists(path_, ec) ? std::filesystem::file_size(path_, ec) : 0;
    if (ec)
        return false;
    if (size + line.size() + 1 > max_bytes_) {
        auto last = path_;
        last += "." + std::to_string(backups_);
        std::filesystem::remove(last, ec);
        if (ec)
            return false;
        for (unsigned i = backups_; i > 0; --i) {
            auto from = path_;
            if (i > 1)
                from += "." + std::to_string(i - 1);
            auto to = path_;
            to += "." + std::to_string(i);
            if (std::filesystem::exists(from, ec))
                std::filesystem::rename(from, to, ec);
            if (ec)
                return false;
        }
    }
    std::ofstream output(path_, std::ios::app | std::ios::binary);
    output << line << '\n';
    output.flush();
    return output.good();
}
} // namespace runtime
