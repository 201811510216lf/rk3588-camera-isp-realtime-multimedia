#include "isp/ae_controller.h"

#include <algorithm>
#include <cmath>

namespace isp {

AeController::AeController(AeConfig config) : config_(config) { reset(); }

void AeController::set_config(const AeConfig &config) {
    config_ = config;
    reset(control_);
}

void AeController::reset(ExposureControl initial) {
    control_ = initial;
    stable_frames_ = 0;
    filtered_luma_ = 0.0F;
    have_luma_ = false;
    last_ = {};
    clamp_control();
}

float AeController::quantize_flicker(float exposure_us) const {
    if (config_.anti_flicker_hz != 50 && config_.anti_flicker_hz != 60) return exposure_us;
    const float period_us = 1'000'000.0F / static_cast<float>(config_.anti_flicker_hz * 2U);
    if (period_us <= 0.0F) return exposure_us;
    return std::max(period_us, std::round(exposure_us / period_us) * period_us);
}

void AeController::clamp_control() {
    config_.min_exposure_us = std::max(1.0F, config_.min_exposure_us);
    config_.max_exposure_us = std::max(config_.min_exposure_us, config_.max_exposure_us);
    config_.min_analog_gain = std::max(1.0F, config_.min_analog_gain);
    config_.max_analog_gain = std::max(config_.min_analog_gain, config_.max_analog_gain);
    config_.min_digital_gain = std::max(1.0F, config_.min_digital_gain);
    config_.max_digital_gain = std::max(config_.min_digital_gain, config_.max_digital_gain);
    control_.exposure_us = std::clamp(quantize_flicker(control_.exposure_us), config_.min_exposure_us, config_.max_exposure_us);
    control_.analog_gain = std::clamp(control_.analog_gain, config_.min_analog_gain, config_.max_analog_gain);
    control_.digital_gain = std::clamp(control_.digital_gain, config_.min_digital_gain, config_.max_digital_gain);
}

AeResult AeController::update(const AeStatistics &stats) {
    const float measured = stats.median_luma > 0.0F ? stats.median_luma : stats.mean_luma;
    if (!have_luma_) { filtered_luma_ = measured; have_luma_ = true; }
    else filtered_luma_ += config_.smoothing * (measured - filtered_luma_);
    const float target = std::clamp(config_.target_luma * std::pow(2.0F, config_.exposure_compensation), 1.0F, 254.0F);
    const float normalized_error = target > 0.0F ? (target - filtered_luma_) / target : 0.0F;
    const bool clipped = stats.highlight_ratio > 0.02F;
    // Use exposure time first. Once the shutter reaches either limit, spill into
    // analogue and then digital gain. This keeps motion blur bounded in normal use.
    float factor = std::exp(config_.proportional_gain * normalized_error);
    if (clipped) factor = std::min(factor, 0.8F);
    control_.exposure_us = quantize_flicker(control_.exposure_us * factor);
    clamp_control();
    if (control_.exposure_us >= config_.max_exposure_us * 0.999F ||
        control_.exposure_us <= config_.min_exposure_us * 1.001F ||
        std::abs(factor - 1.0F) > 0.02F) {
        const float residual = factor * (config_.max_exposure_us > 0.0F ? 1.0F : 1.0F);
        if (residual > 1.0F && control_.exposure_us >= config_.max_exposure_us * 0.999F)
            control_.analog_gain *= residual;
        else if (residual < 1.0F && control_.exposure_us <= config_.min_exposure_us * 1.001F)
            control_.analog_gain *= residual;
        clamp_control();
    }
    if (std::abs(normalized_error) <= config_.convergence_tolerance && !clipped) ++stable_frames_;
    else stable_frames_ = 0;
    last_.control = control_;
    last_.target_luma = target;
    last_.measured_luma = filtered_luma_;
    last_.error = target - filtered_luma_;
    last_.stable_frames = stable_frames_;
    last_.converged = stable_frames_ >= config_.convergence_frames;
    return last_;
}

} // namespace isp
