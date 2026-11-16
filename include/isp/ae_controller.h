#pragma once

#include <array>
#include <cstdint>

namespace isp {

struct AeConfig {
    float target_luma = 110.0F;
    float exposure_compensation = 0.0F; // EV stops
    float min_exposure_us = 100.0F;
    float max_exposure_us = 33333.0F;
    float min_analog_gain = 1.0F;
    float max_analog_gain = 16.0F;
    float min_digital_gain = 1.0F;
    float max_digital_gain = 8.0F;
    float proportional_gain = 0.35F;
    float smoothing = 0.25F;
    float convergence_tolerance = 0.06F;
    uint32_t convergence_frames = 4;
    uint32_t anti_flicker_hz = 0; // 0, 50 or 60
};

struct AeStatistics {
    float mean_luma = 0.0F;
    float median_luma = 0.0F;
    float highlight_ratio = 0.0F;
    float shadow_ratio = 0.0F;
    std::array<uint32_t, 256> histogram{};
};

struct ExposureControl {
    float exposure_us = 10000.0F;
    float analog_gain = 1.0F;
    float digital_gain = 1.0F;
};

struct AeResult {
    ExposureControl control;
    float target_luma = 110.0F;
    float error = 0.0F;
    float measured_luma = 0.0F;
    bool converged = false;
    uint32_t stable_frames = 0;
};

class AeController {
public:
    explicit AeController(AeConfig config = {});
    void set_config(const AeConfig &config);
    const AeConfig &config() const { return config_; }
    void reset(ExposureControl initial = {});
    AeResult update(const AeStatistics &stats);
    const ExposureControl &control() const { return control_; }
    AeResult last_result() const { return last_; }

private:
    AeConfig config_;
    ExposureControl control_;
    AeResult last_;
    uint32_t stable_frames_ = 0;
    float filtered_luma_ = 0.0F;
    bool have_luma_ = false;
    float quantize_flicker(float exposure_us) const;
    void clamp_control();
};

} // namespace isp
