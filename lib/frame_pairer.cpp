#include "frame_pairer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>

namespace {
uint64_t abs_delta(uint64_t a, uint64_t b) {
    return a >= b ? a - b : b - a;
}
}

double FramePairerStats::percentile(double p) const {
    if (deltas_ns.empty()) return 0.0;
    if (p <= 0.0) return static_cast<double>(*std::min_element(deltas_ns.begin(), deltas_ns.end()));
    if (p >= 100.0) return static_cast<double>(*std::max_element(deltas_ns.begin(), deltas_ns.end()));
    std::vector<uint64_t> sorted = deltas_ns;
    std::sort(sorted.begin(), sorted.end());
    const double rank = (p / 100.0) * static_cast<double>(sorted.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(rank));
    const size_t upper = static_cast<size_t>(std::ceil(rank));
    if (lower == upper) return static_cast<double>(sorted[lower]);
    const double weight = rank - static_cast<double>(lower);
    return static_cast<double>(sorted[lower]) * (1.0 - weight) +
           static_cast<double>(sorted[upper]) * weight;
}

FramePairer::FramePairer(FramePairerConfig config) : config_(config) {
    if (config_.max_queue_depth == 0) config_.max_queue_depth = 1;
}

void FramePairer::configure(FramePairerConfig config) {
    if (config.max_queue_depth == 0) config.max_queue_depth = 1;
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[0].clear();
    queues_[1].clear();
    last_[0].reset();
    last_[1].reset();
    last_timestamp_[0] = last_timestamp_[1] = 0;
    last_sequence_[0] = last_sequence_[1] = 0;
    have_last_timestamp_[0] = have_last_timestamp_[1] = false;
    next_pair_id_ = 1;
    stats_ = {};
    config_ = config;
}

uint64_t FramePairer::monotonic_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

bool FramePairer::timestamp_less(const FrameSample &a, const FrameSample &b) {
    const uint64_t ta = a.normalized_timestamp_ns ? a.normalized_timestamp_ns : a.timestamp_ns;
    const uint64_t tb = b.normalized_timestamp_ns ? b.normalized_timestamp_ns : b.timestamp_ns;
    return ta < tb || (ta == tb && a.sequence < b.sequence);
}

bool FramePairer::compatible_clock_domains(const FrameSample &a, const FrameSample &b) const {
    // Unknown is never silently compared.  A precise S1/S2 claim requires
    // both timestamp domains to be known and equal; otherwise the older
    // sample is rejected and the caller can inspect clock_domain_rejects.
    return a.clock_domain != FrameClockDomain::Unknown &&
           a.clock_domain == b.clock_domain;
}

void FramePairer::drop_front_locked(uint32_t camera_id, bool timeout) {
    if (camera_id > 1 || queues_[camera_id].empty()) return;
    queues_[camera_id].pop_front();
    ++stats_.pair_drops;
    if (timeout) ++stats_.timeout_drops;
    if (camera_id == 0) ++stats_.camera0_source_drops;
    else ++stats_.camera1_source_drops;
}

void FramePairer::record_match_locked(const FramePair &pair) {
    if (pair.status == FramePairStatus::StaleHoldLast) {
        ++stats_.stale_pairs;
        return;
    }
    ++stats_.matched_pairs;
    stats_.last_delta_ns = pair.delta_ns;
    stats_.max_delta_ns = std::max(stats_.max_delta_ns, pair.delta_ns);
    stats_.deltas_ns.push_back(pair.delta_ns);
}

std::vector<FramePair> FramePairer::drain_ready_locked(uint64_t now_ns) {
    (void)now_ns;
    std::vector<FramePair> result;
    while (!queues_[0].empty() && !queues_[1].empty()) {
        // Search all currently buffered candidates. A front-only comparison
        // can choose (100,102) while (101,102) is already queued, which is a
        // real pairing error when capture jitter fills one queue first.
        size_t best_i = 0;
        size_t best_j = 0;
        uint64_t best_delta = UINT64_MAX;
        bool found = false;
        for (size_t i = 0; i < queues_[0].size(); ++i) {
            for (size_t j = 0; j < queues_[1].size(); ++j) {
                const FrameSample &candidate0 = queues_[0][i];
                const FrameSample &candidate1 = queues_[1][j];
                if (!compatible_clock_domains(candidate0, candidate1)) continue;
                const uint64_t t0 = candidate0.normalized_timestamp_ns ? candidate0.normalized_timestamp_ns : candidate0.timestamp_ns;
                const uint64_t t1 = candidate1.normalized_timestamp_ns ? candidate1.normalized_timestamp_ns : candidate1.timestamp_ns;
                const uint64_t delta = abs_delta(t0, t1);
                if (delta <= config_.tolerance_ns &&
                    (!found || delta < best_delta ||
                     (delta == best_delta && (i < best_i || (i == best_i && j < best_j))))) {
                    found = true;
                    best_i = i;
                    best_j = j;
                    best_delta = delta;
                }
            }
        }
        if (found) {
            // Older samples cannot be matched after a later sample has been
            // selected; release them before consuming the selected pair.
            for (size_t i = 0; i < best_i; ++i) drop_front_locked(0, false);
            for (size_t j = 0; j < best_j; ++j) drop_front_locked(1, false);
            FrameSample a = std::move(queues_[0].front());
            FrameSample b = std::move(queues_[1].front());
            queues_[0].pop_front();
            queues_[1].pop_front();
            FramePair pair;
            pair.pair_id = next_pair_id_++;
            pair.cam0 = std::move(a);
            pair.cam1 = std::move(b);
            pair.delta_ns = best_delta;
            pair.status = FramePairStatus::Matched;
            pair.clock_domain_verified = pair.cam0.clock_domain != FrameClockDomain::Unknown &&
                                         pair.cam0.clock_domain == pair.cam1.clock_domain;
            // Retain the last frame only for explicit hold-last mode;
            // default drop-older releases leases as soon as the pair is consumed.
            if (config_.policy == FramePairPolicy::HoldLast) {
                last_[0] = pair.cam0;
                last_[1] = pair.cam1;
            }
            record_match_locked(pair);
            result.push_back(std::move(pair));
            continue;
        }
        FrameSample &a = queues_[0].front();
        FrameSample &b = queues_[1].front();
        if (!compatible_clock_domains(a, b)) {
            ++stats_.clock_domain_rejects;
            // The queues are independently monotonic. Discard the older
            // sample; keeping it would make both sides wait forever.
            if (timestamp_less(a, b)) drop_front_locked(0, false);
            else drop_front_locked(1, false);
            continue;
        }
        // Both samples are present and their timestamps are monotonic.  No
        // future sample can improve the older one, so drop the older side.
        if (timestamp_less(a, b)) drop_front_locked(0, false);
        else drop_front_locked(1, false);
    }
    return result;
}

std::vector<FramePair> FramePairer::expire_locked(uint64_t now_ns) {
    std::vector<FramePair> result;
    for (uint32_t camera_id = 0; camera_id < 2; ++camera_id) {
        while (!queues_[camera_id].empty()) {
            FrameSample &front = queues_[camera_id].front();
            const uint64_t arrival = front.arrival_ns;
            if (arrival == 0 || now_ns < arrival || now_ns - arrival < config_.max_wait_ns) break;
            if (config_.policy == FramePairPolicy::HoldLast && last_[1 - camera_id].has_value()) {
                FramePair pair;
                pair.pair_id = next_pair_id_++;
                if (camera_id == 0) {
                    pair.cam0 = std::move(front);
                    pair.cam1 = *last_[1];
                } else {
                    pair.cam0 = *last_[0];
                    pair.cam1 = std::move(front);
                }
                const uint64_t ta = pair.cam0.normalized_timestamp_ns ? pair.cam0.normalized_timestamp_ns
                                                                        : pair.cam0.timestamp_ns;
                const uint64_t tb = pair.cam1.normalized_timestamp_ns ? pair.cam1.normalized_timestamp_ns
                                                                        : pair.cam1.timestamp_ns;
                pair.delta_ns = abs_delta(ta, tb);
                pair.status = FramePairStatus::StaleHoldLast;
                pair.clock_domain_verified = false;
                queues_[camera_id].pop_front();
                record_match_locked(pair);
                result.push_back(std::move(pair));
            } else {
                drop_front_locked(camera_id, true);
            }
        }
    }
    return result;
}

std::vector<FramePair> FramePairer::push(FrameSample sample, uint64_t now_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sample.camera_id > 1) return {};
    if (now_ns == 0) now_ns = monotonic_now_ns();
    if (sample.arrival_ns == 0) sample.arrival_ns = now_ns;
    const uint32_t id = sample.camera_id;
    const uint64_t ts = sample.normalized_timestamp_ns ? sample.normalized_timestamp_ns : sample.timestamp_ns;
    if (have_last_timestamp_[id] &&
        (ts < last_timestamp_[id] ||
         (ts == last_timestamp_[id] && sample.sequence < last_sequence_[id]))) {
        ++stats_.out_of_order_drops;
        if (id == 0) ++stats_.camera0_source_drops;
        else ++stats_.camera1_source_drops;
        return {};
    }
    have_last_timestamp_[id] = true;
    last_timestamp_[id] = ts;
    last_sequence_[id] = sample.sequence;
    queues_[id].push_back(std::move(sample));
    while (queues_[id].size() > config_.max_queue_depth) drop_front_locked(id, false);
    stats_.max_depth_camera0 = std::max<uint64_t>(stats_.max_depth_camera0, queues_[0].size());
    stats_.max_depth_camera1 = std::max<uint64_t>(stats_.max_depth_camera1, queues_[1].size());
    auto ready = drain_ready_locked(now_ns);
    auto expired = expire_locked(now_ns);
    ready.insert(ready.end(), std::make_move_iterator(expired.begin()), std::make_move_iterator(expired.end()));
    return ready;
}

std::vector<FramePair> FramePairer::tick(uint64_t now_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ready = drain_ready_locked(now_ns);
    auto expired = expire_locked(now_ns);
    ready.insert(ready.end(), std::make_move_iterator(expired.begin()), std::make_move_iterator(expired.end()));
    return ready;
}

void FramePairer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[0].clear();
    queues_[1].clear();
    last_[0].reset();
    last_[1].reset();
    have_last_timestamp_[0] = have_last_timestamp_[1] = false;
    last_sequence_[0] = last_sequence_[1] = 0;
}

FramePairerStats FramePairer::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

size_t FramePairer::pending(uint32_t camera_id) const {
    if (camera_id > 1) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    return queues_[camera_id].size();
}

