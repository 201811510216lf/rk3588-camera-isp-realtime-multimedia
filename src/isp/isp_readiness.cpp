#include "isp/isp_readiness.h"
#include "isp/rkaiq_controller.h"

#include <map>
#include <utility>

namespace isp {
namespace {
IspControlResult result(ControlStatus code, IspCameraId id, std::string message) {
    IspControlResult value;
    value.status = code;
    value.camera_id = id;
    value.operation = "isp_readiness";
    value.backend_name = "isp-pipeline-coordinator";
    value.message = std::move(message);
    return value;
}
bool hard_error(const IspStatus &status) {
    return status.state == IspRuntimeState::OwnershipConflict ||
           status.state == IspRuntimeState::ConfigMismatch || status.state == IspRuntimeState::Error ||
           status.iq.sensor_match == Confirmation::Mismatch ||
           status.iq.sdk_compatible == Confirmation::Mismatch || status.iq.loaded == Confirmation::Mismatch;
}
} // namespace
IspControlResult validate_capture_binding(const IspConfig &config, const CaptureBinding &capture) {
    const auto checked = validate_config(config);
    if (!checked.ok())
        return checked;
    if (config.cameras.size() != 2)
        return result(ControlStatus::ConfigMismatch, 0, "Pipeline requires two explicit ISP camera mappings");
    for (const auto &camera : config.cameras) {
        if (camera.video_node != capture.nodes[camera.camera_id] || camera.width != capture.width ||
            camera.height != capture.height || camera.fps != capture.fps ||
            (capture.pixel_format != "auto" && camera.pixel_format != capture.pixel_format))
            return result(ControlStatus::ConfigMismatch, camera.camera_id,
                          "ISP mapping and requested Camera node/size/FPS/format differ");
    }
    return result(ControlStatus::Ok, 0, "Explicit logical Camera bindings match capture configuration");
}
IspReadiness evaluate_readiness(const AiqCameraConfig &config, const IspCameraEvidence &evidence,
                                const IspStatus &status, bool allow_unverified) {
    IspReadiness output;
    output.result = result(ControlStatus::BackendUnavailable, config.camera_id,
                           "ISP readiness is not confirmed; source=NOT_MEASURED");
    if (status.camera_id != config.camera_id || evidence.camera_id != config.camera_id ||
        (!status.sensor.entity.empty() && status.sensor.entity != config.sensor_entity) ||
        (!evidence.output.video_node.empty() && evidence.output.video_node != config.video_node)) {
        output.result =
            result(ControlStatus::ConfigMismatch, config.camera_id, "Camera identity/node mismatch");
        return output;
    }
    if (hard_error(status)) {
        output.result =
            result(status.state == IspRuntimeState::OwnershipConflict ? ControlStatus::OwnershipConflict
                                                                      : ControlStatus::ConfigMismatch,
                   config.camera_id, "Ownership/IQ/backend state prevents capture: " + status.message);
        return output;
    }
    if (status.ownership != IspAiqOwnershipMode::Unavailable && status.ownership != config.ownership) {
        output.result =
            result(ControlStatus::OwnershipConflict, config.camera_id,
                   "Observed owner mode differs from configured ownership; diagnostics cannot bypass it");
        return output;
    }
    if (!config.expected_sensor_name.empty() && !status.sensor.sensor_name.empty() &&
        config.expected_sensor_name != status.sensor.sensor_name) {
        output.result =
            result(ControlStatus::ConfigMismatch, config.camera_id, "Confirmed sensor name mismatch");
        return output;
    }
    if ((!evidence.graph.entities.empty() && !evidence.mapping_valid) ||
        (!evidence.output.active_format.empty() && !evidence.output_valid)) {
        output.result = result(ControlStatus::ConfigMismatch, config.camera_id,
                               "Observed topology/output does not match configured processed ISP route");
        return output;
    }
    const bool verified =
        evidence.valid() && evidence.live_evidence &&
        (evidence.isp_generation == "RKISP30_REPORTED" || status.backend.platform == "RKISP30") &&
        status.backend.real_hardware_backend && status.backend.evidence == EvidenceSource::LiveDevice &&
        status.hardware_confirmed && status.sensor.confirmed &&
        status.sensor.evidence == EvidenceSource::LiveDevice &&
        status.sensor.entity == config.sensor_entity && status.ownership == config.ownership &&
        status.iq.file_readable && status.iq.sensor_match == Confirmation::Confirmed &&
        status.iq.sdk_compatible == Confirmation::Confirmed && status.iq.loaded == Confirmation::Confirmed &&
        (status.state == IspRuntimeState::Ready || status.state == IspRuntimeState::Running);
    if (verified) {
        output.ready = true;
        output.result =
            result(ControlStatus::Ok, config.camera_id, "ISP readiness confirmed by live backend evidence");
        output.result.hardware_confirmed = true;
    } else if (allow_unverified) {
        output.diagnostic_unverified = true;
        output.result =
            result(ControlStatus::Ok, config.camera_id,
                   "UNVERIFIED_ISP diagnostic capture explicitly enabled; readiness remains unconfirmed");
    }
    return output;
}

struct IspPipelineCoordinator::Impl {
    IspConfig config;
    std::unique_ptr<IMediaGraphInspector> inspector;
    BackendFactory factory;
    RkAiqManager manager;
    std::map<IspCameraId, IspCameraEvidence> evidence;
    bool attempted = false;
    bool prepared = false;
};
IspPipelineCoordinator::IspPipelineCoordinator(IspConfig config,
                                               std::unique_ptr<IMediaGraphInspector> inspector,
                                               BackendFactory factory)
    : impl_(new Impl) {
    impl_->config = std::move(config);
    impl_->inspector = inspector ? std::move(inspector) : std::make_unique<ReadOnlyMediaGraphInspector>();
    impl_->factory = factory
                         ? std::move(factory)
                         : BackendFactory{[](const AiqCameraConfig &camera, const OwnershipEvidence &owner) {
                               return camera.ownership == IspAiqOwnershipMode::ExternalService
                                          ? create_external_service_backend(camera, owner)
                                          : (camera.ownership == IspAiqOwnershipMode::InProcess
                                                 ? create_rockchip_backend()
                                                 : create_unavailable_backend());
                           }};
}
IspPipelineCoordinator::~IspPipelineCoordinator() { (void)shutdown(); }
IspControlResult IspPipelineCoordinator::before_stream() {
    auto &p = *impl_;
    if (p.attempted)
        return result(ControlStatus::InvalidState, 0, "ISP coordinator is single-use");
    p.attempted = true;
    auto checked = validate_config(p.config);
    if (!checked.ok())
        return checked;
    const auto owner = discover_aiq_ownership();
    for (const auto &camera : p.config.cameras) {
        auto evidence = inspect_isp_camera(camera, *p.inspector);
        if (camera.ownership == IspAiqOwnershipMode::InProcess &&
            (!evidence.valid() || !evidence.live_evidence))
            return result(ControlStatus::ConfigMismatch, camera.camera_id,
                          "In-process context requires a verified live Sensor/ISP/output mapping");
        p.evidence.emplace(camera.camera_id, std::move(evidence));
        auto rc = p.manager.add_camera(camera, p.factory(camera, owner), owner);
        if (!rc.ok()) {
            (void)p.manager.shutdown_all();
            return rc;
        }
    }
    auto rc = p.manager.initialize_all();
    if (!rc.ok())
        return rc;
    rc = p.manager.start_phase(AiqStartOrder::BeforeStream);
    if (!rc.ok())
        return rc;
    for (const auto &camera : p.config.cameras) {
        // BSP-declared after-stream contexts are prepared here, started and verified in after_stream().
        if (camera.ownership == IspAiqOwnershipMode::InProcess &&
            camera.start_order == AiqStartOrder::AfterStream)
            continue;
        const auto readiness = evaluate_readiness(camera, p.evidence.at(camera.camera_id),
                                                  status(camera.camera_id), p.config.allow_unverified_isp);
        if (!readiness.result.ok()) {
            (void)p.manager.shutdown_all();
            return readiness.result;
        }
    }
    p.prepared = true;
    return result(ControlStatus::Ok, 0,
                  "ISP pre-stream phase accepted; after-stream verification still required");
}
IspControlResult IspPipelineCoordinator::after_stream() {
    auto &p = *impl_;
    if (!p.prepared)
        return result(ControlStatus::InvalidState, 0, "ISP pre-stream phase was not accepted");
    auto rc = p.manager.start_phase(AiqStartOrder::AfterStream, true);
    if (!rc.ok())
        return rc;
    for (const auto &camera : p.config.cameras) {
        const auto readiness = evaluate_readiness(camera, p.evidence.at(camera.camera_id),
                                                  status(camera.camera_id), p.config.allow_unverified_isp);
        if (!readiness.result.ok())
            return readiness.result;
    }
    return result(ControlStatus::Ok, 0,
                  "ISP post-stream phase accepted; consult per-camera confirmation state");
}
IspControlResult IspPipelineCoordinator::shutdown() {
    impl_->prepared = false;
    return impl_->manager.shutdown_all();
}
IspControlResult IspPipelineCoordinator::before_stream_stop() {
    return impl_->manager.shutdown_phase(AiqStopOrder::BeforeStreamOff);
}
IspControlResult IspPipelineCoordinator::after_stream_stop() {
    auto result = impl_->manager.shutdown_phase(AiqStopOrder::AfterStreamOff);
    if (!result.ok())
        return result;
    return shutdown();
}
IspStatus IspPipelineCoordinator::status(IspCameraId id) const {
    IspStatus status;
    status.camera_id = id;
    const auto *controller = impl_->manager.controller(id);
    if (controller)
        (void)controller->query(&status);
    else {
        status.state = IspRuntimeState::Unavailable;
        status.message = "Camera has no configured AIQ controller";
    }
    return status;
}
} // namespace isp
