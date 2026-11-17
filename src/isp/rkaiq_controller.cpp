#include "isp/rkaiq_controller.h"

#include <utility>

namespace isp {
namespace {
IspControlResult manager_result(ControlStatus status, const char *operation, const std::string &message,
                                IspCameraId id = 0) {
    IspControlResult r;
    r.status = status;
    r.operation = operation;
    r.message = message;
    r.camera_id = id;
    r.backend_name = "rkaiq-manager";
    return r;
}
} // namespace
RkAiqManager::~RkAiqManager() {
    try {
        (void)shutdown_all();
    } catch (...) {
    }
}
IspControlResult RkAiqManager::add_camera(AiqCameraConfig config, std::unique_ptr<IRkAiqBackend> backend,
                                          OwnershipEvidence ownership) {
    if (initialized_)
        return manager_result(ControlStatus::InvalidState, "add_camera",
                              "Cannot change camera registry after initialization", config.camera_id);
    if (config.camera_id > 1 || cameras_.count(config.camera_id) || cameras_.size() >= 2)
        return manager_result(ControlStatus::InvalidArgument, "add_camera",
                              "Camera id must be unique and limited to 0/1", config.camera_id);
    if (!backend)
        return manager_result(ControlStatus::BackendUnavailable, "add_camera",
                              "A backend instance is required", config.camera_id);
    for (const auto &entry : cameras_) {
        const auto &other = entry.second.config;
        if (config.ownership != IspAiqOwnershipMode::InProcess ||
            other.ownership != IspAiqOwnershipMode::InProcess)
            continue;
        if (config.isp_instance.empty() || other.isp_instance.empty())
            return manager_result(ControlStatus::ConfigMismatch, "add_camera",
                                  "IN_PROCESS requires explicit ISP resource identity", config.camera_id);
        if (config.isp_instance == other.isp_instance &&
            (config.sensor_entity == other.sensor_entity || !config.independent_context_confirmed ||
             !other.independent_context_confirmed))
            return manager_result(ControlStatus::OwnershipConflict, "add_camera",
                                  "Same ISP requires distinct sensors and BSP-confirmed independent "
                                  "contexts; camera-group API is not inferred",
                                  config.camera_id);
    }
    const auto id = config.camera_id;
    auto controller = std::make_unique<AiqController>(config, std::move(backend), std::move(ownership));
    cameras_.emplace(id, Entry{std::move(config), std::move(controller)});
    return manager_result(ControlStatus::Ok, "add_camera",
                          "Explicit logical-camera/sensor mapping registered", id);
}
IspControlResult RkAiqManager::initialize_all() {
    if (initialized_)
        return manager_result(ControlStatus::InvalidState, "initialize_all",
                              "Manager is already initialized");
    if (cameras_.empty())
        return manager_result(ControlStatus::InvalidArgument, "initialize_all", "No configured cameras");
    initialized_ = true; // A failed rollback must retain the manager lockout state.
    for (auto &entry : cameras_) {
        if (entry.second.config.ownership == IspAiqOwnershipMode::ExternalService)
            continue;
        auto r = entry.second.controller->initialize();
        if (r.ok())
            r = entry.second.controller->prepare();
        if (!r.ok()) {
            const auto rollback = shutdown_all();
            if (!rollback.ok())
                r.message += "; manager rollback: " + rollback.message;
            return r;
        }
    }
    initialized_ = true;
    return manager_result(
        ControlStatus::Ok, "initialize_all",
        "Owned contexts prepared; external services untouched; readiness still requires query evidence");
}
IspControlResult RkAiqManager::start_phase(AiqStartOrder phase, bool stream_active) {
    if (!initialized_ || phase == AiqStartOrder::Unspecified)
        return manager_result(ControlStatus::InvalidState, "start_phase",
                              "Manager must be initialized and BSP start phase explicit");
    for (auto &entry : cameras_) {
        if (entry.second.config.ownership != IspAiqOwnershipMode::InProcess ||
            entry.second.config.start_order != phase)
            continue;
        auto r = entry.second.controller->start(stream_active);
        if (!r.ok()) {
            if (stream_active)
                return r; // The pipeline must run the configured stop phases around STREAMOFF.
            const auto rollback = shutdown_all();
            if (!rollback.ok())
                r.message += "; manager rollback: " + rollback.message;
            return r;
        }
    }
    return manager_result(
        ControlStatus::Ok, "start_phase",
        "Selected BSP-declared start phase completed; hardware evidence is queried separately");
}
IspControlResult RkAiqManager::start_all() {
    auto r = start_phase(AiqStartOrder::BeforeStream);
    if (!r.ok())
        return r;
    return start_phase(AiqStartOrder::AfterStream);
}
IspControlResult RkAiqManager::shutdown_all() {
    auto first = manager_result(ControlStatus::Ok, "shutdown_all",
                                "Owned contexts stopped in reverse order; external service untouched");
    for (auto entry = cameras_.rbegin(); entry != cameras_.rend(); ++entry) {
        auto r = entry->second.controller->shutdown();
        if (!r.ok() && first.ok())
            first = std::move(r);
    }
    if (first.ok())
        initialized_ = false;
    return first;
}
IspControlResult RkAiqManager::shutdown_phase(AiqStopOrder phase) {
    if (phase == AiqStopOrder::Unspecified)
        return manager_result(ControlStatus::InvalidArgument, "shutdown_phase",
                              "BSP shutdown phase must be explicit");
    auto first = manager_result(ControlStatus::Ok, "shutdown_phase",
                                "Selected BSP shutdown phase completed; external services untouched");
    for (auto entry = cameras_.rbegin(); entry != cameras_.rend(); ++entry) {
        if (entry->second.config.ownership != IspAiqOwnershipMode::InProcess ||
            entry->second.config.stop_order != phase)
            continue;
        auto r = entry->second.controller->shutdown();
        if (!r.ok() && first.ok())
            first = std::move(r);
    }
    // Partial phase shutdown does not unlock the registry or permit reinitialization.
    return first;
}
AiqController *RkAiqManager::controller(IspCameraId id) {
    auto entry = cameras_.find(id);
    return entry == cameras_.end() ? nullptr : entry->second.controller.get();
}
const AiqController *RkAiqManager::controller(IspCameraId id) const {
    auto entry = cameras_.find(id);
    return entry == cameras_.end() ? nullptr : entry->second.controller.get();
}
std::vector<IspCameraId> RkAiqManager::camera_ids() const {
    std::vector<IspCameraId> out;
    for (const auto &entry : cameras_)
        out.push_back(entry.first);
    return out;
}
} // namespace isp
