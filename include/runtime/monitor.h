#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace runtime {
struct Health {
    std::string name;
    uint64_t heartbeat_ns = 0;
    uint64_t errors = 0;
    size_t queue_depth = 0;
    bool stale = true;
};
struct SystemMetrics {
    uint64_t rss_bytes = 0;
    double cpu_seconds = 0;
    uint64_t disk_available_bytes = 0;
    bool disk_known = false;
    bool low_disk = false;
    int temperature_millidegrees = -1;
    int frequency_khz = -1;
};
// Callers supply a monotonic timestamp; wall-clock never controls watchdogs.
class HealthMonitor {
  public:
    explicit HealthMonitor(uint64_t timeout_ns = 5'000'000'000ULL);
    void heartbeat(const std::string &name, uint64_t now_ns, size_t queue_depth = 0);
    void error(const std::string &name);
    std::vector<Health> snapshot(uint64_t now_ns) const;

  private:
    uint64_t timeout_ns_;
    mutable std::mutex mutex_;
    std::map<std::string, Health> components_;
};
// Permission to retry is bounded and exponentially delayed. Does not restart a device itself.
class RetryBudget {
  public:
    RetryBudget(unsigned max_attempts, uint64_t initial_delay_ns, uint64_t max_delay_ns);
    bool allow(uint64_t now_ns);
    void reset();
    unsigned attempts() const;

  private:
    unsigned max_attempts_, attempts_ = 0;
    uint64_t initial_delay_, max_delay_, next_ns_ = 0;
};
SystemMetrics sample_system(const std::filesystem::path &storage, uint64_t minimum_free_bytes);
// One writer; rename rotation preserves bounded backups, never touches recordings.
class RotatingLog {
  public:
    RotatingLog(std::filesystem::path path, uint64_t max_bytes, unsigned backups = 3);
    bool append(const std::string &line);

  private:
    std::filesystem::path path_;
    uint64_t max_bytes_;
    unsigned backups_;
    std::mutex mutex_;
};
} // namespace runtime
