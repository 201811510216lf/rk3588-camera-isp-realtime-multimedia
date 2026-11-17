#include "behavior/event_manager.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace behavior {
namespace {
bool confidence_valid(float v) { return std::isfinite(v) && v >= 0.0F && v <= 1.0F; }
bool box_valid(const BoundingBox &b) {
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) && std::isfinite(b.height) &&
           b.width > 0 && b.height > 0;
}
double iou(const BoundingBox &a, const BoundingBox &b) {
    const double w =
        std::max(0.0, std::min(static_cast<double>(a.x) + a.width, static_cast<double>(b.x) + b.width) -
                          std::max(a.x, b.x));
    const double h =
        std::max(0.0, std::min(static_cast<double>(a.y) + a.height, static_cast<double>(b.y) + b.height) -
                          std::max(a.y, b.y));
    const double overlap = w * h;
    return overlap /
           (static_cast<double>(a.width) * a.height + static_cast<double>(b.width) * b.height - overlap);
}
std::string json_quote(const std::string &s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\')
            out << '\\' << static_cast<char>(ch);
        else if (ch < 0x20)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(ch)
                << std::dec;
        else
            out << static_cast<char>(ch);
    }
    out << '"';
    return out.str();
}
std::string csv_quote(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        out += c;
        if (c == '"')
            out += '"';
    }
    return out + '"';
}
} // namespace

RatTracker::RatTracker(TrackerConfig c) : config_(c) {
    if (!confidence_valid(c.min_iou) || c.min_iou <= 0 || c.max_tracks == 0 || c.max_gap_ns == 0)
        throw std::invalid_argument("invalid tracker configuration");
}
std::vector<TrackedDetection> RatTracker::update(const DetectionResult &result) {
    const auto &frame = result.frame;
    if (frame.camera_id > 1 || frame.demo_result || frame.model_id.empty() || frame.model_version.empty() ||
        frame.model_id.size() > 256 || frame.model_version.size() > 256)
        return {};
    const auto prior = last_timestamp_.find(frame.camera_id);
    if (prior != last_timestamp_.end() && frame.monotonic_timestamp_ns <= prior->second)
        return {};
    last_timestamp_[frame.camera_id] = frame.monotonic_timestamp_ns;
    const auto model = std::make_pair(frame.model_id, frame.model_version);
    const bool changed_model = last_model_.count(frame.camera_id) && last_model_.at(frame.camera_id) != model;
    last_model_[frame.camera_id] = model;
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track &t) {
                                     return t.camera_id == frame.camera_id &&
                                            (changed_model ||
                                             frame.monotonic_timestamp_ns - t.last_ns > config_.max_gap_ns);
                                 }),
                  tracks_.end());
    std::set<uint64_t> used;
    std::vector<TrackedDetection> output;
    for (const auto &d : result.detections) {
        if (!box_valid(d.bbox) || !confidence_valid(d.confidence) || d.class_id < 0)
            continue;
        Track *best = nullptr;
        double best_iou = config_.min_iou;
        for (auto &track : tracks_) {
            if (track.camera_id != frame.camera_id || used.count(track.value.track_id))
                continue;
            // Behaviour labels may change while the same animal remains tracked.
            const auto overlap = iou(track.value.detection.bbox, d.bbox);
            if (overlap >= best_iou) {
                best_iou = overlap;
                best = &track;
            }
        }
        if (best) {
            best->value.detection = d;
            best->last_ns = frame.monotonic_timestamp_ns;
            output.push_back(best->value);
        } else if (tracks_.size() < config_.max_tracks) {
            Track track{frame.camera_id, frame.monotonic_timestamp_ns, {d, next_id_++}};
            tracks_.push_back(track);
            output.push_back(track.value);
        }
        if (!output.empty())
            used.insert(output.back().track_id);
    }
    return output;
}

BehaviorFrame classify_behavior(const FrameMetadata &frame, const std::vector<TrackedDetection> &tracks,
                                const std::map<int, std::string> &classes) {
    BehaviorFrame out;
    out.frame = frame;
    if (frame.demo_result)
        return out;
    for (const auto &track : tracks) {
        const auto label = classes.find(track.detection.class_id);
        if (label != classes.end() && !label->second.empty())
            out.samples.push_back({track.track_id, label->second, track.detection.confidence, std::nullopt});
    }
    return out;
}

EventManager::EventManager(EventConfig c) : config_(std::move(c)) {
    if (!config_.min_observations || config_.max_active_tracks == 0 ||
        !confidence_valid(config_.confidence_threshold) || config_.session_id.empty() ||
        config_.session_id.size() > 256)
        throw std::invalid_argument("event configuration requires a unique session id and valid thresholds");
}
void EventManager::complete(const State &state, std::vector<BehaviorEvent> *events) {
    const auto &e = state.event;
    if (state.observations < config_.min_observations ||
        e.end_monotonic_ns - e.start_monotonic_ns < config_.min_duration_ns)
        return;
    auto finished = e;
    finished.event_id = config_.session_id + "-" + std::to_string(next_id_++);
    finished.duration_ms = static_cast<double>(e.end_monotonic_ns - e.start_monotonic_ns) / 1000000.0;
    finished.confidence = static_cast<float>(state.confidence_sum / state.observations);
    events->push_back(std::move(finished));
    ++stats_.finalized;
}

std::vector<BehaviorEvent> EventManager::update(const BehaviorFrame &input) {
    const auto &f = input.frame;
    std::vector<BehaviorEvent> output;
    const auto last = last_frame_.find(f.camera_id);
    if (f.camera_id > 1 || f.demo_result || f.model_id.empty() || f.model_version.empty() ||
        f.model_id.size() > 256 || f.model_version.size() > 256 ||
        input.samples.size() > config_.max_active_tracks ||
        (watermark_ns_.count(f.camera_id) && f.monotonic_timestamp_ns < watermark_ns_.at(f.camera_id)) ||
        (last != last_frame_.end() &&
         (f.sequence <= last->second.first || f.monotonic_timestamp_ns <= last->second.second))) {
        ++stats_.rejected_frames;
        return output;
    }
    last_frame_[f.camera_id] = {f.sequence, f.monotonic_timestamp_ns};
    output = tick(f.camera_id, f.monotonic_timestamp_ns);
    std::set<uint64_t> seen;
    for (const auto &s : input.samples) {
        if (!s.track_id || s.behavior_type.empty() || s.behavior_type.size() > 128 ||
            (s.rat_id && s.rat_id->size() > 128) || !confidence_valid(s.confidence) ||
            s.confidence < config_.confidence_threshold || !seen.insert(s.track_id).second) {
            ++stats_.rejected_samples;
            continue;
        }
        const Key key{f.camera_id, s.track_id};
        auto state = states_.find(key);
        if (state != states_.end() && (state->second.event.behavior_type != s.behavior_type ||
                                       state->second.event.model_id != f.model_id ||
                                       state->second.event.model_version != f.model_version ||
                                       state->second.event.rat_id != s.rat_id)) {
            complete(state->second, &output);
            states_.erase(state);
            state = states_.end();
        }
        if (state == states_.end()) {
            if (states_.size() >= config_.max_active_tracks) {
                ++stats_.rejected_samples;
                continue;
            }
            State initial;
            auto &e = initial.event;
            e.camera_id = f.camera_id;
            e.track_id = s.track_id;
            e.rat_id = s.rat_id;
            e.behavior_type = s.behavior_type;
            e.start_timestamp_ns = f.source_timestamp_ns;
            e.start_monotonic_ns = f.monotonic_timestamp_ns;
            e.first_sequence = f.sequence;
            e.model_id = f.model_id;
            e.model_version = f.model_version;
            state = states_.emplace(key, std::move(initial)).first;
        }
        auto &st = state->second;
        ++st.observations;
        st.confidence_sum += s.confidence;
        st.event.end_timestamp_ns = f.source_timestamp_ns;
        st.event.end_monotonic_ns = f.monotonic_timestamp_ns;
        st.event.last_sequence = f.sequence;
    }
    return output;
}

std::vector<BehaviorEvent> EventManager::tick(uint32_t camera_id, uint64_t now_ns) {
    std::vector<BehaviorEvent> events;
    if (camera_id > 1)
        return events;
    if (watermark_ns_.count(camera_id) && now_ns < watermark_ns_.at(camera_id))
        return events;
    watermark_ns_[camera_id] = now_ns;
    for (auto it = states_.begin(); it != states_.end();) {
        const auto last_ns = it->second.event.end_monotonic_ns;
        if (it->first.first == camera_id && now_ns >= last_ns &&
            now_ns - last_ns > config_.missing_grace_ns) {
            complete(it->second, &events);
            it = states_.erase(it);
        } else
            ++it;
    }
    return events;
}
std::vector<BehaviorEvent> EventManager::finish() {
    std::vector<BehaviorEvent> events;
    for (const auto &entry : states_)
        complete(entry.second, &events);
    states_.clear();
    return events;
}

bool add_video_span(BehaviorEvent *event, VideoSpan span) {
    if (!event || span.segment_id.empty() || span.start_offset_ns > span.end_offset_ns ||
        span.first_sequence > span.last_sequence || span.first_sequence < event->first_sequence ||
        span.last_sequence > event->last_sequence)
        return false;
    for (const auto &prior : event->video_spans)
        if (prior.segment_id == span.segment_id ||
            !(span.last_sequence < prior.first_sequence || span.first_sequence > prior.last_sequence))
            return false;
    event->video_spans.push_back(std::move(span));
    std::sort(event->video_spans.begin(), event->video_spans.end(),
              [](const VideoSpan &a, const VideoSpan &b) { return a.first_sequence < b.first_sequence; });
    return true;
}

std::string event_json(const BehaviorEvent &e) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << "{\"event_id\":" << json_quote(e.event_id) << ",\"camera_id\":" << e.camera_id
        << ",\"track_id\":" << e.track_id;
    if (e.rat_id)
        out << ",\"rat_id\":" << json_quote(*e.rat_id);
    out << ",\"behavior_type\":" << json_quote(e.behavior_type)
        << ",\"start_timestamp_ns\":" << e.start_timestamp_ns
        << ",\"end_timestamp_ns\":" << e.end_timestamp_ns
        << ",\"start_monotonic_ns\":" << e.start_monotonic_ns
        << ",\"end_monotonic_ns\":" << e.end_monotonic_ns << ",\"duration_ms\":" << e.duration_ms
        << ",\"confidence\":" << e.confidence << ",\"first_sequence\":" << e.first_sequence
        << ",\"last_sequence\":" << e.last_sequence << ",\"model_id\":" << json_quote(e.model_id)
        << ",\"model_version\":" << json_quote(e.model_version) << ",\"video_spans\":[";
    bool first = true;
    for (const auto &span : e.video_spans) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"segment_id\":" << json_quote(span.segment_id)
            << ",\"start_offset_ns\":" << span.start_offset_ns << ",\"end_offset_ns\":" << span.end_offset_ns
            << ",\"first_sequence\":" << span.first_sequence << ",\"last_sequence\":" << span.last_sequence
            << '}';
    }
    out << "]}";
    return out.str();
}
const char *event_csv_header() {
    return "event_id,camera_id,track_id,rat_id,behavior_type,start_timestamp_ns,end_timestamp_ns,duration_ms,"
           "confidence,first_sequence,last_sequence,model_id,model_version,video_spans_json";
}
std::string event_csv(const BehaviorEvent &e) {
    auto json = event_json(e);
    const auto at = json.find("\"video_spans\":");
    const auto spans = json.substr(at + 14, json.size() - at - 15);
    std::ostringstream out;
    out << csv_quote(e.event_id) << ',' << e.camera_id << ',' << e.track_id << ','
        << csv_quote(e.rat_id.value_or("")) << ',' << csv_quote(e.behavior_type) << ','
        << e.start_timestamp_ns << ',' << e.end_timestamp_ns << ',' << e.duration_ms << ',' << e.confidence
        << ',' << e.first_sequence << ',' << e.last_sequence << ',' << csv_quote(e.model_id) << ','
        << csv_quote(e.model_version) << ',' << csv_quote(spans);
    return out.str();
}

} // namespace behavior
