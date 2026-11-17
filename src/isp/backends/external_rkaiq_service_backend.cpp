#include "isp/rkaiq_backend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <utility>

namespace isp {
namespace {
IspControlResult result(ControlStatus status, const char *op, IspCameraId id, std::string message) {
    IspControlResult r;
    r.status = status;
    r.operation = op;
    r.camera_id = id;
    r.message = std::move(message);
    r.backend_name = "external-rkaiq-service";
    return r;
}
bool candidate(std::string name, const std::string &hint) {
    for (char &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string lower_hint = hint;
    for (char &c : lower_hint)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char *token : {"rkaiq", "rkisp", "ispserver", "camera_engine"})
        if (name.find(token) != std::string::npos)
            return true;
    return !lower_hint.empty() && name.find(lower_hint) != std::string::npos;
}
class ExternalBackend final : public IRkAiqBackend {
  public:
    ExternalBackend(AiqCameraConfig c, OwnershipEvidence o) : config_(std::move(c)), owner_(std::move(o)) {}
    BackendInfo info() const override {
        BackendInfo b;
        b.name = "external-rkaiq-service";
        b.evidence = owner_.source;
        return b;
    }
    IspControlResult initialize(const AiqCameraConfig &) override { return unsupported("initialize"); }
    IspControlResult prepare() override { return unsupported("prepare"); }
    IspControlResult start() override { return unsupported("start"); }
    IspControlResult stop() override { return unsupported("stop"); }
    IspControlResult deinitialize() override { return unsupported("deinitialize"); }
    IspControlResult query_capabilities(IspControlCapabilities *c) const override {
        if (!c)
            return result(ControlStatus::InvalidArgument, "query_capabilities", config_.camera_id,
                          "null capabilities");
        *c = {};
        return result(ControlStatus::Ok, "query_capabilities", config_.camera_id,
                      "No documented service IPC control API is installed");
    }
    IspControlResult query_status(IspStatus *s) const override {
        if (!s)
            return result(ControlStatus::InvalidArgument, "query_status", config_.camera_id, "null status");
        const auto current_owner =
            owner_.source == EvidenceSource::LiveDevice ? discover_aiq_ownership() : owner_;
        *s = {};
        s->camera_id = config_.camera_id;
        s->ownership = IspAiqOwnershipMode::ExternalService;
        s->backend = info();
        s->owner = current_owner;
        s->sensor.entity = config_.sensor_entity;
        s->sensor.sensor_name = config_.expected_sensor_name;
        s->state = !current_owner.owners.empty() ? IspRuntimeState::Discovered
                                                 : (current_owner.scan_complete ? IspRuntimeState::Unavailable
                                                                                : IspRuntimeState::Unknown);
        s->message =
            "Process/service discovery does not prove per-camera ownership, IQ loading or ISP readiness";
        if (!config_.iq_file.empty()) {
            const auto checked = validate_iq_file(config_, &s->iq);
            if (!checked.ok()) {
                s->state = IspRuntimeState::ConfigMismatch;
                s->message = checked.message;
            }
        }
        return result(ControlStatus::Ok, "query_status", config_.camera_id, s->message);
    }
    IspControlResult read_controls(ControlSnapshot *c) const override {
        if (c)
            *c = {};
        return unsupported("read_controls");
    }
    IspControlResult validate_iq(const AiqCameraConfig &c, IqValidation *v) const override {
        auto r = validate_iq_file(c, v);
        if (!r.ok())
            return r;
        return unsupported("validate_iq_sensor_and_sdk");
    }
    IspControlResult set_ae_mode(AeMode) override { return unsupported("set_ae_mode"); }
    IspControlResult set_exposure_us(double) override { return unsupported("set_exposure_us"); }
    IspControlResult set_gain(double) override { return unsupported("set_gain"); }
    IspControlResult set_awb_mode(AwbMode) override { return unsupported("set_awb_mode"); }

  private:
    IspControlResult unsupported(const char *op) const {
        return result(ControlStatus::Unsupported, op, config_.camera_id,
                      "EXTERNAL_SERVICE is discovery-only; no legal client IPC/API, no "
                      "init/start/stop/control call was made");
    }
    AiqCameraConfig config_;
    OwnershipEvidence owner_;
};
} // namespace
OwnershipEvidence discover_aiq_ownership(const std::string &hint) {
    OwnershipEvidence result;
#ifdef __linux__
    namespace fs = std::filesystem;
    result.source = EvidenceSource::LiveDevice;
    result.scan_complete = true;
    std::error_code ec;
    fs::directory_iterator entries("/proc", fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        result.scan_complete = false;
        result.detail = "Cannot enumerate /proc; absence of an owner is unconfirmed";
        return result;
    }
    for (; entries != fs::directory_iterator(); entries.increment(ec)) {
        if (ec) {
            result.scan_complete = false;
            break;
        }
        const auto name = entries->path().filename().string();
        if (name.empty() ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;
        std::ifstream comm(entries->path() / "comm");
        std::string process;
        if (!std::getline(comm, process)) {
            // A process may disappear during a procfs scan; permission failures remain unknown.
            std::error_code exists_ec;
            if (fs::exists(entries->path(), exists_ec) || exists_ec)
                result.scan_complete = false;
            continue;
        }
        if (candidate(process, hint))
            result.owners.push_back(name + ":" + process);
    }
    result.detail = "Read-only process candidates; attribution to a specific ISP/camera is NOT_MEASURED";
#else
    (void)hint;
    result.detail = "Live AIQ ownership discovery is unavailable on this non-Linux host";
#endif
    return result;
}
IspControlResult validate_iq_file(const AiqCameraConfig &c, IqValidation *v) {
    namespace fs = std::filesystem;
    if (!v)
        return result(ControlStatus::InvalidArgument, "validate_iq", c.camera_id,
                      "null IQ validation output");
    *v = {};
    v->loaded = Confirmation::NotMeasured;
    if (c.sensor_entity.empty() || c.iq_root.empty() || c.iq_file.empty())
        return result(ControlStatus::ConfigMismatch, "validate_iq", c.camera_id,
                      "Explicit sensor entity, iq_root and iq_file are required");
    std::error_code ec;
    const auto root = fs::canonical(c.iq_root, ec);
    if (ec || !fs::is_directory(root, ec))
        return result(ControlStatus::NotFound, "validate_iq", c.camera_id,
                      "IQ root is not a readable directory");
    fs::path requested(c.iq_file);
    if (requested.is_relative())
        requested = root / requested;
    const auto file = fs::canonical(requested, ec);
    if (ec || !fs::is_regular_file(file, ec))
        return result(ControlStatus::NotFound, "validate_iq", c.camera_id,
                      "IQ file does not exist or is not a regular file");
    const auto contained = std::mismatch(root.begin(), root.end(), file.begin(), file.end());
    if (contained.first != root.end())
        return result(ControlStatus::ConfigMismatch, "validate_iq", c.camera_id,
                      "IQ file resolves outside iq_root");
    std::ifstream stream(file, std::ios::binary);
    char first;
    if (!stream.get(first))
        return result(ControlStatus::NotFound, "validate_iq", c.camera_id,
                      "IQ file cannot be read or is empty");
    v->resolved_path = file.string();
    v->file_readable = true;
    v->detail = "File checks passed; sensor metadata, SDK compatibility and actual IQ loading require the "
                "selected SDK/service";
    return result(ControlStatus::Ok, "validate_iq", c.camera_id, v->detail);
}
std::unique_ptr<IRkAiqBackend> create_external_service_backend(const AiqCameraConfig &c,
                                                               OwnershipEvidence o) {
    return std::make_unique<ExternalBackend>(c, std::move(o));
}
} // namespace isp
