#include "isp/isp_readiness.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace {
class ThrowingInspector final : public isp::IMediaGraphInspector {
  public:
    bool inspect(const std::string &, const std::string &, isp::IspDeviceInfo *, std::string *) override {
        throw std::runtime_error("injected discovery failure");
    }
    bool query_output(const std::string &, const std::string &, isp::IspOutputNode *,
                      std::string *) override {
        throw std::runtime_error("unexpected query");
    }
};
class ThrowingProvider final : public isp::IspReadinessProvider {
  public:
    isp::IspControlResult before_stream() override {
        throw std::runtime_error("injected provider start failure");
    }
    isp::IspControlResult after_stream() override { return {}; }
    isp::IspControlResult before_stream_stop() override {
        throw std::runtime_error("injected provider stop failure");
    }
    isp::IspControlResult after_stream_stop() override { return {}; }
    isp::IspControlResult shutdown() override { return {}; }
    bool wrong_camera = false;
    isp::IspStatus status(isp::IspCameraId id) const override {
        if (wrong_camera) {
            isp::IspStatus snapshot;
            snapshot.camera_id = id + 1;
            snapshot.state = isp::IspRuntimeState::Running;
            snapshot.hardware_confirmed = true;
            return snapshot;
        }
        throw std::runtime_error("injected provider status failure");
    }
};
} // namespace

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
    evidence.graph.evidence = evidence.output.evidence = isp::EvidenceSource::LiveDevice;
    evidence.sensor.identity.entity = camera.sensor_entity;
    evidence.sensor.identity.confirmed = true;
    evidence.sensor.identity.evidence = isp::EvidenceSource::LiveDevice;
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
    assert(isp::evaluate_runtime_status(0, status, false).ready);
    auto stale_runtime = status;
    stale_runtime.iq.loaded = isp::Confirmation::NotMeasured;
    assert(!isp::evaluate_runtime_status(0, stale_runtime, false).result.ok());
    auto diagnostic_runtime = isp::evaluate_runtime_status(0, stale_runtime, true);
    assert(diagnostic_runtime.result.ok() && diagnostic_runtime.diagnostic_unverified &&
           !diagnostic_runtime.ready);
    stale_runtime.state = isp::IspRuntimeState::Error;
    assert(isp::evaluate_runtime_status(0, stale_runtime, true).result.status ==
           isp::ControlStatus::BackendError);
    stale_runtime = status;
    stale_runtime.camera_id = 1;
    assert(!isp::evaluate_runtime_status(0, stale_runtime, true).result.ok());
    stale_runtime = status;
    stale_runtime.state = static_cast<isp::IspRuntimeState>(999);
    assert(!isp::evaluate_runtime_status(0, stale_runtime, true).result.ok());
    auto contradictory = evidence;
    contradictory.graph.evidence = isp::EvidenceSource::OfflineFile;
    assert(!isp::evaluate_readiness(camera, contradictory, status, false).ready);
    contradictory = evidence;
    contradictory.sensor.identity.entity = "another sensor";
    assert(!isp::evaluate_readiness(camera, contradictory, status, true).result.ok());
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
    assert(offline.after_stream().status == isp::ControlStatus::InvalidState);
    assert(offline.status(0).state != isp::IspRuntimeState::Ready);
    assert(!offline.status(0).hardware_confirmed);
    assert(offline.after_stream_stop().status == isp::ControlStatus::InvalidState);
    assert(offline.before_stream_stop().ok());
    assert(offline.before_stream_stop().ok());
    assert(offline.after_stream().status == isp::ControlStatus::InvalidState);
    assert(offline.after_stream_stop().ok());
    assert(offline.after_stream_stop().ok());
    assert(offline.after_stream().status == isp::ControlStatus::InvalidState);

    isp::IspPipelineCoordinator broken_discovery(config, std::make_unique<ThrowingInspector>());
    const auto discovery_failure = broken_discovery.before_stream();
    assert(discovery_failure.status == isp::ControlStatus::BackendError);
    assert(discovery_failure.message.find("injected discovery failure") != std::string::npos);
    assert(broken_discovery.shutdown().ok());
    size_t factory_calls = 0;
    isp::IspPipelineCoordinator broken_factory(
        config, {},
        [&](const isp::AiqCameraConfig &,
            const isp::OwnershipEvidence &) -> std::unique_ptr<isp::IRkAiqBackend> {
            ++factory_calls;
            throw std::runtime_error("injected factory failure");
        });
    const auto factory_failure = broken_factory.before_stream();
    assert(factory_failure.status == isp::ControlStatus::BackendError && factory_calls == 1);
    assert(broken_factory.before_stream().status == isp::ControlStatus::InvalidState);
    assert(broken_factory.shutdown().ok());
    ThrowingProvider provider;
    assert(isp::invoke_readiness_operation("before_stream", [&] {
               return provider.before_stream();
           }).status == isp::ControlStatus::BackendError);
    assert(isp::invoke_readiness_operation("before_stream_stop", [&] {
               return provider.before_stream_stop();
           }).status == isp::ControlStatus::BackendError);
    const auto exception_status = isp::query_readiness_status(provider, 0);
    assert(exception_status.state == isp::IspRuntimeState::Error && !exception_status.hardware_confirmed);
    assert(!isp::evaluate_runtime_status(0, exception_status, true).result.ok());
    provider.wrong_camera = true;
    const auto wrong_camera = isp::query_readiness_status(provider, 0);
    assert(wrong_camera.camera_id == 0 && wrong_camera.state == isp::IspRuntimeState::ConfigMismatch);
    assert(!wrong_camera.hardware_confirmed);
    return 0;
}
