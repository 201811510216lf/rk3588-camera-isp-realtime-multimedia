#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace behavior {

struct FrameMetadata {
    uint32_t camera_id = 0;
    uint64_t sequence = 0;
    uint64_t source_timestamp_ns = 0;
    uint64_t monotonic_timestamp_ns = 0;
    std::string model_id;
    std::string model_version;
    bool demo_result = false;
};
struct BoundingBox {
    float x = 0, y = 0, width = 0, height = 0;
};
struct Detection {
    BoundingBox bbox;
    int class_id = -1;
    float confidence = 0;
};
struct DetectionResult {
    FrameMetadata frame;
    std::vector<Detection> detections;
};
struct TrackedDetection {
    Detection detection;
    uint64_t track_id = 0;
};
struct TrackerConfig {
    float min_iou = 0.3F;
    uint64_t max_gap_ns = 500000000;
    size_t max_tracks = 128;
};

// Camera-local IoU association only. track_id is not an animal identity.
class RatTracker {
  public:
    explicit RatTracker(TrackerConfig config = {});
    std::vector<TrackedDetection> update(const DetectionResult &result);
    size_t active_tracks() const { return tracks_.size(); }

  private:
    struct Track {
        uint32_t camera_id;
        uint64_t last_ns;
        TrackedDetection value;
    };
    TrackerConfig config_;
    uint64_t next_id_ = 1;
    std::vector<Track> tracks_;
    std::map<uint32_t, uint64_t> last_timestamp_;
    std::map<uint32_t, std::pair<std::string, std::string>> last_model_;
};

struct BehaviorSample {
    uint64_t track_id = 0;
    std::string behavior_type;
    float confidence = 0;
    // Supplied only by a model explicitly supporting individual identity.
    std::optional<std::string> rat_id;
};
struct BehaviorFrame {
    FrameMetadata frame;
    std::vector<BehaviorSample> samples;
};

// Explicit classifier label mapping; an ordinary rat detector leaves this empty.
BehaviorFrame classify_behavior(const FrameMetadata &frame, const std::vector<TrackedDetection> &tracks,
                                const std::map<int, std::string> &behavior_classes);

struct VideoSpan {
    std::string segment_id;
    uint64_t start_offset_ns = 0, end_offset_ns = 0;
    uint64_t first_sequence = 0, last_sequence = 0;
};
struct BehaviorEvent {
    std::string event_id;
    uint32_t camera_id = 0;
    uint64_t track_id = 0;
    std::optional<std::string> rat_id;
    std::string behavior_type;
    uint64_t start_timestamp_ns = 0, end_timestamp_ns = 0;
    uint64_t start_monotonic_ns = 0, end_monotonic_ns = 0;
    uint64_t first_sequence = 0, last_sequence = 0;
    double duration_ms = 0;
    float confidence = 0;
    std::string model_id, model_version;
    std::vector<VideoSpan> video_spans;
};
struct EventConfig {
    uint32_t min_observations = 3;
    uint64_t min_duration_ns = 200000000;
    uint64_t missing_grace_ns = 300000000;
    float confidence_threshold = 0.5F;
    size_t max_active_tracks = 256;
    std::string session_id;
};
struct EventStats {
    uint64_t rejected_frames = 0, rejected_samples = 0, finalized = 0;
};

// Single-owner state machine. Emit completed events after grace timeout, label
// change or shutdown. Use monotonic time for durations, source time for evidence.
class EventManager {
  public:
    explicit EventManager(EventConfig config);
    std::vector<BehaviorEvent> update(const BehaviorFrame &frame);
    std::vector<BehaviorEvent> tick(uint32_t camera_id, uint64_t monotonic_ns);
    std::vector<BehaviorEvent> finish();
    EventStats stats() const { return stats_; }
    size_t active_tracks() const { return states_.size(); }

  private:
    struct State {
        BehaviorEvent event;
        uint32_t observations = 0;
        double confidence_sum = 0;
    };
    using Key = std::pair<uint32_t, uint64_t>;
    void complete(const State &state, std::vector<BehaviorEvent> *events);
    EventConfig config_;
    uint64_t next_id_ = 1;
    std::map<Key, State> states_;
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> last_frame_;
    std::map<uint32_t, uint64_t> watermark_ns_;
    EventStats stats_;
};

bool add_video_span(BehaviorEvent *event, VideoSpan span);
std::string event_json(const BehaviorEvent &event);
std::string event_csv(const BehaviorEvent &event);
const char *event_csv_header();

} // namespace behavior
