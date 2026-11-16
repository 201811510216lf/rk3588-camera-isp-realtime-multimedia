#include "frame_pairer.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
FrameSample sample(uint32_t camera, uint64_t seq, uint64_t ts, uint64_t arrival = 1,
                   FrameClockDomain domain = FrameClockDomain::Monotonic) {
    FrameSample s;
    s.camera_id = camera;
    s.sequence = seq;
    s.timestamp_ns = ts;
    s.normalized_timestamp_ns = ts;
    s.arrival_ns = arrival;
    s.clock_domain = domain;
    return s;
}
}

int main() {
    FramePairerConfig cfg;
    cfg.tolerance_ns = 5;
    cfg.max_wait_ns = 10;
    cfg.max_queue_depth = 2;
    FramePairer pairer(cfg);

    auto no_pair = pairer.push(sample(0, 1, 100, 100), 100);
    assert(no_pair.empty());
    auto pair = pairer.push(sample(1, 1, 103, 101), 101);
    assert(pair.size() == 1 && pair[0].pair_id == 1 && pair[0].delta_ns == 3);
    assert(pair[0].clock_domain_verified);

    // A timestamp beyond tolerance drops only the older side.  The newer
    // camera-1 sample remains queued and can match the next camera-0 frame.
    pairer.push(sample(0, 2, 200, 200), 200);
    pairer.push(sample(1, 2, 220, 201), 201);
    assert(pairer.stats().pair_drops >= 1);
    pair = pairer.push(sample(0, 3, 222, 202), 202);
    assert(pair.size() == 1 && pair[0].delta_ns == 2);
    auto stats = pairer.stats();
    assert(stats.matched_pairs >= 2);
    assert(stats.p95_ms() >= 0.0);

    // Out-of-order and clock-domain mismatches are rejected, never compared.
    pairer.push(sample(0, 4, 190, 204), 204);
    const auto before = pairer.stats().out_of_order_drops;
    assert(before >= 1);
    pairer.push(sample(0, 5, 300, 300, FrameClockDomain::Monotonic), 300);
    pairer.push(sample(1, 5, 300, 301, FrameClockDomain::Realtime), 301);
    assert(pairer.stats().clock_domain_rejects >= 1);

    // A missing side expires after max_wait and releases the pending sample.
    pairer.push(sample(0, 6, 400, 400), 400);
    pairer.tick(411);
    assert(pairer.pending(0) == 0);
    assert(pairer.stats().timeout_drops >= 1);

    // Queue depth is bounded and drops the oldest source frame.
    pairer.push(sample(1, 6, 500, 500), 500);
    pairer.push(sample(1, 7, 501, 501), 501);
    pairer.push(sample(1, 8, 502, 502), 502);
    assert(pairer.pending(1) <= 2);
    assert(pairer.stats().camera1_source_drops >= 1);

    // When one queue already contains multiple candidates, choose the
    // smallest timestamp delta rather than blindly pairing both fronts.
    FramePairer closest(FramePairerConfig{5, 100, 4, FramePairPolicy::DropOlder});
    closest.push(sample(0, 20, 1000, 1000), 1000);
    closest.push(sample(0, 21, 1001, 1001), 1001);
    auto closest_pair = closest.push(sample(1, 20, 1002, 1002), 1002);
    assert(closest_pair.size() == 1);
    assert(closest_pair[0].cam0.sequence == 21);
    assert(closest_pair[0].delta_ns == 1);

    FramePairer unknown_domain;
    unknown_domain.push(sample(0, 1, 10, 10, FrameClockDomain::Unknown), 10);
    unknown_domain.push(sample(1, 1, 10, 10, FrameClockDomain::Unknown), 10);
    assert(unknown_domain.stats().clock_domain_rejects >= 1);
    assert(unknown_domain.stats().matched_pairs == 0);

    std::cout << "frame_pairer_test: PASS\n";
    return 0;
}

