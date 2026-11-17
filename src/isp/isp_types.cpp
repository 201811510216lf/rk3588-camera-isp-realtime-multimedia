#include "isp/isp_types.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace isp {
namespace {
std::string quote(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\')
            out << '\\' << static_cast<char>(c);
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c)
                << std::dec;
        else
            out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
const char *boolean(bool value) { return value ? "true" : "false"; }
void number(std::ostringstream &out, const std::optional<double> &value) {
    if (value && std::isfinite(*value))
        out << std::setprecision(17) << *value;
    else
        out << "null";
}
} // namespace
const char *to_string(ControlStatus v) {
    switch (v) {
    case ControlStatus::Ok:
        return "OK";
    case ControlStatus::InvalidArgument:
        return "INVALID_ARGUMENT";
    case ControlStatus::NotFound:
        return "NOT_FOUND";
    case ControlStatus::Unsupported:
        return "UNSUPPORTED";
    case ControlStatus::BackendUnavailable:
        return "BACKEND_UNAVAILABLE";
    case ControlStatus::OwnershipConflict:
        return "OWNERSHIP_CONFLICT";
    case ControlStatus::ConfigMismatch:
        return "CONFIG_MISMATCH";
    case ControlStatus::InvalidState:
        return "INVALID_STATE";
    case ControlStatus::BackendError:
        return "BACKEND_ERROR";
    case ControlStatus::ReadbackMismatch:
        return "READBACK_MISMATCH";
    }
    return "UNKNOWN";
}
const char *to_string(IspRuntimeState v) {
    switch (v) {
    case IspRuntimeState::Unknown:
        return "UNKNOWN";
    case IspRuntimeState::Discovered:
        return "DISCOVERED";
    case IspRuntimeState::Ready:
        return "READY";
    case IspRuntimeState::Running:
        return "RUNNING";
    case IspRuntimeState::Degraded:
        return "DEGRADED";
    case IspRuntimeState::Unavailable:
        return "UNAVAILABLE";
    case IspRuntimeState::OwnershipConflict:
        return "OWNERSHIP_CONFLICT";
    case IspRuntimeState::ConfigMismatch:
        return "CONFIG_MISMATCH";
    case IspRuntimeState::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}
const char *to_string(IspAiqOwnershipMode v) {
    switch (v) {
    case IspAiqOwnershipMode::ExternalService:
        return "EXTERNAL_SERVICE";
    case IspAiqOwnershipMode::InProcess:
        return "IN_PROCESS";
    case IspAiqOwnershipMode::Unavailable:
        return "UNAVAILABLE";
    }
    return "UNKNOWN";
}
const char *to_string(IspWorkingMode v) {
    switch (v) {
    case IspWorkingMode::Normal:
        return "NORMAL";
    case IspWorkingMode::Hdr2:
        return "HDR2";
    case IspWorkingMode::Hdr3:
        return "HDR3";
    }
    return "UNKNOWN";
}
const char *to_string(Confirmation v) {
    switch (v) {
    case Confirmation::NotMeasured:
        return "NOT_MEASURED";
    case Confirmation::Confirmed:
        return "CONFIRMED";
    case Confirmation::Mismatch:
        return "MISMATCH";
    case Confirmation::NotLoaded:
        return "NOT_LOADED";
    }
    return "UNKNOWN";
}
const char *to_string(EvidenceSource v) {
    switch (v) {
    case EvidenceSource::NotMeasured:
        return "NOT_MEASURED";
    case EvidenceSource::LiveDevice:
        return "LIVE_DEVICE";
    case EvidenceSource::OfflineFile:
        return "OFFLINE_FILE";
    case EvidenceSource::TestBackend:
        return "MOCK_OR_TEST";
    }
    return "UNKNOWN";
}
std::string result_json(const IspControlResult &v) {
    std::ostringstream out;
    out << "{\"status\":" << quote(to_string(v.status)) << ",\"operation\":" << quote(v.operation)
        << ",\"camera_id\":" << v.camera_id << ",\"error_code\":" << v.error_code
        << ",\"message\":" << quote(v.message) << ",\"backend_name\":" << quote(v.backend_name)
        << ",\"backend_version\":" << quote(v.backend_version)
        << ",\"request_accepted\":" << boolean(v.request_accepted)
        << ",\"hardware_confirmed\":" << boolean(v.hardware_confirmed) << '}';
    return out.str();
}
std::string status_json(const IspStatus &v) {
    std::ostringstream out;
    out << "{\"camera_id\":" << v.camera_id << ",\"state\":" << quote(to_string(v.state))
        << ",\"ownership\":" << quote(to_string(v.ownership)) << ",\"backend_name\":" << quote(v.backend.name)
        << ",\"backend_version\":" << quote(v.backend.version)
        << ",\"sdk_version\":" << quote(v.backend.sdk_version)
        << ",\"platform\":" << quote(v.backend.platform)
        << ",\"source\":" << quote(to_string(v.backend.evidence))
        << ",\"hardware_confirmed\":" << boolean(v.hardware_confirmed)
        << ",\"ownership_scan_complete\":" << boolean(v.owner.scan_complete) << ",\"owners\":[";
    bool first = true;
    for (const auto &owner : v.owner.owners) {
        if (!first)
            out << ',';
        first = false;
        out << quote(owner);
    }
    out << "],\"sensor_entity\":" << quote(v.sensor.entity)
        << ",\"sensor_name\":" << quote(v.sensor.sensor_name)
        << ",\"sensor_identity_confirmed\":" << boolean(v.sensor.confirmed)
        << ",\"iq_file\":" << quote(v.iq.resolved_path) << ",\"iq_readable\":" << boolean(v.iq.file_readable)
        << ",\"iq_sensor_match\":" << quote(to_string(v.iq.sensor_match))
        << ",\"iq_sdk_compatible\":" << quote(to_string(v.iq.sdk_compatible))
        << ",\"iq_loaded\":" << quote(to_string(v.iq.loaded))
        << ",\"capabilities\":{\"ae_mode\":" << boolean(v.capabilities.ae_mode)
        << ",\"exposure_time\":" << boolean(v.capabilities.exposure_time)
        << ",\"analog_gain\":" << boolean(v.capabilities.analog_gain)
        << ",\"awb_mode\":" << boolean(v.capabilities.awb_mode)
        << ",\"readback\":" << boolean(v.capabilities.readback) << "},\"controls\":{\"ae_mode\":"
        << (v.controls.ae_mode ? quote(*v.controls.ae_mode == AeMode::Auto ? "AUTO" : "MANUAL") : "null")
        << ",\"awb_mode\":"
        << (v.controls.awb_mode ? quote(*v.controls.awb_mode == AwbMode::Auto ? "AUTO" : "MANUAL") : "null")
        << ",\"exposure_us\":";
    number(out, v.controls.exposure_us);
    out << ",\"analog_gain\":";
    number(out, v.controls.analog_gain);
    out << ",\"hardware_confirmed\":" << boolean(v.controls.hardware_confirmed)
        << "},\"message\":" << quote(v.message) << '}';
    return out.str();
}
} // namespace isp
