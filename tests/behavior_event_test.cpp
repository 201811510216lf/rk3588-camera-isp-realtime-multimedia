#include "behavior/event_manager.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
behavior::BehaviorFrame sample(uint32_t camera, uint64_t sequence, uint64_t ms,
                               const char *type = "grooming") {
    behavior::BehaviorFrame f;
    f.frame = {camera, sequence, ms * 1000000 + 100000, ms * 1000000, "test-model", "test-only", false};
    if (type)
        f.samples.push_back({1, type, 0.9F, std::nullopt});
    return f;
}
behavior::EventConfig config() {
    behavior::EventConfig c;
    c.session_id = "unit-test-session";
    c.min_duration_ns = 200000000;
    c.missing_grace_ns = 300000000;
    return c;
}
} // namespace
int main() {
    behavior::EventManager manager(config());
    assert(manager.update(sample(0, 1, 0)).empty());
    assert(manager.update(sample(0, 2, 100)).empty());
    assert(manager.update(sample(0, 3, 200)).empty());
    assert(manager.update(sample(0, 4, 350, nullptr)).empty());
    // A brief miss merges; the event closes at its last supported timestamp.
    assert(manager.update(sample(0, 5, 450)).empty());
    assert(manager.tick(0, 750000000).empty());
    auto ended = manager.tick(0, 751000000);
    assert(ended.size() == 1);
    auto e = ended[0];
    assert(e.duration_ms == 450 && e.first_sequence == 1 && e.last_sequence == 5 && !e.rat_id);
    assert(e.start_timestamp_ns == 100000 && e.start_monotonic_ns == 0);
    assert(behavior::add_video_span(&e, {"segment-a", 0, 200000000, 1, 3}));
    assert(behavior::add_video_span(&e, {"segment-b", 0, 100000000, 4, 5}));
    assert(!behavior::add_video_span(&e, {"overlap", 0, 100, 2, 4}));
    assert(behavior::event_json(e).find("\"video_spans\":[{") != std::string::npos);
    assert(behavior::event_json(e).find("\"rat_id\"") == std::string::npos);
    assert(behavior::event_csv(e).find("segment-a") != std::string::npos);
    // Cameras and model versions remain independent; label changes close old events.
    for (uint64_t i = 0; i < 3; ++i) {
        manager.update(sample(0, 10 + i, 1000 + 100 * i));
        manager.update(sample(1, 1 + i, 1000 + 100 * i));
    }
    auto switched = manager.update(sample(0, 13, 1300, "rearing"));
    assert(switched.size() == 1 && switched[0].behavior_type == "grooming");
    auto rest = manager.finish();
    assert(rest.size() == 1 && rest[0].camera_id == 1);
    auto demo = sample(0, 14, 1400);
    demo.frame.demo_result = true;
    assert(manager.update(demo).empty());
    assert(manager.update(sample(0, 1, 1)).empty());
    assert(manager.stats().rejected_frames == 2);
    auto low = sample(0, 15, 1500);
    low.samples[0].confidence = std::numeric_limits<float>::quiet_NaN();
    manager.update(low);
    assert(manager.stats().rejected_samples == 1);
    assert(manager.finish().empty());
    behavior::RatTracker tracker;
    behavior::DetectionResult detected;
    detected.frame = {0, 1, 1, 1, "test", "v1", false};
    detected.detections = {{{0, 0, 10, 10}, 0, 0.9F}};
    const auto t1 = tracker.update(detected);
    assert(t1.size() == 1);
    detected.frame.monotonic_timestamp_ns = 100000001;
    detected.frame.sequence = 2;
    detected.detections[0].class_id = 1;
    const auto t2 = tracker.update(detected);
    assert(t2.size() == 1 && t1[0].track_id == t2[0].track_id);
    assert(behavior::classify_behavior(detected.frame, t2, {}).samples.empty());
    const auto classified = behavior::classify_behavior(detected.frame, t2, {{1, "rearing"}});
    assert(classified.samples.size() == 1 && !classified.samples[0].rat_id);
    detected.frame.camera_id = 1;
    const auto other = tracker.update(detected);
    assert(other.size() == 1 && other[0].track_id != t1[0].track_id);
    detected.frame.camera_id = 0;
    detected.frame.monotonic_timestamp_ns = 1000000001;
    const auto expired = tracker.update(detected);
    assert(expired[0].track_id != t1[0].track_id);
    // Long deterministic event sequence: bounded active state, unique session IDs.
    behavior::EventManager long_run(config());
    uint64_t events = 0;
    for (uint64_t i = 0; i < 20000; ++i) {
        events += long_run.update(sample(0, i + 1, i * 100, i % 20 < 10 ? "grooming" : "rearing")).size();
        assert(long_run.active_tracks() <= 1);
    }
    events += long_run.finish().size();
    assert(events == 2000);
    behavior::EventManager late(config());
    late.tick(0, 2000000000);
    late.update(sample(0, 1, 1000));
    assert(late.stats().rejected_frames == 1);
    auto invalid_camera = sample(2, 1, 3000);
    late.update(invalid_camera);
    assert(late.stats().rejected_frames == 2);
    detected.frame.monotonic_timestamp_ns += 1;
    detected.frame.model_version = "v2";
    const auto changed_model = tracker.update(detected);
    assert(changed_model[0].track_id != expired[0].track_id);
    return 0;
}
