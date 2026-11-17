#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace isp {
using IspCameraId = uint32_t;
enum class IspWorkingMode { Normal, Hdr2, Hdr3 };
enum class AiqStartOrder { Unspecified, BeforeStream, AfterStream };
enum class AiqStopOrder { Unspecified, BeforeStreamOff, AfterStreamOff };
enum class IspAiqOwnershipMode { ExternalService, InProcess, Unavailable };
enum class IspRuntimeState {
    Unknown,
    Discovered,
    Ready,
    Running,
    Degraded,
    Unavailable,
    OwnershipConflict,
    ConfigMismatch,
    Error
};
enum class EvidenceSource { NotMeasured, LiveDevice, OfflineFile, TestBackend };
enum class Confirmation { NotMeasured, Confirmed, Mismatch, NotLoaded };
enum class ControlStatus {
    Ok,
    InvalidArgument,
    NotFound,
    Unsupported,
    BackendUnavailable,
    OwnershipConflict,
    ConfigMismatch,
    InvalidState,
    BackendError,
    ReadbackMismatch
};
enum class AeMode { Auto, Manual };
enum class AwbMode { Auto, Manual };

struct IspControlResult {
    ControlStatus status = ControlStatus::BackendUnavailable;
    std::string operation;
    IspCameraId camera_id = 0;
    int error_code = 0;
    std::string message;
    std::string backend_name;
    std::string backend_version = "UNKNOWN";
    bool request_accepted = false;
    bool hardware_confirmed = false;
    bool ok() const { return status == ControlStatus::Ok; }
};
struct BackendInfo {
    std::string name;
    std::string version = "UNKNOWN";
    std::string sdk_version = "UNKNOWN";
    std::string platform = "UNKNOWN";
    bool sdk_available = false;
    bool real_hardware_backend = false;
    EvidenceSource evidence = EvidenceSource::NotMeasured;
};
struct SensorIdentity {
    std::string entity;
    std::string sensor_name;
    std::optional<int> physical_camera_id;
    EvidenceSource evidence = EvidenceSource::NotMeasured;
    bool confirmed = false;
};
struct NumericControlRange {
    double minimum = 0;
    double maximum = 0;
};
struct IspControlCapabilities {
    bool ae_mode = false;
    bool exposure_time = false;
    bool analog_gain = false;
    bool awb_mode = false;
    bool readback = false;
    bool hdr2 = false;
    bool hdr3 = false;
    NumericControlRange exposure_us;
    NumericControlRange gain;
};
struct ControlSnapshot {
    std::optional<AeMode> ae_mode;
    std::optional<double> exposure_us;
    std::optional<double> analog_gain;
    std::optional<AwbMode> awb_mode;
    bool hardware_confirmed = false;
};
struct OwnershipEvidence {
    bool scan_complete = false;
    std::vector<std::string> owners;
    EvidenceSource source = EvidenceSource::NotMeasured;
    std::string detail;
};
struct IqValidation {
    std::string resolved_path;
    bool file_readable = false;
    Confirmation sensor_match = Confirmation::NotMeasured;
    Confirmation sdk_compatible = Confirmation::NotMeasured;
    Confirmation loaded = Confirmation::NotMeasured;
    std::string detail;
};
struct IspStatus {
    IspCameraId camera_id = 0;
    IspRuntimeState state = IspRuntimeState::Unknown;
    IspAiqOwnershipMode ownership = IspAiqOwnershipMode::Unavailable;
    BackendInfo backend;
    SensorIdentity sensor;
    OwnershipEvidence owner;
    IqValidation iq;
    IspControlCapabilities capabilities;
    ControlSnapshot controls;
    bool hardware_confirmed = false;
    std::string message;
};
const char *to_string(ControlStatus value);
const char *to_string(IspRuntimeState value);
const char *to_string(IspAiqOwnershipMode value);
const char *to_string(IspWorkingMode value);
const char *to_string(Confirmation value);
const char *to_string(EvidenceSource value);
std::string result_json(const IspControlResult &value);
std::string status_json(const IspStatus &value);
} // namespace isp
