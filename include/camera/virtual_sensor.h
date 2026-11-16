#pragma once

#include "camera/raw10.h"
#include "camera/virtual_sensor_config.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace camera {

struct VirtualSensorControls {
    double exposure_us = 10000.0;
    float analog_gain = 1.0F;
    float digital_gain = 1.0F;
};

struct Raw10Frame {
    uint32_t width = 0;
    uint32_t height = 0;
    size_t stride = 0;
    BayerPattern bayer = BayerPattern::RGGB;
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    VirtualSensorControls controls;
    std::vector<uint8_t> data;

    bool valid() const {
        return width != 0 && height != 0 && !data.empty() &&
               raw10::packed_size(width, height, stride) <= data.size();
    }
};

// Host-side, deterministic camera source. It models a Bayer RAW10 sensor and
// its controls; it is intentionally not an IMX415 register or driver model.
class VirtualSensor {
public:
    explicit VirtualSensor(VirtualSensorConfig config = {});
    VirtualSensor(const VirtualSensor &) = delete;
    VirtualSensor &operator=(const VirtualSensor &) = delete;

    bool start(std::string *error = nullptr);
    void stop();
    bool is_running() const;

    bool capture(Raw10Frame *frame, std::string *error = nullptr);

    VirtualSensorConfig config() const;
    VirtualSensorControls controls() const;
    bool set_exposure_us(double exposure_us, std::string *error = nullptr);
    bool set_analog_gain(float gain, std::string *error = nullptr);
    bool set_digital_gain(float gain, std::string *error = nullptr);
    bool set_scene(VirtualScene scene, std::string *error = nullptr);
    void reset_sequence();

private:
    struct Rgb {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
    };

    static uint8_t bayer_channel(BayerPattern pattern, uint32_t x, uint32_t y);
    Rgb scene_rgb(float x, float y, uint64_t sequence,
                  const VirtualSensorConfig &config) const;
    float noise(uint32_t x, uint32_t y, uint64_t sequence,
                const VirtualSensorConfig &config) const;
    bool validate_control(double value, double min, double max,
                          const char *name, std::string *error) const;

    mutable std::mutex mutex_;
    VirtualSensorConfig config_;
    VirtualSensorControls controls_;
    bool running_ = false;
    uint64_t sequence_ = 0;
    std::chrono::steady_clock::time_point last_capture_{};
};

}  // namespace camera
