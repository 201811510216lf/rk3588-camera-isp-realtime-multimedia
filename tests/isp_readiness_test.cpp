#include "isp/isp_readiness.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>

int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Hypothetical live status fixture tests policy only; no backend/hardware operation occurs.
    isp::AiqCameraConfig camera;
    camera.sensor_entity = "test sensor";
    camera.video_node = "/dev/video-test";
    isp::IspCameraEvidence evidence;
    evidence.mapping_valid = evidence.output_valid = evidence.live_evidence = true;
    evidence.output.video_node = camera.video_node;
    evidence.isp_generation = "RKISP30_REPORTED";
    isp::IspStatus status;
    status.ownership = camera.ownership;
    status.state = isp::IspRuntimeState::Running;
    status.backend.real_hardware_backend = true;
    status.backend.evidence = isp::EvidenceSource::LiveDevice;
    status.hardware_confirmed = true;
    status.sensor.entity = camera.sensor_entity;
    status.sensor.confirmed = true;
    status.sensor.evidence = isp::EvidenceSource::LiveDevice;
    status.iq.file_readable = true;
    status.iq.sensor_match = status.iq.sdk_compatible = status.iq.loaded = isp::Confirmation::Confirmed;
    assert(isp::evaluate_readiness(camera, evidence, status, false).ready);
    auto unverified = evidence;
    unverified.live_evidence = false;
    assert(!isp::evaluate_readiness(camera, unverified, status, false).result.ok());
    auto diagnostic = isp::evaluate_readiness(camera, unverified, status, true);
    assert(diagnostic.result.ok() && diagnostic.diagnostic_unverified && !diagnostic.ready);
    status.backend.evidence = isp::EvidenceSource::TestBackend;
    assert(!isp::evaluate_readiness(camera, evidence, status, false).ready);
    status.iq.loaded = isp::Confirmation::NotMeasured;
    assert(!isp::evaluate_readiness(camera, evidence, status, false).ready);
    status.state = isp::IspRuntimeState::OwnershipConflict;
    assert(!isp::evaluate_readiness(camera, evidence, status, true).result.ok());
    status.state = isp::IspRuntimeState::Discovered;
    status.ownership = isp::IspAiqOwnershipMode::InProcess;
    assert(!isp::evaluate_readiness(camera, evidence, status, true).result.ok());
    status.ownership = camera.ownership;
    status.iq.sensor_match = isp::Confirmation::Mismatch;
    assert(!isp::evaluate_readiness(camera, evidence, status, true).result.ok());
    status.iq.sensor_match = isp::Confirmation::NotMeasured;
    evidence.output.video_node = "/dev/other";
    assert(!isp::evaluate_readiness(camera, evidence, status, true).result.ok());

    isp::IspConfig config;
    const auto fixtures = std::filesystem::u8path(ISP_TEST_FIXTURE_DIR);
    assert(isp::load_isp_config((fixtures / "isp_offline.yaml").string(), &config).ok());
    isp::CaptureBinding capture;
    capture.nodes = {"/dev/video42", "/dev/video47"};
    capture.width = 640;
    capture.height = 480;
    capture.fps = 30;
    assert(isp::validate_capture_binding(config, capture).ok());
    capture.nodes[1] = "/dev/different";
    assert(!isp::validate_capture_binding(config, capture).ok());
    capture.nodes[1] = "/dev/video47";
    capture.fps = 25;
    assert(!isp::validate_capture_binding(config, capture).ok());
    isp::IspPipelineCoordinator strict(config);
    assert(!strict.before_stream().ok());
    assert(!strict.before_stream().ok());
    assert(strict.shutdown().ok());
    config.strict_isp_ready = false;
    config.allow_unverified_isp = true;
    isp::IspPipelineCoordinator offline(config);
    assert(offline.before_stream().ok());
    assert(offline.after_stream().ok());
    assert(offline.status(0).state != isp::IspRuntimeState::Ready);
    assert(!offline.status(0).hardware_confirmed);
    assert(offline.before_stream_stop().ok());
    assert(offline.after_stream_stop().ok());
    return 0;
}
