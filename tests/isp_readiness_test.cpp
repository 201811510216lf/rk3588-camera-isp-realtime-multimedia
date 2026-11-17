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
// Existing offline fixtures exercise when negotiation may occur; no product Camera or SDK fake.
class NegotiationInspector final : public isp::IMediaGraphInspector {
  public:
    bool negotiated = false;
    bool invalid_route = false;
    bool wrong_node = false;
    size_t graph_reads = 0, output_reads = 0;
    bool inspect(const std::string &device, const std::string &path, isp::IspDeviceInfo *output,
                 std::string *error) override {
        ++graph_reads;
        if (!reader.inspect(device, path, output, error))
            return false;
        if (invalid_route)
            for (auto &link : output->links)
                link.enabled = false;
        return true;
    }
    bool query_output(const std::string &node, const std::string &path, isp::IspOutputNode *output,
                      std::string *error) override {
        ++output_reads;
        if (!reader.query_output(node, path, output, error))
            return false;
        if (!negotiated) {
            // Driver defaults can differ before V4L2 S_FMT/G_FMT and S_PARM/G_PARM.
            output->width = 1280;
            output->height = 720;
            output->fps_numerator = 25;
            output->active_format = "YUYV";
        }
        if (wrong_node)
            output->video_node = "/dev/wrong-output";
        return true;
    }

  private:
    isp::ReadOnlyMediaGraphInspector reader;
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
    auto independent_sizes = config;
    independent_sizes.cameras[1].width = 1280;
    independent_sizes.cameras[1].height = 720;
    independent_sizes.cameras[1].fps = 25;
    // Omitted capture values inherit each entry, including differing independent-camera layouts.
    isp::CaptureBinding inherited;
    assert(isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.nodes[0] = "/dev/video42";
    assert(isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.nodes[1] = "/dev/wrong";
    assert(!isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.nodes[1].clear();
    inherited.width = 640;
    assert(!isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.width = 0;
    inherited.height = 480;
    assert(!isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.height = 0;
    inherited.fps = 30;
    assert(!isp::validate_capture_binding(independent_sizes, inherited).ok());
    inherited.fps = 0;
    inherited.pixel_format = "NV12";
    assert(!isp::validate_capture_binding(independent_sizes, inherited).ok());
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

    auto negotiating_inspector = std::make_unique<NegotiationInspector>();
    auto *negotiating = negotiating_inspector.get();
    isp::IspPipelineCoordinator negotiate(config, std::move(negotiating_inspector));
    const auto pre_negotiation = negotiate.before_stream();
    assert(pre_negotiation.ok() && !pre_negotiation.hardware_confirmed);
    assert(negotiating->graph_reads == 2 && negotiating->output_reads == 2);
    assert(!negotiate.status(0).hardware_confirmed); // Offline data never creates live readiness.
    negotiating->negotiated = true; // Represents Camera open/format negotiation, without real ioctls.
    assert(negotiate.after_stream().ok());
    assert(negotiating->graph_reads == 4 && negotiating->output_reads == 4);
    assert(!negotiate.status(0).hardware_confirmed);
    assert(negotiate.before_stream_stop().ok() && negotiate.after_stream_stop().ok());

    auto unchanged_inspector = std::make_unique<NegotiationInspector>();
    auto *unchanged = unchanged_inspector.get();
    isp::IspPipelineCoordinator still_wrong(config, std::move(unchanged_inspector));
    assert(still_wrong.before_stream().ok());
    assert(still_wrong.after_stream().status == isp::ControlStatus::ConfigMismatch);
    assert(unchanged->graph_reads == 4 && unchanged->output_reads == 4);
    assert(still_wrong.before_stream_stop().ok() && still_wrong.after_stream_stop().ok());

    size_t invalid_factories = 0;
    auto count_factory = [&](const isp::AiqCameraConfig &,
                             const isp::OwnershipEvidence &) -> std::unique_ptr<isp::IRkAiqBackend> {
        ++invalid_factories;
        return isp::create_unavailable_backend();
    };
    auto invalid_route = std::make_unique<NegotiationInspector>();
    invalid_route->invalid_route = true;
    isp::IspPipelineCoordinator bad_route(config, std::move(invalid_route), count_factory);
    assert(bad_route.before_stream().status == isp::ControlStatus::ConfigMismatch);
    assert(invalid_factories == 0);
    assert(bad_route.shutdown().ok());
    auto wrong_node = std::make_unique<NegotiationInspector>();
    wrong_node->wrong_node = true;
    isp::IspPipelineCoordinator bad_node(config, std::move(wrong_node), count_factory);
    assert(bad_node.before_stream().status == isp::ControlStatus::ConfigMismatch);
    assert(invalid_factories == 0);
    assert(bad_node.shutdown().ok());
    auto raw_config = config;
    raw_config.cameras[0].pixel_format = "RAW10";
    isp::IspPipelineCoordinator raw_target(raw_config, std::make_unique<NegotiationInspector>(),
                                           count_factory);
    assert(raw_target.before_stream().status == isp::ControlStatus::InvalidArgument);
    assert(invalid_factories == 0);
    assert(raw_target.shutdown().ok());

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
