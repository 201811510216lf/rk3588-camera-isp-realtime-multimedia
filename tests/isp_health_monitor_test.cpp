#include "isp/isp_health_monitor.h"

#include <cassert>
#include <cmath>
#include <stdexcept>

int main() {
    isp::IspHealthMonitor monitor(100, 200, 2);
    assert(monitor.snapshot(0, 1).state == isp::IspRuntimeState::Unknown);
    isp::IspStatus service;
    service.camera_id = 0;
    service.state = isp::IspRuntimeState::Ready;
    service.backend.evidence = isp::EvidenceSource::TestBackend;
    service.owner.source = isp::EvidenceSource::TestBackend;
    assert(monitor.observe_service(service, 100));
    assert(monitor.observe_capture(0, {10, 1000, 100, true, isp::EvidenceSource::TestBackend}));
    assert(monitor.observe_capture(0, {12, 1100, 150, true, isp::EvidenceSource::TestBackend}));
    auto running = monitor.snapshot(0, 150);
    assert(running.state == isp::IspRuntimeState::Running && running.sequence_gaps == 1);
    assert(running.source_fps && std::abs(*running.source_fps - 10'000'000.0) < .001);
    assert(running.arrival_fps && *running.arrival_fps == 20'000'000.0);
    assert(!running.status.hardware_confirmed &&
           running.capture_evidence == isp::EvidenceSource::TestBackend);
    assert(monitor.snapshot(0, 251).capture_stalled);
    assert(monitor.snapshot(0, 251).state == isp::IspRuntimeState::Degraded);
    assert(monitor.snapshot(0, 301).service_stale);
    assert(monitor.snapshot(1, 150).captured_frames == 0);
    assert(monitor.capture_started(1, 100));
    assert(!monitor.snapshot(1, 200).capture_stalled);
    assert(monitor.snapshot(1, 201).capture_stalled);
    assert(monitor.snapshot(1, 201).state == isp::IspRuntimeState::Degraded);
    assert(monitor.capture_stopped(1));
    assert(!monitor.snapshot(1, 500).capture_stalled);
    assert(monitor.capture_started(1, 600));
    assert(!monitor.capture_started(1, 700));
    assert(!monitor.snapshot(1, 700).capture_stalled);
    assert(monitor.record_error(1) && monitor.snapshot(1, 700).errors == 1);
    assert(!monitor.observe_capture(0, {13, 1200, 150, true, isp::EvidenceSource::TestBackend}));
    assert(monitor.observe_capture(0, {12, 900, 180, true, isp::EvidenceSource::TestBackend}));
    auto invalid = monitor.snapshot(0, 180);
    assert(invalid.sequence_duplicates == 1 && invalid.timestamp_errors == 2 && !invalid.source_fps);
    assert(monitor.observe_capture(0, {1, 2000, 200, true, isp::EvidenceSource::TestBackend}));
    assert(monitor.snapshot(0, 200).sequence_backwards == 1);
    assert(monitor.observe_capture(0, {2, 2100, 230, false, isp::EvidenceSource::LiveDevice}));
    assert(!monitor.snapshot(0, 230).source_fps && !monitor.snapshot(0, 230).arrival_fps);
    service.state = isp::IspRuntimeState::OwnershipConflict;
    assert(monitor.observe_service(service, 230));
    assert(monitor.snapshot(0, 230).state == isp::IspRuntimeState::OwnershipConflict);
    assert(!monitor.observe_service(service, 229));
    assert(!monitor.observe_capture(2, {1, 1, 1, true, isp::EvidenceSource::LiveDevice}));
    assert(!monitor.record_error(2));
    assert(!monitor.observe_capture(1, {1, 1, 1, true, isp::EvidenceSource::NotMeasured}));
    const auto json = isp::health_json(monitor.snapshot(0, 230));
    assert(json.find("OWNERSHIP_CONFLICT") != std::string::npos);
    assert(json.find("\"hardware_confirmed\":false") != std::string::npos);
    bool rejected = false;
    try {
        isp::IspHealthMonitor bad(1, 1, 0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected);
    return 0;
}
