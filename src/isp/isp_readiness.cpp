#include "isp/isp_readiness.h"
#include "isp/rkaiq_controller.h"

#include <exception>
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
bool valid_state(IspRuntimeState state) {
    switch (state) {
    case IspRuntimeState::Unknown:
    case IspRuntimeState::Discovered:
    case IspRuntimeState::Ready:
    case IspRuntimeState::Running:
    case IspRuntimeState::Degraded:
    case IspRuntimeState::Unavailable:
    case IspRuntimeState::OwnershipConflict:
    case IspRuntimeState::ConfigMismatch:
    case IspRuntimeState::Error:
        return true;
    }
    return false;
}
bool valid_owner(IspAiqOwnershipMode owner) {
    return owner == IspAiqOwnershipMode::ExternalService || owner == IspAiqOwnershipMode::InProcess ||
           owner == IspAiqOwnershipMode::Unavailable;
}
bool confirmed_status(const IspStatus &status) {
    return status.backend.real_hardware_backend && status.backend.evidence == EvidenceSource::LiveDevice &&
           status.hardware_confirmed && status.sensor.confirmed && !status.sensor.entity.empty() &&
           status.sensor.evidence == EvidenceSource::LiveDevice &&
           status.ownership != IspAiqOwnershipMode::Unavailable && status.iq.file_readable &&
           status.iq.sensor_match == Confirmation::Confirmed &&
           status.iq.sdk_compatible == Confirmation::Confirmed &&
           status.iq.loaded == Confirmation::Confirmed &&
           (status.state == IspRuntimeState::Ready || status.state == IspRuntimeState::Running);
}
IspReadiness invalid_status(IspCameraId expected_id, const IspStatus &status) {
    IspReadiness output;
    output.result = result(ControlStatus::Ok, expected_id, "ISP status shape accepted");
    if (expected_id > 1 || status.camera_id != expected_id || !valid_state(status.state) ||
        !valid_owner(status.ownership)) {
        output.result = result(ControlStatus::ConfigMismatch, expected_id,
                               "Invalid logical Camera identity, runtime state or ownership mode");
    } else if (status.state == IspRuntimeState::OwnershipConflict) {
        output.result = result(ControlStatus::OwnershipConflict, expected_id,
                               "ISP ownership prevents capture: " + status.message);
    } else if (status.state == IspRuntimeState::Error) {
        output.result =
            result(ControlStatus::BackendError, expected_id, "ISP backend failed: " + status.message);
    } else if (status.state == IspRuntimeState::ConfigMismatch ||
               status.iq.sensor_match == Confirmation::Mismatch ||
               status.iq.sdk_compatible == Confirmation::Mismatch ||
               status.iq.loaded == Confirmation::Mismatch) {
        output.result = result(ControlStatus::ConfigMismatch, expected_id,
                               "ISP/Sensor/IQ configuration prevents capture: " + status.message);
    }
    return output;
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
IspReadiness evaluate_runtime_status(IspCameraId expected_id, const IspStatus &status,
                                     bool allow_unverified) {
    auto output = invalid_status(expected_id, status);
    if (!output.result.ok())
        return output;
    output.ready = confirmed_status(status);
    if (output.ready) {
        output.result = result(ControlStatus::Ok, expected_id, "Live ISP runtime confirmation is intact");
        output.result.hardware_confirmed = true;
    } else if (allow_unverified) {
        output.diagnostic_unverified = true;
        output.result = result(ControlStatus::Ok, expected_id,
                               "UNVERIFIED_ISP diagnostic capture: live ISP readiness is not confirmed");
    } else {
        output.result = result(ControlStatus::BackendUnavailable, expected_id,
                               "ISP runtime readiness lost or unconfirmed; strict capture cannot continue");
    }
    return output;
}
IspReadiness evaluate_readiness(const AiqCameraConfig &config, const IspCameraEvidence &evidence,
                                const IspStatus &status, bool allow_unverified) {
    auto output = invalid_status(config.camera_id, status);
    if (!output.result.ok())
        return output;
    output.result = result(ControlStatus::BackendUnavailable, config.camera_id,
                           "ISP readiness is not confirmed; source=NOT_MEASURED");
    if (!valid_owner(config.ownership) || evidence.camera_id != config.camera_id ||
        (!status.sensor.entity.empty() && status.sensor.entity != config.sensor_entity) ||
        (!evidence.sensor.identity.entity.empty() &&
         evidence.sensor.identity.entity != config.sensor_entity) ||
        (!evidence.graph.media_device.empty() && evidence.graph.media_device != config.media_device) ||
        (!evidence.output.video_node.empty() && evidence.output.video_node != config.video_node)) {
        output.result =
            result(ControlStatus::ConfigMismatch, config.camera_id, "Camera identity/node mismatch");
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
        evidence.valid() && evidence.live_evidence && evidence.graph.evidence == EvidenceSource::LiveDevice &&
        evidence.output.evidence == EvidenceSource::LiveDevice && evidence.sensor.identity.confirmed &&
        evidence.sensor.identity.evidence == EvidenceSource::LiveDevice &&
        evidence.sensor.identity.entity == config.sensor_entity &&
        (evidence.isp_generation == "RKISP30_REPORTED" || status.backend.platform == "RKISP30") &&
        status.sensor.entity == config.sensor_entity && status.ownership == config.ownership &&
        confirmed_status(status);
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
IspControlResult invoke_readiness_operation(const char *operation,
                                            const std::function<IspControlResult()> &callback) {
    if (!operation || !callback)
        return result(ControlStatus::InvalidArgument, 0, "Missing ISP operation callback");
    try {
        return callback();
    } catch (const std::exception &error) {
        auto failure =
            result(ControlStatus::BackendError, 0, std::string("ISP provider exception: ") + error.what());
        failure.operation = operation;
        return failure;
    } catch (...) {
        auto failure = result(ControlStatus::BackendError, 0, "Unknown ISP provider exception");
        failure.operation = operation;
        return failure;
    }
}
IspStatus query_readiness_status(const IspReadinessProvider &provider, IspCameraId id) {
    IspStatus snapshot;
    snapshot.camera_id = id;
    const auto checked = invoke_readiness_operation("query_status", [&] {
        snapshot = provider.status(id);
        return result(ControlStatus::Ok, id, "ISP status callback returned");
    });
    if (!checked.ok()) {
        snapshot = {};
        snapshot.camera_id = id;
        snapshot.state = IspRuntimeState::Error;
        snapshot.message = checked.message;
    } else if (snapshot.camera_id != id || id > 1) {
        const auto reported_id = snapshot.camera_id;
        snapshot = {};
        snapshot.camera_id = id;
        snapshot.state = IspRuntimeState::ConfigMismatch;
        snapshot.message = "ISP provider returned Camera " + std::to_string(reported_id) +
                           " for requested logical Camera " + std::to_string(id);
    }
    return snapshot;
}

struct IspPipelineCoordinator::Impl {
    IspConfig config;
    std::unique_ptr<IMediaGraphInspector> inspector;
    BackendFactory factory;
    RkAiqManager manager;
    std::map<IspCameraId, IspCameraEvidence> evidence;
    enum class Phase { Fresh, Preparing, Prepared, StreamActive, Stopping, Stopped, Failed };
    Phase phase = Phase::Fresh;
    bool before_stop_attempted = false;
    bool before_stop_complete = false;
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
IspPipelineCoordinator::~IspPipelineCoordinator() {
    try {
        (void)shutdown();
    } catch (...) { /* A destructor cannot propagate backend/provider failures. */
    }
}
IspControlResult IspPipelineCoordinator::before_stream() {
    auto &p = *impl_;
    if (p.phase != Impl::Phase::Fresh)
        return result(ControlStatus::InvalidState, 0, "ISP coordinator is single-use");
    p.phase = Impl::Phase::Preparing;
    auto started = invoke_readiness_operation("before_stream", [&] {
        auto checked = validate_config(p.config);
        if (!checked.ok())
            return checked;
        const auto owner = discover_aiq_ownership();
        // Complete discovery for both Cameras before creating or starting any owned context.
        for (const auto &camera : p.config.cameras) {
            auto evidence = inspect_isp_camera(camera, *p.inspector);
            if ((camera.ownership == IspAiqOwnershipMode::InProcess &&
                 (!evidence.valid() || !evidence.live_evidence)) ||
                (!evidence.graph.entities.empty() && !evidence.mapping_valid) ||
                (!evidence.output.active_format.empty() && !evidence.output_valid))
                return result(ControlStatus::ConfigMismatch, camera.camera_id,
                              "Sensor/ISP/output mapping is missing or conflicts with capture configuration");
            p.evidence.emplace(camera.camera_id, std::move(evidence));
        }
        for (const auto &camera : p.config.cameras) {
            auto registered = p.manager.add_camera(camera, p.factory(camera, owner), owner);
            if (!registered.ok())
                return registered;
        }
        auto ready = p.manager.initialize_all();
        if (!ready.ok())
            return ready;
        ready = p.manager.start_phase(AiqStartOrder::BeforeStream);
        if (!ready.ok())
            return ready;
        for (const auto &camera : p.config.cameras) {
            if (camera.ownership == IspAiqOwnershipMode::InProcess &&
                camera.start_order == AiqStartOrder::AfterStream)
                continue;
            const auto readiness =
                evaluate_readiness(camera, p.evidence.at(camera.camera_id), status(camera.camera_id),
                                   p.config.allow_unverified_isp);
            if (!readiness.result.ok())
                return readiness.result;
        }
        return result(ControlStatus::Ok, 0,
                      "ISP pre-stream phase accepted; after-stream verification still required");
    });
    if (!started.ok()) {
        const auto rollback =
            invoke_readiness_operation("rollback_before_stream", [&] { return p.manager.shutdown_all(); });
        if (!rollback.ok())
            started.message += "; rollback failed: " + rollback.message;
        p.phase = Impl::Phase::Failed;
    } else
        p.phase = Impl::Phase::Prepared;
    return started;
}
IspControlResult IspPipelineCoordinator::after_stream() {
    auto &p = *impl_;
    if (p.phase != Impl::Phase::Prepared)
        return result(ControlStatus::InvalidState, 0,
                      "ISP after-stream phase is repeated, stopped or not prepared");
    // Mark attempted before invoking the SDK so failure cannot be retried on an uncertain context.
    p.phase = Impl::Phase::StreamActive;
    return invoke_readiness_operation("after_stream", [&] {
        auto ready = p.manager.start_phase(AiqStartOrder::AfterStream, true);
        if (!ready.ok())
            return ready;
        for (const auto &camera : p.config.cameras) {
            const auto readiness =
                evaluate_readiness(camera, p.evidence.at(camera.camera_id), status(camera.camera_id),
                                   p.config.allow_unverified_isp);
            if (!readiness.result.ok())
                return readiness.result;
        }
        return result(ControlStatus::Ok, 0,
                      "ISP post-stream phase accepted; consult per-camera confirmation state");
    });
}
IspControlResult IspPipelineCoordinator::shutdown() {
    auto &p = *impl_;
    if (p.phase == Impl::Phase::Stopped)
        return result(ControlStatus::Ok, 0, "ISP coordinator is already stopped");
    const auto stopped = invoke_readiness_operation("shutdown", [&] { return p.manager.shutdown_all(); });
    p.phase = stopped.ok() ? Impl::Phase::Stopped : Impl::Phase::Failed;
    return stopped;
}
IspControlResult IspPipelineCoordinator::before_stream_stop() {
    auto &p = *impl_;
    if (p.phase == Impl::Phase::Stopped || p.before_stop_complete)
        return result(ControlStatus::Ok, 0, "ISP pre-STREAMOFF phase is already complete");
    p.phase = Impl::Phase::Stopping;
    p.before_stop_attempted = true;
    const auto stopped = invoke_readiness_operation(
        "before_stream_stop", [&] { return p.manager.shutdown_phase(AiqStopOrder::BeforeStreamOff); });
    p.before_stop_complete = stopped.ok();
    return stopped;
}
IspControlResult IspPipelineCoordinator::after_stream_stop() {
    auto &p = *impl_;
    if (p.phase == Impl::Phase::Stopped)
        return result(ControlStatus::Ok, 0, "ISP coordinator is already stopped");
    if (p.phase != Impl::Phase::Stopping || !p.before_stop_attempted)
        return result(ControlStatus::InvalidState, 0,
                      "ISP post-STREAMOFF cleanup requires the pre-STREAMOFF phase");
    const auto stopped = invoke_readiness_operation(
        "after_stream_stop", [&] { return p.manager.shutdown_phase(AiqStopOrder::AfterStreamOff); });
    if (!stopped.ok())
        return stopped;
    if (!p.before_stop_complete)
        return result(ControlStatus::BackendError, 0,
                      "Pre-STREAMOFF cleanup failed; ISP ownership remains uncertain");
    return shutdown();
}
IspStatus IspPipelineCoordinator::status(IspCameraId id) const {
    IspStatus status;
    status.camera_id = id;
    const auto *controller = impl_->manager.controller(id);
    if (controller) {
        const auto queried =
            invoke_readiness_operation("query_status", [&] { return controller->query(&status); });
        if (!queried.ok()) {
            status.hardware_confirmed = false;
            status.controls.hardware_confirmed = false;
            status.iq.loaded = Confirmation::NotMeasured;
            status.state = queried.status == ControlStatus::OwnershipConflict
                               ? IspRuntimeState::OwnershipConflict
                               : (queried.status == ControlStatus::ConfigMismatch
                                      ? IspRuntimeState::ConfigMismatch
                                      : (queried.status == ControlStatus::BackendUnavailable ||
                                                 queried.status == ControlStatus::Unsupported
                                             ? IspRuntimeState::Unavailable
                                             : IspRuntimeState::Error));
            status.message = queried.message;
        }
    } else {
        status.state = IspRuntimeState::Unavailable;
        status.message = "Camera has no configured AIQ controller";
    }
    return status;
}
} // namespace isp
