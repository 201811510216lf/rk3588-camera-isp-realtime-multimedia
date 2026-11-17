#include "bounded_frame_queue.h"
#include "camera_frame.h"
#include "frame_pairer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
void layout_and_lifetime() {
    std::vector<uint8_t> plane(18, 0xee);
    for (size_t i = 0; i < 4; ++i) {
        plane[i] = static_cast<uint8_t>(i + 1);
        plane[6 + i] = static_cast<uint8_t>(i + 5);
        plane[12 + i] = static_cast<uint8_t>(i + 9);
    }
    camera::CameraBufferDesc d;
    d.width = 4;
    d.height = 2;
    d.fourcc = camera::kNv12Fourcc;
    d.plane_count = 1;
    d.stride_y = 6;
    d.stride_uv = 6;
    d.offset_uv = 12;
    d.data = plane.data();
    d.data_length = 18;
    d.bytesused = 16;
    d.mapped_length = 18;
    std::vector<uint8_t> packed;
    assert(camera::copy_nv12_packed(d, &packed));
    for (size_t i = 0; i < packed.size(); ++i)
        assert(packed[i] == i + 1);
    assert(packed.size() == 12);
    d.bytesused = 15;
    assert(!camera::validate_nv12_layout(d));
    d.bytesused = 16;
    d.offset_uv = 10;
    assert(!camera::validate_nv12_layout(d));
    d.offset_uv = 12;
    d.stride_uv = 3;
    assert(!camera::validate_nv12_layout(d));
    d.stride_uv = 6;
    d.fourcc = camera::kNv12MFourcc;
    d.plane_count = 2;
    d.data_uv = plane.data() + 12;
    d.data_length = 12;
    d.bytesused = 10;
    d.data_uv_length = 6;
    d.bytesused_uv = 4;
    d.mapped_length_uv = 6;
    assert(camera::copy_nv12_packed(d, &packed));
    for (size_t i = 0; i < packed.size(); ++i)
        assert(packed[i] == i + 1);
    d.bytesused_uv = 7;
    assert(!camera::validate_nv12_layout(d));
    d.bytesused_uv = 4;
    d.data_offset_uv = 3;
    assert(!camera::validate_nv12_layout(d));
    d.data_offset_uv = 0;
    d.plane_count = 1;
    assert(!camera::validate_nv12_layout(d));

    unsigned requeues = 0;
    auto state = std::make_shared<camera::BufferLeaseState>([&](uint32_t i) {
        assert(i == 1);
        ++requeues;
    });
    assert(state->acquire(1));
    assert(!state->acquire(1));
    auto lease = std::make_shared<camera::FrameLease>(state, 1);
    auto shared = lease;
    lease.reset();
    assert(requeues == 0);
    shared->reset();
    shared->reset();
    shared.reset();
    assert(requeues == 1 && state->in_flight() == 0);
    state->release(1);
    assert(requeues == 1);
}

void policies_and_close() {
    int v = 0;
    BoundedFrameQueue<int> oldest(2, QueuePolicy::DROP_OLDEST);
    assert(oldest.push(1) && oldest.push(2) && oldest.push(3));
    assert(oldest.tryPop(v) && v == 2);
    assert(oldest.tryPop(v) && v == 3);
    assert(oldest.stats().dropped == 1 && oldest.stats().high_water_mark == 2);
    BoundedFrameQueue<int> newest(1, QueuePolicy::DROP_NEWEST);
    assert(newest.push(7));
    assert(!newest.tryPush(8));
    assert(newest.pop(v) && v == 7);
    BoundedFrameQueue<int> latest(5, QueuePolicy::LATEST_ONLY);
    for (int i = 0; i < 10; ++i)
        assert(latest.push(i));
    assert(latest.size() == 1 && latest.pop(v) && v == 9 && latest.stats().dropped == 9);
    BoundedFrameQueue<int> zero(0);
    assert(!zero.push(1));
    BoundedFrameQueue<int> blocking(1);
    assert(!blocking.waitPop(v, 1ms));
    assert(blocking.push(1));
    assert(!blocking.push(2, 1ms));
    auto producer = std::async(std::launch::async, [&] { return blocking.push(3); });
    blocking.close();
    assert(producer.wait_for(1s) == std::future_status::ready && !producer.get());
    assert(blocking.pop(v) && v == 1);
    assert(!blocking.pop(v));
    BoundedFrameQueue<int> empty(1);
    auto consumer = std::async(std::launch::async, [&] {
        int item;
        return empty.pop(item);
    });
    empty.close();
    assert(consumer.wait_for(1s) == std::future_status::ready && !consumer.get());
}

void queue_stress() {
    for (int round = 0; round < 8; ++round) {
        BoundedFrameQueue<uint64_t> q(32);
        constexpr uint64_t count = 2000;
        uint64_t sum = 0, consumed = 0;
        std::thread consumer([&] {
            uint64_t v;
            while (q.pop(v)) {
                sum += v;
                ++consumed;
            }
        });
        std::vector<std::thread> producers;
        for (uint64_t p = 0; p < 4; ++p)
            producers.emplace_back([&, p] {
                for (uint64_t i = 1; i <= count; ++i)
                    assert(q.push(p * count + i));
            });
        for (auto &p : producers)
            p.join();
        q.close();
        consumer.join();
        const uint64_t total = 4 * count;
        assert(consumed == total && sum == total * (total + 1) / 2);
        const auto s = q.stats();
        assert(s.pushed == total && s.popped == total && s.dropped == 0 && s.high_water_mark <= q.capacity());
    }
}

FrameSample sample(uint32_t camera, uint64_t sequence, uint64_t ts, uint64_t arrival) {
    FrameSample s;
    s.camera_id = camera;
    s.sequence = sequence;
    s.timestamp_ns = ts;
    s.arrival_ns = arrival;
    s.clock_domain = FrameClockDomain::Monotonic;
    return s;
}

void bounded_pair_metrics() {
    FramePairerConfig config;
    config.max_wait_ns = 10;
    config.tolerance_ns = 5;
    config.max_stats_samples = 3;
    FramePairer pairer(config);
    for (uint64_t i = 0; i < 20; ++i) {
        const auto t = 100 + i * 100;
        assert(pairer.push(sample(0, i, t, t), t).empty());
        assert(pairer.push(sample(1, i, t + 1, t + 1), t + 1).size() == 1);
    }
    const auto stats = pairer.stats();
    assert(stats.deltas_ns.size() == 3 && stats.matched_pairs == 20 && stats.matched_frame_ratio() == 1.0);
    assert(stats.percentile(50) == 1.0);
    pairer.push(sample(0, 21, 3000, 3000), 3000);
    assert(pairer.push(sample(1, 21, 3001, 3020), 3020).empty());
    assert(pairer.stats().timeout_drops >= 1);

    config.policy = FramePairPolicy::HoldLast;
    FramePairer hold(config);
    hold.push(sample(0, 1, 100, 100), 100);
    assert(hold.push(sample(1, 1, 101, 101), 101).size() == 1);
    hold.push(sample(0, 2, 200, 200), 200);
    auto stale = hold.tick(211);
    assert(stale.size() == 1 && stale[0].is_stale() && !stale[0].clock_domain_verified);
    auto mismatched = sample(0, 3, 300, 300);
    mismatched.clock_domain = FrameClockDomain::Realtime;
    hold.push(mismatched, 300);
    assert(hold.tick(311).empty() && hold.stats().clock_domain_rejects > 0);
    hold.clear();
    assert(hold.pending(0) == 0 && hold.pending(1) == 0);
}
} // namespace
int main() {
    layout_and_lifetime();
    policies_and_close();
    queue_stress();
    bounded_pair_metrics();
    std::cout << "camera_queue_test: PASS (NV12/NV12M, leases, four policies, close, 64000 threaded "
                 "transfers, rolling pairing)\n";
}
