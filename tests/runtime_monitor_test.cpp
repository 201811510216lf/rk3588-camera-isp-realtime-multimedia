#include "runtime/monitor.h"
#include <cassert>
#include <fstream>
#include <limits>

int main() {
    runtime::HealthMonitor monitor(100);
    monitor.heartbeat("camera0", 100, 4);
    monitor.heartbeat("camera1", 180, 2);
    monitor.error("camera0");
    auto state = monitor.snapshot(201);
    assert(state.size() == 2 && state[0].stale && !state[1].stale);
    assert(state[0].errors == 1 && state[0].queue_depth == 4);
    runtime::RetryBudget retry(3, 10, 20);
    assert(retry.allow(0) && !retry.allow(9) && retry.allow(10));
    assert(!retry.allow(29) && retry.allow(30) && !retry.allow(100));
    retry.reset();
    assert(retry.allow(101));
    auto directory = std::filesystem::current_path() / "runtime-test-output";
    std::filesystem::create_directories(directory);
    runtime::RotatingLog log(directory / "health.log", 12, 2);
    assert(log.append("abcde") && log.append("fghij") && log.append("klmno"));
    assert(std::filesystem::exists(directory / "health.log.1"));
    assert(!log.append("0123456789012"));
    runtime::RotatingLog exact(directory / "exact.log", 6, 1);
    assert(exact.append("abcde"));
    assert(std::filesystem::file_size(directory / "exact.log") == 6);
    const auto metrics = runtime::sample_system(directory, std::numeric_limits<uint64_t>::max());
    assert(metrics.disk_known && metrics.low_disk);
    std::filesystem::remove(directory / "health.log");
    std::filesystem::remove(directory / "health.log.1");
    std::filesystem::remove(directory / "health.log.2");
    std::filesystem::remove(directory / "exact.log");
    std::filesystem::remove(directory);
    return 0;
}
