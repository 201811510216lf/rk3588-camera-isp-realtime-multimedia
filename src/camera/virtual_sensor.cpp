#include "camera/virtual_sensor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace camera {
namespace {

constexpr double kNominalExposureUs = 10000.0;
constexpr double kMinExposureUs = 100.0;
constexpr double kMaxExposureUs = 100000.0;
constexpr float kMinGain = 1.0F;
constexpr float kMaxAnalogGain = 16.0F;
constexpr float kMaxDigitalGain = 8.0F;

void set_error(std::string *error, const std::string &message) {
    if (error) *error = message;
}

float clamp01(float value) { return std::max(0.0F, std::min(1.0F, value)); }

float hash_noise(uint32_t seed, uint32_t x, uint32_t y, uint64_t sequence) {
    uint32_t v = seed ^ (x * 0x9E3779B9U) ^ (y * 0x85EBCA6BU) ^
                 static_cast<uint32_t>(sequence * 0xC2B2AE35U) ^
                 static_cast<uint32_t>(sequence >> 32U);
    v ^= v >> 16U;
    v *= 0x7FEB352DU;
    v ^= v >> 15U;
    v *= 0x846CA68BU;
    v ^= v >> 16U;
    return static_cast<float>(v) / 4294967295.0F;
}

}  // namespace

VirtualSensor::VirtualSensor(VirtualSensorConfig config) : config_(std::move(config)) {}

bool VirtualSensor::start(std::string *error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.validate(error)) return false;
    running_ = true;
    sequence_ = 0;
    last_capture_ = std::chrono::steady_clock::time_point{};
    return true;
}

void VirtualSensor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

bool VirtualSensor::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

VirtualSensorConfig VirtualSensor::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

VirtualSensorControls VirtualSensor::controls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return controls_;
}

bool VirtualSensor::validate_control(double value, double min, double max,
                                     const char *name, std::string *error) const {
    if (!std::isfinite(value) || value < min || value > max) {
        set_error(error, std::string(name) + " is outside the supported range");
        return false;
    }
    return true;
}

bool VirtualSensor::set_exposure_us(double exposure_us, std::string *error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validate_control(exposure_us, kMinExposureUs, kMaxExposureUs,
                          "exposure_us", error)) return false;
    controls_.exposure_us = exposure_us;
    return true;
}

bool VirtualSensor::set_analog_gain(float gain, std::string *error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validate_control(gain, kMinGain, kMaxAnalogGain, "analog_gain", error)) return false;
    controls_.analog_gain = gain;
    return true;
}

bool VirtualSensor::set_digital_gain(float gain, std::string *error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validate_control(gain, kMinGain, kMaxDigitalGain, "digital_gain", error)) return false;
    controls_.digital_gain = gain;
    return true;
}

bool VirtualSensor::set_scene(VirtualScene scene, std::string *error) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)error;
    config_.scene = scene;
    return true;
}

void VirtualSensor::reset_sequence() {
    std::lock_guard<std::mutex> lock(mutex_);
    sequence_ = 0;
}

uint8_t VirtualSensor::bayer_channel(BayerPattern pattern, uint32_t x, uint32_t y) {
    const bool even_x = (x & 1U) == 0U;
    const bool even_y = (y & 1U) == 0U;
    switch (pattern) {
        case BayerPattern::RGGB:
            return even_y ? (even_x ? 0U : 1U) : (even_x ? 1U : 2U);
        case BayerPattern::GRBG:
            return even_y ? (even_x ? 1U : 0U) : (even_x ? 2U : 1U);
        case BayerPattern::GBRG:
            return even_y ? (even_x ? 1U : 2U) : (even_x ? 0U : 1U);
        case BayerPattern::BGGR:
            return even_y ? (even_x ? 2U : 1U) : (even_x ? 1U : 0U);
    }
    return 1U;
}

VirtualSensor::Rgb VirtualSensor::scene_rgb(float x, float y, uint64_t sequence,
                                            const VirtualSensorConfig &config) const {
    const float shift = (config.scene == VirtualScene::Motion)
        ? std::fmod(static_cast<float>(sequence) * config.motion_speed, 1.0F) : 0.0F;
    x = std::fmod(x + shift + 1.0F, 1.0F);
    const float base = clamp01(config.scene_brightness);
    Rgb rgb{base, base, base};
    switch (config.scene) {
        case VirtualScene::GrayChart: {
            const int patch = std::min(7, static_cast<int>(x * 8.0F));
            const float level = (0.08F + 0.12F * static_cast<float>(patch)) * 1.25F;
            rgb = {level, level, level};
            break;
        }
        case VirtualScene::ColorChecker: {
            static constexpr float patches[6][3] = {
                {0.78F, 0.18F, 0.16F}, {0.17F, 0.52F, 0.20F},
                {0.18F, 0.25F, 0.74F}, {0.78F, 0.70F, 0.15F},
                {0.52F, 0.18F, 0.57F}, {0.16F, 0.65F, 0.61F},
            };
            const int index = std::min(5, static_cast<int>((x * 6.0F)));
            const float row_mod = 0.75F + 0.25F * std::floor(y * 4.0F) / 3.0F;
            rgb = {patches[index][0] * row_mod, patches[index][1] * row_mod,
                   patches[index][2] * row_mod};
            break;
        }
        case VirtualScene::Checkerboard: {
            const int cx = static_cast<int>(x * 16.0F);
            const int cy = static_cast<int>(y * 12.0F);
            const float level = ((cx + cy) & 1) ? 0.88F : 0.12F;
            rgb = {level, level, level};
            break;
        }
        case VirtualScene::SlantedEdge: {
            const float level = (x + 0.35F * y > 0.53F) ? 0.88F : 0.12F;
            rgb = {level, level, level};
            break;
        }
        case VirtualScene::Dark:
            rgb = {base * 0.15F, base * 0.13F, base * 0.18F};
            break;
        case VirtualScene::Bright:
            rgb = {std::max(base, 0.92F), std::max(base, 0.90F), std::max(base, 0.88F)};
            break;
        case VirtualScene::HDR: {
            const float level = (x < 0.5F) ? 0.08F : 0.95F;
            rgb = {level * 1.05F, level, level * 0.9F};
            break;
        }
        case VirtualScene::Motion:
            rgb = (x < 0.34F) ? Rgb{0.85F, 0.20F, 0.12F}
                              : Rgb{base * 0.55F, base * 0.55F, base * 0.55F};
            break;
    }
    // Keep the chart structure while making scene_brightness a real optical
    // input to every scene generator (the default brightness is 0.5).
    const float scene_scale = base / 0.5F;
    rgb.r *= scene_scale;
    rgb.g *= scene_scale;
    rgb.b *= scene_scale;
    rgb.r *= config.illuminant_r;
    rgb.g *= config.illuminant_g;
    rgb.b *= config.illuminant_b;
    const float luma = 0.299F * rgb.r + 0.587F * rgb.g + 0.114F * rgb.b;
    rgb.r = clamp01(luma + (rgb.r - luma) * config.scene_saturation);
    rgb.g = clamp01(luma + (rgb.g - luma) * config.scene_saturation);
    rgb.b = clamp01(luma + (rgb.b - luma) * config.scene_saturation);
    return rgb;
}

float VirtualSensor::noise(uint32_t x, uint32_t y, uint64_t sequence,
                           const VirtualSensorConfig &config) const {
    return (hash_noise(config.seed, x, y, sequence) - 0.5F) * 2.0F;
}

bool VirtualSensor::capture(Raw10Frame *frame, std::string *error) {
    if (!frame) {
        set_error(error, "capture requires a non-null frame");
        return false;
    }
    VirtualSensorConfig cfg;
    VirtualSensorControls controls;
    uint64_t sequence = 0;
    bool realtime = false;
    std::chrono::steady_clock::time_point previous;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            set_error(error, "virtual sensor is not running");
            return false;
        }
        cfg = config_;
        controls = controls_;
        sequence = sequence_++;
        realtime = cfg.realtime_pacing;
        previous = last_capture_;
    }
    if (realtime && previous != std::chrono::steady_clock::time_point{}) {
        const auto period = std::chrono::duration<double>(1.0 / static_cast<double>(cfg.fps));
        const auto deadline = previous + std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        const auto now = std::chrono::steady_clock::now();
        if (now < deadline) std::this_thread::sleep_until(deadline);
    }
    const auto capture_time = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_capture_ = capture_time;
        if (!running_) {
            set_error(error, "virtual sensor stopped during capture");
            return false;
        }
    }
    std::vector<uint16_t> pixels(static_cast<size_t>(cfg.width) * cfg.height);
    const float exposure = static_cast<float>(controls.exposure_us / kNominalExposureUs) *
                           controls.analog_gain * controls.digital_gain;
    for (uint32_t y = 0; y < cfg.height; ++y) {
        const float ny = (cfg.height > 1U) ? static_cast<float>(y) / (cfg.height - 1U) : 0.0F;
        for (uint32_t x = 0; x < cfg.width; ++x) {
            const float nx = (cfg.width > 1U) ? static_cast<float>(x) / (cfg.width - 1U) : 0.0F;
            const Rgb rgb = scene_rgb(nx, ny, sequence, cfg);
            const uint8_t channel = bayer_channel(cfg.bayer, x, y);
            const float value = channel == 0U ? rgb.r : (channel == 1U ? rgb.g : rgb.b);
            const float dx = nx - 0.5F;
            const float dy = ny - 0.5F;
            const float radius2 = std::min(1.0F, 2.0F * (dx * dx + dy * dy));
            const float shading = std::max(0.0F, 1.0F - cfg.vignetting * radius2);
            float code = value * exposure * shading * 1023.0F + cfg.dark_current;
            code += noise(x, y, sequence, cfg) * cfg.noise_sigma;
            if (cfg.bad_pixel_rate > 0.0F &&
                hash_noise(cfg.seed ^ 0xBAD0U, x, y, 0) < cfg.bad_pixel_rate) {
                code = ((x + y) & 1U) ? 1023.0F : 0.0F;
            }
            pixels[static_cast<size_t>(y) * cfg.width + x] =
                static_cast<uint16_t>(std::lround(std::max(0.0F, std::min(1023.0F, code))));
        }
    }
    frame->width = cfg.width;
    frame->height = cfg.height;
    frame->stride = cfg.stride;
    frame->bayer = cfg.bayer;
    frame->sequence = sequence;
    frame->timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(capture_time.time_since_epoch()).count());
    frame->controls = controls;
    if (!raw10::pack(pixels.data(), cfg.width, cfg.height, &frame->data,
                     cfg.stride, error)) return false;
    if (frame->stride == 0) frame->stride = raw10::row_bytes(cfg.width);
    return true;
}

}  // namespace camera
