#include "isp/ae_controller.h"
#include "isp/isp_profile.h"

#include <cassert>
#include <cstdio>
#include <fstream>

int main() {
    isp::AeController ae;
    isp::AeStatistics dark; dark.mean_luma = 20.0F; dark.median_luma = 20.0F;
    const auto before = ae.control(); const auto after = ae.update(dark);
    assert(after.control.exposure_us >= before.exposure_us);
    isp::AeStatistics bright; bright.mean_luma = 245.0F; bright.median_luma = 245.0F; bright.highlight_ratio = 0.2F;
    const auto clipped = ae.update(bright); assert(clipped.control.exposure_us <= after.control.exposure_us || clipped.control.analog_gain <= after.control.analog_gain);

    const char *path = "round6_test_profile.yaml";
    { std::ofstream out(path); out << "version: 2\nsensor_profile: IMX415_SIM\ninput:\n  width: 8\n  height: 8\n  bayer: RGGB\nblc:\n  enabled: true\n  r: 32\nawb:\n  algorithm: white_patch\ndenoise:\n  spatial_strength: 0.5\n"; }
    isp::IspProfile profile; std::string error; assert(isp::IspProfileStore::load(path, &profile, &error)); assert(profile.version == 2 && profile.config.width == 8);
    isp::SoftwareIsp pipeline(profile.config); isp::IspProfileStore store; assert(store.apply(profile, &pipeline, &error)); auto changed = profile; changed.version = 3; changed.config.gamma = 1.8F; assert(store.apply(changed, &pipeline, &error)); assert(store.rollback(&pipeline, &error)); assert(store.active().version == 2); std::remove(path);
    return 0;
}
