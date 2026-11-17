#include "aiq_controller.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>
#ifdef __linux__
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
using namespace isp;
AiqResult legacy(const IspControlResult &r, std::string *error) {
    if (error)
        *error = r.message;
    switch (r.status) {
    case ControlStatus::Ok:
        return AiqResult::Ok;
    case ControlStatus::InvalidArgument:
        return AiqResult::InvalidArgument;
    case ControlStatus::NotFound:
        return AiqResult::NotFound;
    case ControlStatus::Unsupported:
    case ControlStatus::BackendUnavailable:
        return AiqResult::Unsupported;
    default:
        return AiqResult::BackendError;
    }
}
IspControlResult call(const std::function<IspControlResult()> &operation) {
    try {
        return operation();
    } catch (const std::exception &e) {
        IspControlResult r;
        r.status = ControlStatus::BackendError;
        r.message = std::string("Backend exception: ") + e.what();
        return r;
    } catch (...) {
        IspControlResult r;
        r.status = ControlStatus::BackendError;
        r.message = "Unknown backend exception";
        return r;
    }
}
bool equal(double actual, double expected) {
    return std::isfinite(actual) && std::abs(actual - expected) <= std::max(1e-6, std::abs(expected) * 1e-6);
}
bool valid_controls(const ControlSnapshot &values, const IspControlCapabilities &caps, std::string *error) {
    if ((values.ae_mode && *values.ae_mode != AeMode::Auto && *values.ae_mode != AeMode::Manual) ||
        (values.awb_mode && *values.awb_mode != AwbMode::Auto && *values.awb_mode != AwbMode::Manual)) {
        *error = "Backend returned an invalid AE/AWB mode";
        return false;
    }
    auto number_valid = [&](const std::optional<double> &value, const NumericControlRange &range,
                            const char *name) {
        if (!value)
            return true;
        if (!std::isfinite(*value) || *value <= 0) {
            *error = std::string("Backend returned non-finite/non-positive ") + name;
            return false;
        }
        if (std::isfinite(range.minimum) && std::isfinite(range.maximum) && range.maximum >= range.minimum &&
            range.maximum > 0 && (*value < range.minimum || *value > range.maximum)) {
            *error = std::string("Backend readback is outside the reported ") + name + " range";
            return false;
        }
        return true;
    };
    return number_valid(values.exposure_us, caps.exposure_us, "exposure") &&
           number_valid(values.analog_gain, caps.gain, "gain");
}
} // namespace

AiqController::AiqController(AiqControllerConfig config) : config_(std::move(config)) {
    camera_config_.camera_id = config_.camera_id;
    camera_config_.ownership = isp::IspAiqOwnershipMode::ExternalService;
    if (!config_.iq_roots.empty())
        camera_config_.iq_root = config_.iq_roots.front();
    ownership_ = isp::discover_aiq_ownership(config_.daemon_hint);
    backend_ = isp::create_external_service_backend(camera_config_, ownership_);
}
AiqController::AiqController(isp::AiqCameraConfig config, std::unique_ptr<isp::IRkAiqBackend> backend,
                             isp::OwnershipEvidence ownership)
    : camera_config_(std::move(config)), backend_(std::move(backend)), ownership_(std::move(ownership)) {
    config_.camera_id = camera_config_.camera_id;
    if (!backend_) {
        if (camera_config_.ownership == isp::IspAiqOwnershipMode::Unavailable)
            backend_ = isp::create_unavailable_backend();
        else if (camera_config_.ownership == isp::IspAiqOwnershipMode::ExternalService)
            backend_ = isp::create_external_service_backend(camera_config_, ownership_);
        else
            backend_ = isp::create_rockchip_backend();
    }
}
AiqController::~AiqController() {
    try {
        (void)shutdown();
    } catch (...) {
    }
}
isp::BackendInfo AiqController::backend_info() const noexcept {
    try {
        return backend_->info();
    } catch (...) {
        isp::BackendInfo info;
        info.name = "backend-info-error";
        return info;
    }
}
isp::IspControlResult AiqController::result(isp::ControlStatus status, const std::string &op,
                                            const std::string &message) const {
    isp::IspControlResult r;
    r.status = status;
    r.operation = op;
    r.camera_id = camera_config_.camera_id;
    r.message = message;
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::decorate(isp::IspControlResult r) const {
    if (r.operation.empty())
        r.operation = "backend_call";
    const auto info = backend_info();
    r.camera_id = camera_config_.camera_id;
    r.backend_name = info.name;
    r.backend_version = info.version;
    r.hardware_confirmed = r.ok() && r.hardware_confirmed && info.real_hardware_backend &&
                           info.evidence == isp::EvidenceSource::LiveDevice;
    if (info.name == "backend-info-error") {
        r.status = isp::ControlStatus::BackendError;
        r.message = "Backend info query threw; hardware confirmation is unavailable";
    }
    return r;
}
void AiqController::release_ownership_lock() {
#ifdef __linux__
    if (ownership_fd_ >= 0) {
        (void)::flock(ownership_fd_, LOCK_UN);
        ::close(ownership_fd_);
        ownership_fd_ = -1;
    }
    if (isp_ownership_fd_ >= 0) {
        (void)::flock(isp_ownership_fd_, LOCK_UN);
        ::close(isp_ownership_fd_);
        isp_ownership_fd_ = -1;
    }
#endif
}
isp::IspControlResult AiqController::initialize() {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ || faulted_)
        return result(ControlStatus::InvalidState, "initialize",
                      "Controller is already initialized or requires cleanup");
    if (camera_config_.ownership != IspAiqOwnershipMode::InProcess)
        return result(ControlStatus::Unsupported, "initialize",
                      "Only IN_PROCESS owns an AIQ context; external service lifecycle is never called");
    if (camera_config_.camera_id > 1 || camera_config_.sensor_entity.empty() ||
        camera_config_.isp_instance.empty() ||
        (camera_config_.start_order != AiqStartOrder::BeforeStream &&
         camera_config_.start_order != AiqStartOrder::AfterStream) ||
        camera_config_.width == 0 || camera_config_.height == 0 || camera_config_.fps == 0)
        return result(ControlStatus::ConfigMismatch, "initialize",
                      "Explicit camera 0/1, sensor entity, ISP resource and BSP start_order are required");
    if (camera_config_.working_mode != IspWorkingMode::Normal &&
        camera_config_.working_mode != IspWorkingMode::Hdr2 &&
        camera_config_.working_mode != IspWorkingMode::Hdr3)
        return result(ControlStatus::InvalidArgument, "initialize", "Invalid working mode");
    if (camera_config_.stop_order != AiqStopOrder::BeforeStreamOff &&
        camera_config_.stop_order != AiqStopOrder::AfterStreamOff)
        return result(ControlStatus::ConfigMismatch, "initialize",
                      "Explicit BSP stop_order is required before taking ownership");
    const auto info = backend_info();
    if (info.real_hardware_backend && info.evidence == EvidenceSource::LiveDevice)
        ownership_ = discover_aiq_ownership();
    if (!ownership_.scan_complete || !ownership_.owners.empty())
        return result(
            ControlStatus::OwnershipConflict, "initialize",
            "An external owner is present or owner absence is unconfirmed; refusing duplicate AIQ context");
    if (!info.sdk_available && info.evidence != EvidenceSource::TestBackend)
        return result(ControlStatus::BackendUnavailable, "initialize",
                      "BLOCKED_HARDWARE: selected RKAIQ SDK adapter is unavailable");
    auto checked = validate_iq_file(camera_config_, &iq_);
    if (!checked.ok())
        return decorate(std::move(checked));
    checked = call([&] { return backend_->validate_iq(camera_config_, &iq_); });
    if (!checked.ok())
        return decorate(std::move(checked));
    if (iq_.sensor_match != Confirmation::Confirmed || iq_.sdk_compatible != Confirmation::Confirmed)
        return result(ControlStatus::ConfigMismatch, "initialize",
                      "SDK has not confirmed sensor/IQ identity and SDK compatibility");
    IspControlCapabilities caps;
    checked = call([&] { return backend_->query_capabilities(&caps); });
    if (!checked.ok())
        return decorate(std::move(checked));
    if ((camera_config_.working_mode == IspWorkingMode::Hdr2 && !caps.hdr2) ||
        (camera_config_.working_mode == IspWorkingMode::Hdr3 && !caps.hdr3))
        return result(ControlStatus::Unsupported, "initialize",
                      "Requested HDR working mode is unsupported by the selected adapter");
    if (info.real_hardware_backend) {
#ifdef __linux__
        // Shared/exclusive ISP-resource locks prevent independently launched apps from assuming separate
        // contexts. A second exclusive sensor lock excludes duplicate contexts even when BSP independent
        // contexts are confirmed.
        auto lock_name = [](const std::string &key, const char *prefix) {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char c : key) {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            std::ostringstream name;
            name << prefix << std::hex << hash << ".lock";
            return name.str();
        };
        std::error_code ec;
        const auto directory = std::filesystem::temp_directory_path(ec);
        if (ec)
            return result(ControlStatus::OwnershipConflict, "initialize",
                          "Cannot find ownership lock directory");
        auto acquire = [&](const std::string &name, int mode) {
            const auto path = directory / name;
            int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (fd >= 0 && ::flock(fd, mode | LOCK_NB) != 0) {
                ::close(fd);
                fd = -1;
            }
            return fd;
        };
        isp_ownership_fd_ = acquire(lock_name(camera_config_.isp_instance, "rkisp-aiq-resource-"),
                                    camera_config_.independent_context_confirmed ? LOCK_SH : LOCK_EX);
        if (isp_ownership_fd_ < 0)
            return result(ControlStatus::OwnershipConflict, "initialize",
                          "ISP resource ownership is incompatible with another process, or cannot be locked");
        ownership_fd_ = acquire(
            lock_name(camera_config_.isp_instance + "/" + camera_config_.sensor_entity, "rkisp-aiq-sensor-"),
            LOCK_EX);
        if (ownership_fd_ < 0) {
            release_ownership_lock();
            return result(ControlStatus::OwnershipConflict, "initialize",
                          "Another application owns the Sensor context, or its lock cannot be established");
        }
#else
        return result(ControlStatus::BackendUnavailable, "initialize",
                      "Real IN_PROCESS RKAIQ ownership requires the Linux target");
#endif
    }
    auto r = call([&] { return backend_->initialize(camera_config_); });
    if (!r.ok()) {
        const auto rollback = call([&] { return backend_->deinitialize(); });
        if (!rollback.ok()) {
            initialized_ = faulted_ = true;
            r.message += "; rollback deinitialize failed: " + rollback.message;
        } else
            release_ownership_lock();
        return decorate(std::move(r));
    }
    initialized_ = true;
    faulted_ = false;
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::prepare() {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || prepared_ || running_ || faulted_)
        return result(ControlStatus::InvalidState, "prepare",
                      "Prepare requires initialized, unprepared context");
    auto r = call([&] { return backend_->prepare(); });
    if (!r.ok()) {
        auto rollback = call([&] { return backend_->deinitialize(); });
        if (rollback.ok()) {
            initialized_ = false;
            release_ownership_lock();
        } else {
            faulted_ = true;
            r.message += "; rollback deinitialize failed: " + rollback.message;
        }
        return decorate(std::move(r));
    }
    prepared_ = true;
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::start(bool defer_failure_cleanup) {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !prepared_ || running_ || faulted_)
        return result(ControlStatus::InvalidState, "start", "Start requires a prepared, stopped context");
    auto r = call([&] { return backend_->start(); });
    if (!r.ok()) {
        faulted_ = true;
        // A failed start may have acquired hardware resources. Never deinit until stop succeeded.
        if (defer_failure_cleanup) {
            running_ = true;
            r.message += "; cleanup deferred to explicit BSP stop_order around STREAMOFF";
            return decorate(std::move(r));
        }
        auto stopped = call([&] { return backend_->stop(); });
        if (!stopped.ok()) {
            running_ = true;
            r.message += "; rollback stop failed: " + stopped.message;
            return decorate(std::move(r));
        }
        auto rollback = call([&] { return backend_->deinitialize(); });
        if (rollback.ok()) {
            initialized_ = prepared_ = faulted_ = false;
            release_ownership_lock();
        } else
            r.message += "; rollback deinitialize failed: " + rollback.message;
        return decorate(std::move(r));
    }
    running_ = true;
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::stop() {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
        return result(ControlStatus::InvalidState, "stop", "Context is not running");
    auto r = call([&] { return backend_->stop(); });
    if (r.ok())
        running_ = false;
    else
        faulted_ = true;
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::deinitialize() {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return result(ControlStatus::InvalidState, "deinitialize", "Stop must succeed before deinitialize");
    if (!initialized_)
        return result(ControlStatus::InvalidState, "deinitialize", "Context is not initialized");
    auto r = call([&] { return backend_->deinitialize(); });
    if (r.ok()) {
        initialized_ = prepared_ = faulted_ = false;
        release_ownership_lock();
    } else {
        faulted_ = true;
    }
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::shutdown_locked() {
    using namespace isp;
    if (camera_config_.ownership != IspAiqOwnershipMode::InProcess)
        return result(ControlStatus::Ok, "shutdown", "External service lifecycle untouched");
    if (running_) {
        auto r = call([&] { return backend_->stop(); });
        if (!r.ok()) {
            faulted_ = true;
            return decorate(std::move(r));
        }
        running_ = false;
    }
    if (initialized_) {
        auto r = call([&] { return backend_->deinitialize(); });
        if (!r.ok()) {
            faulted_ = true;
            return decorate(std::move(r));
        }
        initialized_ = prepared_ = false;
    }
    faulted_ = false;
    release_ownership_lock();
    return result(ControlStatus::Ok, "shutdown", "Owned context stopped and released");
}
isp::IspControlResult AiqController::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_locked();
}
isp::IspControlResult AiqController::query(isp::IspStatus *status) const {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    if (camera_config_.ownership == IspAiqOwnershipMode::Unavailable) {
        IspControlResult disabled;
        disabled.status = status ? ControlStatus::BackendUnavailable : ControlStatus::InvalidArgument;
        disabled.operation = "query_status";
        disabled.camera_id = camera_config_.camera_id;
        disabled.backend_name = "unavailable-by-configuration";
        disabled.message = "UNAVAILABLE ownership configured; no SDK/backend query was invoked";
        if (status) {
            *status = {};
            status->camera_id = camera_config_.camera_id;
            status->ownership = IspAiqOwnershipMode::Unavailable;
            status->state = IspRuntimeState::Unavailable;
            status->backend.name = disabled.backend_name;
            status->message = disabled.message;
        }
        return disabled;
    }
    if (!status)
        return result(ControlStatus::InvalidArgument, "query_status", "null status");
    if (faulted_) {
        *status = {};
        status->camera_id = camera_config_.camera_id;
        status->ownership = camera_config_.ownership;
        status->backend = backend_info();
        status->state = IspRuntimeState::Error;
        status->message =
            "Context lifecycle is uncertain after a failed operation; explicit shutdown is required";
        return result(ControlStatus::InvalidState, "query_status", status->message);
    }
    const auto observed_info = backend_info();
    const auto observed_owner = camera_config_.ownership == IspAiqOwnershipMode::InProcess &&
                                        observed_info.real_hardware_backend &&
                                        observed_info.evidence == EvidenceSource::LiveDevice
                                    ? discover_aiq_ownership()
                                    : ownership_;
    if (camera_config_.ownership == IspAiqOwnershipMode::InProcess &&
        (!observed_owner.scan_complete || !observed_owner.owners.empty())) {
        *status = {};
        status->camera_id = camera_config_.camera_id;
        status->ownership = camera_config_.ownership;
        status->backend = observed_info;
        status->owner = observed_owner;
        status->state = IspRuntimeState::OwnershipConflict;
        status->message = "External owner candidates appeared or owner absence cannot be confirmed";
        return result(ControlStatus::OwnershipConflict, "query_status", status->message);
    }
    IspStatus report;
    auto r = call([&] { return backend_->query_status(&report); });
    if (r.ok() && report.camera_id != camera_config_.camera_id &&
        (initialized_ || camera_config_.ownership == IspAiqOwnershipMode::ExternalService)) {
        r.status = ControlStatus::ConfigMismatch;
        r.message = "Backend status belongs to a different logical Camera";
    }
    if (r.ok() && !camera_config_.sensor_entity.empty() && !report.sensor.entity.empty() &&
        report.sensor.entity != camera_config_.sensor_entity) {
        r.status = ControlStatus::ConfigMismatch;
        r.message = "Backend status belongs to a different Sensor entity";
    }
    if (r.ok() && report.ownership != IspAiqOwnershipMode::Unavailable &&
        report.ownership != camera_config_.ownership) {
        r.status = ControlStatus::OwnershipConflict;
        r.message = "Backend reported a different ownership mode";
    }
    report.camera_id = camera_config_.camera_id;
    report.ownership = camera_config_.ownership;
    report.backend = backend_info();
    if (camera_config_.ownership != IspAiqOwnershipMode::ExternalService)
        report.owner = observed_owner;
    if (report.sensor.entity.empty()) {
        report.sensor.entity = camera_config_.sensor_entity;
        report.sensor.confirmed = false;
        report.sensor.evidence = EvidenceSource::NotMeasured;
    }
    if (report.iq.resolved_path.empty() && !iq_.resolved_path.empty())
        report.iq = iq_;
    report.capabilities = {};
    report.controls = {};
    if (r.ok()) {
        IspControlCapabilities capabilities;
        auto queried = call([&] { return backend_->query_capabilities(&capabilities); });
        if (!queried.ok()) {
            r = std::move(queried);
            r.message = "Capability query failed: " + r.message;
        } else {
            report.capabilities = capabilities;
            if (running_ && capabilities.readback) {
                auto read = read_controls_locked(&report.controls, capabilities);
                if (!read.ok()) {
                    r = std::move(read);
                    r.message = "Control snapshot failed: " + r.message;
                }
            }
        }
    }
    if (report.backend.name == "backend-info-error") {
        r.status = ControlStatus::BackendError;
        r.message = "Backend info query threw";
    }
    if (r.ok() && report.state == IspRuntimeState::Error) {
        r.status = ControlStatus::BackendError;
        r.message = report.message.empty() ? "Backend reported an ISP error" : report.message;
    } else if (r.ok() && report.state == IspRuntimeState::OwnershipConflict) {
        r.status = ControlStatus::OwnershipConflict;
        r.message = report.message.empty() ? "Backend reported an ownership conflict" : report.message;
    } else if (r.ok() && report.state == IspRuntimeState::ConfigMismatch) {
        r.status = ControlStatus::ConfigMismatch;
        r.message = report.message.empty() ? "Backend reported a configuration mismatch" : report.message;
    }
    if (!r.ok()) {
        report.state = r.status == ControlStatus::ConfigMismatch      ? IspRuntimeState::ConfigMismatch
                       : r.status == ControlStatus::OwnershipConflict ? IspRuntimeState::OwnershipConflict
                       : r.status == ControlStatus::BackendError      ? IspRuntimeState::Error
                                                                      : IspRuntimeState::Unavailable;
        report.hardware_confirmed = false;
        report.controls = {};
        report.sensor.confirmed = false;
        if (report.iq.loaded == Confirmation::Confirmed)
            report.iq.loaded = Confirmation::NotMeasured;
        if (report.iq.sensor_match == Confirmation::Confirmed)
            report.iq.sensor_match = Confirmation::NotMeasured;
        if (report.iq.sdk_compatible == Confirmation::Confirmed)
            report.iq.sdk_compatible = Confirmation::NotMeasured;
        report.message = r.message;
    }
    if (camera_config_.ownership == IspAiqOwnershipMode::InProcess && r.ok()) {
        if (!initialized_) {
            report.state = IspRuntimeState::Unavailable;
            report.hardware_confirmed = false;
            report.iq.loaded = Confirmation::NotMeasured;
            report.message = "IN_PROCESS context is not initialized";
        } else if (!running_ &&
                   (report.state == IspRuntimeState::Ready || report.state == IspRuntimeState::Running)) {
            report.state = IspRuntimeState::Discovered;
            report.hardware_confirmed = false;
            report.message = "Owned context has not entered running state";
        }
    }
    if (!iq_.resolved_path.empty() && !report.iq.resolved_path.empty() &&
        std::filesystem::path(iq_.resolved_path).lexically_normal() !=
            std::filesystem::path(report.iq.resolved_path).lexically_normal()) {
        report.state = IspRuntimeState::ConfigMismatch;
        report.iq.loaded = Confirmation::Mismatch;
        report.hardware_confirmed = false;
        report.message = "SDK-reported IQ file differs from the validated selected file";
        r.status = ControlStatus::ConfigMismatch;
        r.message = report.message;
    }
    if (camera_config_.ownership == IspAiqOwnershipMode::InProcess &&
        (!observed_owner.scan_complete || !observed_owner.owners.empty())) {
        report.state = IspRuntimeState::OwnershipConflict;
        report.hardware_confirmed = false;
        r.status = ControlStatus::OwnershipConflict;
        r.message = "Owned context has unconfirmed or conflicting ownership";
    }
    // Even a test backend reporting RUNNING can never establish real hardware readiness.
    const bool hardware =
        report.backend.real_hardware_backend && report.backend.evidence == EvidenceSource::LiveDevice;
    report.hardware_confirmed = hardware && report.hardware_confirmed;
    report.controls.hardware_confirmed = hardware && report.controls.hardware_confirmed;
    report.sensor.confirmed = hardware && report.sensor.confirmed;
    if (!hardware) {
        report.iq.sensor_match = Confirmation::NotMeasured;
        report.iq.sdk_compatible = Confirmation::NotMeasured;
        report.iq.loaded = Confirmation::NotMeasured;
    }
    if (!hardware && (report.state == IspRuntimeState::Ready || report.state == IspRuntimeState::Running)) {
        report.state = IspRuntimeState::Discovered;
        report.message = "MOCK_OR_TEST or unverified adapter state; hardware readiness is not confirmed";
    }
    *status = std::move(report);
    return decorate(std::move(r));
}
isp::IspControlResult
AiqController::read_controls_locked(isp::ControlSnapshot *controls,
                                    const isp::IspControlCapabilities &capabilities) const {
    using namespace isp;
    *controls = {};
    if (!capabilities.readback)
        return result(ControlStatus::Unsupported, "read_controls",
                      "Control readback capability is unavailable");
    ControlSnapshot candidate;
    auto read = call([&] { return backend_->read_controls(&candidate); });
    if (!read.ok())
        return decorate(std::move(read));
    std::string validation_error;
    if (!valid_controls(candidate, capabilities, &validation_error))
        return result(ControlStatus::BackendError, "read_controls", validation_error);
    const auto info = backend_info();
    if (info.name == "backend-info-error")
        return result(ControlStatus::BackendError, "read_controls", "Backend metadata query failed");
    candidate.hardware_confirmed = candidate.hardware_confirmed && info.real_hardware_backend &&
                                   info.evidence == EvidenceSource::LiveDevice;
    *controls = std::move(candidate);
    read.hardware_confirmed = controls->hardware_confirmed;
    return decorate(std::move(read));
}
isp::IspControlResult AiqController::read_controls(isp::ControlSnapshot *controls) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!controls)
        return result(isp::ControlStatus::InvalidArgument, "read_controls", "null controls");
    *controls = {};
    isp::IspControlCapabilities capabilities;
    auto checked = check_control("read_controls", &capabilities);
    if (!checked.ok())
        return checked;
    return read_controls_locked(controls, capabilities);
}
isp::IspControlResult AiqController::check_control(const char *op, isp::IspControlCapabilities *caps) const {
    using namespace isp;
    if (camera_config_.ownership != IspAiqOwnershipMode::InProcess)
        return result(ControlStatus::Unsupported, op,
                      "External service has no documented client control API");
    const auto snapshot_info = backend_info();
    if (!snapshot_info.sdk_available && snapshot_info.evidence != EvidenceSource::TestBackend)
        return result(ControlStatus::BackendUnavailable, op,
                      "BLOCKED_HARDWARE: no real RKAIQ SDK control backend");
    if (!running_ || faulted_)
        return result(ControlStatus::InvalidState, op, "Control requires a healthy running owned context");
    if (snapshot_info.real_hardware_backend && snapshot_info.evidence == EvidenceSource::LiveDevice) {
        const auto owner = discover_aiq_ownership();
        if (!owner.scan_complete || !owner.owners.empty())
            return result(ControlStatus::OwnershipConflict, op,
                          "External owner candidates appeared or current ownership cannot be confirmed");
    }
    auto r = call([&] { return backend_->query_capabilities(caps); });
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::set_ae_mode(isp::AeMode mode) const {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    IspControlCapabilities caps;
    if (mode != AeMode::Auto && mode != AeMode::Manual)
        return result(ControlStatus::InvalidArgument, "set_ae_mode", "Invalid AE mode");
    auto r = check_control("set_ae_mode", &caps);
    if (!r.ok())
        return r;
    if (!caps.ae_mode)
        return result(ControlStatus::Unsupported, "set_ae_mode", "AE mode capability unavailable");
    r = call([&] { return backend_->set_ae_mode(mode); });
    if (!r.ok())
        return decorate(std::move(r));
    r.request_accepted = true;
    r.hardware_confirmed = false;
    ControlSnapshot actual;
    if (caps.readback) {
        auto read = read_controls_locked(&actual, caps);
        if (!read.ok()) {
            r.status = read.status;
            r.error_code = read.error_code;
            r.message = "Request accepted, but readback failed: " + read.message;
            return decorate(std::move(r));
        }
        if (!actual.ae_mode || *actual.ae_mode != mode) {
            r.status = ControlStatus::ReadbackMismatch;
            r.message = "AE mode readback does not match accepted request";
        } else
            r.hardware_confirmed = actual.hardware_confirmed;
    } else
        r.message = "AE request accepted; readback unavailable, hardware effect NOT_MEASURED";
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::set_exposure_time_us(double value) const {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    IspControlCapabilities caps;
    if (!std::isfinite(value) || value <= 0)
        return result(ControlStatus::InvalidArgument, "set_exposure_us",
                      "Exposure must be finite and positive");
    auto r = check_control("set_exposure_us", &caps);
    if (!r.ok())
        return r;
    if (!caps.exposure_time || !std::isfinite(caps.exposure_us.minimum) ||
        !std::isfinite(caps.exposure_us.maximum) || !(caps.exposure_us.maximum >= caps.exposure_us.minimum) ||
        caps.exposure_us.maximum <= 0)
        return result(ControlStatus::Unsupported, "set_exposure_us",
                      "Exposure capability/range is unavailable");
    if (value < caps.exposure_us.minimum || value > caps.exposure_us.maximum)
        return result(ControlStatus::InvalidArgument, "set_exposure_us",
                      "Exposure is outside SDK-reported range");
    r = call([&] { return backend_->set_exposure_us(value); });
    if (!r.ok())
        return decorate(std::move(r));
    r.request_accepted = true;
    r.hardware_confirmed = false;
    ControlSnapshot actual;
    if (caps.readback) {
        auto read = read_controls_locked(&actual, caps);
        if (!read.ok()) {
            r.status = read.status;
            r.error_code = read.error_code;
            r.message = "Request accepted, but readback failed: " + read.message;
            return decorate(std::move(r));
        }
        if (!actual.exposure_us || !equal(*actual.exposure_us, value)) {
            r.status = ControlStatus::ReadbackMismatch;
            r.message = "Exposure readback does not match accepted request";
        } else
            r.hardware_confirmed = actual.hardware_confirmed;
    } else
        r.message = "Exposure request accepted; readback unavailable, hardware effect NOT_MEASURED";
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::set_analog_gain(double value) const {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    IspControlCapabilities caps;
    if (!std::isfinite(value) || value <= 0)
        return result(ControlStatus::InvalidArgument, "set_gain", "Gain must be finite and positive");
    auto r = check_control("set_gain", &caps);
    if (!r.ok())
        return r;
    if (!caps.analog_gain || !std::isfinite(caps.gain.minimum) || !std::isfinite(caps.gain.maximum) ||
        !(caps.gain.maximum >= caps.gain.minimum) || caps.gain.maximum <= 0)
        return result(ControlStatus::Unsupported, "set_gain", "Gain capability/range is unavailable");
    if (value < caps.gain.minimum || value > caps.gain.maximum)
        return result(ControlStatus::InvalidArgument, "set_gain", "Gain is outside SDK-reported range");
    r = call([&] { return backend_->set_gain(value); });
    if (!r.ok())
        return decorate(std::move(r));
    r.request_accepted = true;
    r.hardware_confirmed = false;
    ControlSnapshot actual;
    if (caps.readback) {
        auto read = read_controls_locked(&actual, caps);
        if (!read.ok()) {
            r.status = read.status;
            r.error_code = read.error_code;
            r.message = "Request accepted, but readback failed: " + read.message;
            return decorate(std::move(r));
        }
        if (!actual.analog_gain || !equal(*actual.analog_gain, value)) {
            r.status = ControlStatus::ReadbackMismatch;
            r.message = "Gain readback does not match accepted request";
        } else
            r.hardware_confirmed = actual.hardware_confirmed;
    } else
        r.message = "Gain request accepted; readback unavailable, hardware effect NOT_MEASURED";
    return decorate(std::move(r));
}
isp::IspControlResult AiqController::set_awb_mode(isp::AwbMode mode) const {
    using namespace isp;
    std::lock_guard<std::mutex> lock(mutex_);
    IspControlCapabilities caps;
    if (mode != AwbMode::Auto && mode != AwbMode::Manual)
        return result(ControlStatus::InvalidArgument, "set_awb_mode", "Invalid AWB mode");
    auto r = check_control("set_awb_mode", &caps);
    if (!r.ok())
        return r;
    if (!caps.awb_mode)
        return result(ControlStatus::Unsupported, "set_awb_mode", "AWB mode capability unavailable");
    r = call([&] { return backend_->set_awb_mode(mode); });
    if (!r.ok())
        return decorate(std::move(r));
    r.request_accepted = true;
    r.hardware_confirmed = false;
    ControlSnapshot actual;
    if (caps.readback) {
        auto read = read_controls_locked(&actual, caps);
        if (!read.ok()) {
            r.status = read.status;
            r.error_code = read.error_code;
            r.message = "Request accepted, but readback failed: " + read.message;
            return decorate(std::move(r));
        }
        if (!actual.awb_mode || *actual.awb_mode != mode) {
            r.status = ControlStatus::ReadbackMismatch;
            r.message = "AWB mode readback does not match accepted request";
        } else
            r.hardware_confirmed = actual.hardware_confirmed;
    } else
        r.message = "AWB request accepted; readback unavailable, hardware effect NOT_MEASURED";
    return decorate(std::move(r));
}
AiqResult AiqController::query_status(AiqStatus *status) const {
    if (!status)
        return AiqResult::InvalidArgument;
    isp::IspStatus modern;
    auto r = query(&modern);
    *status = {};
    status->camera_id = modern.camera_id;
    status->daemon_detected = !modern.owner.owners.empty();
    if (status->daemon_detected)
        status->owner = modern.owner.owners.front();
    status->vendor_runtime_detected = modern.backend.sdk_available;
    status->control_backend_available = modern.backend.real_hardware_backend &&
                                        (modern.capabilities.ae_mode || modern.capabilities.exposure_time ||
                                         modern.capabilities.analog_gain || modern.capabilities.awb_mode);
    status->iq_path = modern.iq.resolved_path;
    status->iq_match = isp::to_string(modern.iq.sensor_match);
    status->detail = isp::status_json(modern);
    return legacy(r, nullptr);
}
AiqResult AiqController::set_ae_auto(bool v, std::string *error) const {
    return legacy(set_ae_mode(v ? isp::AeMode::Auto : isp::AeMode::Manual), error);
}
AiqResult AiqController::set_exposure_us(uint32_t v, std::string *error) const {
    return legacy(set_exposure_time_us(v), error);
}
AiqResult AiqController::set_gain(double v, std::string *error) const {
    return legacy(set_analog_gain(v), error);
}
AiqResult AiqController::set_awb_auto(bool v, std::string *error) const {
    return legacy(set_awb_mode(v ? isp::AwbMode::Auto : isp::AwbMode::Manual), error);
}
AiqResult AiqController::dump_state(const std::string &path, std::string *error) const {
    if (path.empty()) {
        if (error)
            *error = "dump path is empty";
        return AiqResult::InvalidArgument;
    }
    isp::IspStatus snapshot;
    auto r = query(&snapshot);
    if (!r.ok())
        return legacy(r, error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << isp::status_json(snapshot) << '\n';
    out.flush();
    if (!out.good()) {
        if (error)
            *error = "Cannot write requested ISP state snapshot";
        return AiqResult::BackendError;
    }
    return AiqResult::Ok;
}
const char *AiqController::result_name(AiqResult r) {
    switch (r) {
    case AiqResult::Ok:
        return "OK";
    case AiqResult::InvalidArgument:
        return "INVALID_ARGUMENT";
    case AiqResult::NotFound:
        return "NOT_FOUND";
    case AiqResult::Unsupported:
        return "UNSUPPORTED";
    case AiqResult::BackendError:
        return "BACKEND_ERROR";
    }
    return "UNKNOWN";
}
