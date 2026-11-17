#include "camera/virtual_sensor_config.h"

namespace camera {

namespace {
void set_error(std::string *error, const char *message) {
    if (error) *error = message;
}
}  // namespace

bool VirtualSensorConfig::validate(std::string *error) const {
    if (width == 0 || height == 0) {
        set_error(error, "virtual sensor width and height must be non-zero");
        return false;
    }
    if (static_cast<size_t>(width) > static_cast<size_t>(-1) /
                                    static_cast<size_t>(height)) {
        set_error(error, "virtual sensor dimensions overflow the host address space");
        return false;
    }
    if (fps == 0 || fps > 240) {
        set_error(error, "virtual sensor fps must be in 1..240");
        return false;
    }
    if (mipi_lanes == 0 || mipi_lanes > 8) {
        set_error(error, "virtual sensor mipi_lanes must be in 1..8");
        return false;
    }
    if (scene_brightness < 0.0F || scene_brightness > 1.0F) {
        set_error(error, "scene_brightness must be in [0,1]");
        return false;
    }
    if (scene_saturation < 0.0F || scene_saturation > 2.0F) {
        set_error(error, "scene_saturation must be in [0,2]");
        return false;
    }
    if (illuminant_r <= 0.0F || illuminant_g <= 0.0F || illuminant_b <= 0.0F ||
        illuminant_r > 4.0F || illuminant_g > 4.0F || illuminant_b > 4.0F) {
        set_error(error, "illuminant multipliers must be in (0,4]");
        return false;
    }
    if (noise_sigma < 0.0F || dark_current < 0.0F) {
        set_error(error, "noise_sigma and dark_current must be non-negative");
        return false;
    }
    if (vignetting < 0.0F || vignetting >= 1.0F) {
        set_error(error, "vignetting must be in [0,1)");
        return false;
    }
    if (bad_pixel_rate < 0.0F || bad_pixel_rate > 1.0F) {
        set_error(error, "bad_pixel_rate must be in [0,1]");
        return false;
    }
    if (stride != 0) {
        const size_t minimum = (static_cast<size_t>(width) + 3U) / 4U * 5U;
        if (stride < minimum) {
            set_error(error, "RAW10 stride is smaller than the packed row size");
            return false;
        }
    }
    return true;
}

const char *to_string(BayerPattern pattern) {
    switch (pattern) {
        case BayerPattern::RGGB: return "RGGB";
        case BayerPattern::GRBG: return "GRBG";
        case BayerPattern::GBRG: return "GBRG";
        case BayerPattern::BGGR: return "BGGR";
    }
    return "UNKNOWN";
}

const char *to_string(VirtualScene scene) {
    switch (scene) {
        case VirtualScene::GrayChart: return "gray_chart";
        case VirtualScene::ColorChecker: return "color_checker";
        case VirtualScene::Checkerboard: return "checkerboard";
        case VirtualScene::SlantedEdge: return "slanted_edge";
        case VirtualScene::Dark: return "dark";
        case VirtualScene::Bright: return "bright";
        case VirtualScene::HDR: return "hdr";
        case VirtualScene::Motion: return "motion";
    }
    return "unknown";
}

}  // namespace camera

