#pragma once

#include "isp/isp_pipeline.h"
#include "isp/ae_controller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace isp {

struct IspProfile {
    uint32_t version = 1;
    std::string sensor_profile = "IMX415_SIM";
    IspConfig config;
    AeConfig ae;
};

// Lightweight dependency-free YAML subset loader. It intentionally accepts
// only scalar key/value pairs used by the software profile; Rockchip IQ files
// are a separate format and are never parsed as this profile.
class IspProfileStore {
public:
    static bool load(const std::string &path, IspProfile *profile, std::string *error = nullptr);
    static bool save(const std::string &path, const IspProfile &profile, std::string *error = nullptr);
    bool apply(const IspProfile &profile, SoftwareIsp *pipeline, std::string *error = nullptr);
    bool apply(const IspProfile &profile, SoftwareIsp *pipeline, AeController *ae, std::string *error = nullptr);
    bool rollback(SoftwareIsp *pipeline, std::string *error = nullptr);
    bool rollback(SoftwareIsp *pipeline, AeController *ae, std::string *error = nullptr);
    const IspProfile &active() const { return active_; }
    bool has_active() const { return have_active_; }

private:
    IspProfile active_;
    IspProfile previous_;
    bool have_active_ = false;
    bool have_previous_ = false;
};

} // namespace isp
