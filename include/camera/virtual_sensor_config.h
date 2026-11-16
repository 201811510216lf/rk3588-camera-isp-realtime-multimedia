#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace camera {

enum class BayerPattern : uint8_t { RGGB, GRBG, GBRG, BGGR };
enum class VirtualScene : uint8_t {
    GrayChart,
    ColorChecker,
    Checkerboard,
    SlantedEdge,
    Dark,
    Bright,
    HDR,
    Motion,
};

enum class RawFormat : uint8_t { RAW10 };

struct VirtualSensorConfig {
    std::string name = "IMX415_SIM";
    uint32_t width = 3840;
    uint32_t height = 2160;
    uint32_t fps = 30;
    RawFormat raw_format = RawFormat::RAW10;
    BayerPattern bayer = BayerPattern::RGGB;
    uint32_t mipi_lanes = 4;
    bool virtual_i2c = true;
    bool virtual_sensor_controls = true;

    VirtualScene scene = VirtualScene::ColorChecker;
    float scene_brightness = 0.5F;   // linear scene level, [0, 1]
    float scene_saturation = 1.0F;   // [0, 2]
    // Simulated illuminant multipliers; these are scene inputs, not sensor
    // white-balance registers. Typical values model daylight/tungsten/cool LED.
    float illuminant_r = 1.0F;
    float illuminant_g = 1.0F;
    float illuminant_b = 1.0F;
    float noise_sigma = 2.0F;       // RAW code values
    float dark_current = 1.0F;      // RAW code values
    float vignetting = 0.15F;       // edge attenuation, [0, 1]
    float bad_pixel_rate = 0.0F;    // deterministic hot/dead pixel rate
    float motion_speed = 0.02F;     // normalized scene shift/frame
    bool realtime_pacing = false;
    size_t stride = 0;              // packed RAW10 row stride; zero = minimum
    uint32_t seed = 0x41574B31U;

    bool validate(std::string *error = nullptr) const;
};

const char *to_string(BayerPattern pattern);
const char *to_string(VirtualScene scene);

}  // namespace camera
