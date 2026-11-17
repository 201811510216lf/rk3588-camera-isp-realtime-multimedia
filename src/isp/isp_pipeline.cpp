#include "isp/isp_pipeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <iterator>
#include <utility>

namespace isp {
namespace {

template <typename T>
T clamp_value(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

bool valid_dimensions(uint32_t width, uint32_t height) {
    return width > 0 && height > 0 && width <= 16384 && height <= 16384;
}

bool is_red(BayerPattern pattern, uint32_t x, uint32_t y) {
    const bool xe = (x & 1U) == 0U;
    const bool ye = (y & 1U) == 0U;
    switch (pattern) {
    case BayerPattern::RGGB: return ye && xe;
    case BayerPattern::GRBG: return ye && !xe;
    case BayerPattern::GBRG: return !ye && xe;
    case BayerPattern::BGGR: return !ye && !xe;
    }
    return false;
}

bool is_blue(BayerPattern pattern, uint32_t x, uint32_t y) {
    const bool xe = (x & 1U) == 0U;
    const bool ye = (y & 1U) == 0U;
    switch (pattern) {
    case BayerPattern::RGGB: return !ye && !xe;
    case BayerPattern::GRBG: return !ye && xe;
    case BayerPattern::GBRG: return ye && !xe;
    case BayerPattern::BGGR: return ye && xe;
    }
    return false;
}

int channel_index(BayerPattern pattern, uint32_t x, uint32_t y) {
    const bool xe = (x & 1U) == 0U;
    const bool ye = (y & 1U) == 0U;
    switch (pattern) {
    case BayerPattern::RGGB:
        return ye ? (xe ? 0 : 1) : (xe ? 2 : 3);
    case BayerPattern::GRBG:
        return ye ? (xe ? 1 : 0) : (xe ? 3 : 2);
    case BayerPattern::GBRG:
        return ye ? (xe ? 2 : 3) : (xe ? 0 : 1);
    case BayerPattern::BGGR:
        return ye ? (xe ? 3 : 2) : (xe ? 1 : 0);
    }
    return 1;
}

uint16_t raw_value(const std::vector<uint16_t> &raw, uint32_t width, int x, int y) {
    x = clamp_value(x, 0, static_cast<int>(width) - 1);
    y = clamp_value(y, 0, static_cast<int>(raw.size() / width) - 1);
    return raw[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
}

float channel_average(const RgbImage &image, int channel) {
    if (!image.valid()) return 0.0F;
    double sum = 0.0;
    const size_t count = static_cast<size_t>(image.width) * image.height;
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t *row = image.data.data() + static_cast<size_t>(y) * image.stride;
        for (uint32_t x = 0; x < image.width; ++x) sum += row[x * 3U + static_cast<size_t>(channel)];
    }
    return static_cast<float>(sum / static_cast<double>(count));
}

}  // namespace

bool pack_raw10(const uint16_t *pixels, size_t pixel_count, uint32_t width,
                uint32_t height, uint32_t stride, std::vector<uint8_t> *packed,
                std::string *error) {
    if (!pixels || !packed || !valid_dimensions(width, height)) {
        if (error) *error = "invalid RAW10 dimensions or output";
        return false;
    }
    const uint32_t row_bytes = ((width + 3U) / 4U) * 5U;
    if (stride < row_bytes || pixel_count < static_cast<size_t>(width) * height) {
        if (error) *error = "RAW10 stride or input pixel count is too small";
        return false;
    }
    packed->assign(static_cast<size_t>(stride) * height, 0U);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *dst = packed->data() + static_cast<size_t>(y) * stride;
        const uint16_t *src = pixels + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; x += 4U) {
            const uint32_t n = std::min(4U, width - x);
            uint8_t msb = 0;
            for (uint32_t i = 0; i < n; ++i) {
                const uint16_t value = src[x + i];
                if (value > 1023U) {
                    if (error) *error = "RAW10 pixel exceeds 10-bit range";
                    packed->clear();
                    return false;
                }
                dst[(x / 4U) * 5U + i] = static_cast<uint8_t>(value & 0xffU);
                msb |= static_cast<uint8_t>(((value >> 8U) & 0x03U) << (i * 2U));
            }
            dst[(x / 4U) * 5U + 4U] = msb;
        }
    }
    return true;
}

bool unpack_raw10(const uint8_t *packed, size_t packed_size, uint32_t width,
                  uint32_t height, uint32_t stride, std::vector<uint16_t> *pixels,
                  std::string *error) {
    if (!packed || !pixels || !valid_dimensions(width, height)) {
        if (error) *error = "invalid RAW10 dimensions or output";
        return false;
    }
    const uint32_t row_bytes = ((width + 3U) / 4U) * 5U;
    if (stride < row_bytes || packed_size < static_cast<size_t>(stride) * height) {
        if (error) *error = "RAW10 input is truncated or stride is too small";
        return false;
    }
    pixels->assign(static_cast<size_t>(width) * height, 0U);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *src = packed + static_cast<size_t>(y) * stride;
        uint16_t *dst = pixels->data() + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; x += 4U) {
            const uint8_t msb = src[(x / 4U) * 5U + 4U];
            const uint32_t n = std::min(4U, width - x);
            for (uint32_t i = 0; i < n; ++i) {
                dst[x + i] = static_cast<uint16_t>(src[(x / 4U) * 5U + i] |
                    (((msb >> (i * 2U)) & 0x03U) << 8U));
            }
        }
    }
    return true;
}

SoftwareIsp::SoftwareIsp(IspConfig config) : config_{} { set_config(config); }

void SoftwareIsp::set_config(const IspConfig &config) {
    config_ = config;
    bool invalid_ccm = false;
    for (const float value : config_.ccm) invalid_ccm = invalid_ccm || !std::isfinite(value) || std::abs(value) > 8.0F;
    if (invalid_ccm) {
        const float identity[9] = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
        std::copy(std::begin(identity), std::end(identity), std::begin(config_.ccm));
    }
    if (config_.gamma < 0.1F || !std::isfinite(config_.gamma)) config_.gamma = 1.0F;
    config_.awb_smoothing = clamp_value(config_.awb_smoothing, 0.0F, 1.0F);
    config_.denoise_strength = clamp_value(config_.denoise_strength, 0.0F, 1.0F);
    config_.temporal_strength = clamp_value(config_.temporal_strength, 0.0F, 1.0F);
    config_.sharpen_strength = clamp_value(config_.sharpen_strength, 0.0F, 1.0F);
    reset();
}

void SoftwareIsp::reset() {
    previous_rgb_ = {};
    last_awb_ = {};
}

void SoftwareIsp::apply_blc(std::vector<uint16_t> *raw) const {
    if (!config_.enable_blc || !raw || config_.width == 0) return;
    const uint16_t levels[4] = {config_.black_level_r, config_.black_level_gr,
                                config_.black_level_gb, config_.black_level_b};
    for (uint32_t y = 0; y < config_.height; ++y) {
        for (uint32_t x = 0; x < config_.width; ++x) {
            const uint16_t level = levels[channel_index(config_.bayer, x, y)];
            uint16_t &value = (*raw)[static_cast<size_t>(y) * config_.width + x];
            value = value > level ? static_cast<uint16_t>(value - level) : 0U;
        }
    }
}

void SoftwareIsp::apply_dpc(std::vector<uint16_t> *raw) const {
    if (!config_.enable_dpc || !raw || config_.width < 3 || config_.height < 3) return;
    std::unordered_set<uint32_t> defects(config_.defect_pixels.begin(), config_.defect_pixels.end());
    for (uint32_t y = 1; y + 1 < config_.height; ++y) {
        for (uint32_t x = 1; x + 1 < config_.width; ++x) {
            const uint32_t index = y * config_.width + x;
            const uint16_t value = (*raw)[index];
            const uint16_t n0 = raw_value(*raw, config_.width, x - 1, y);
            const uint16_t n1 = raw_value(*raw, config_.width, x + 1, y);
            const uint16_t n2 = raw_value(*raw, config_.width, x, y - 1);
            const uint16_t n3 = raw_value(*raw, config_.width, x, y + 1);
            const uint16_t lo = std::min(std::min(n0, n1), std::min(n2, n3));
            const uint16_t hi = std::max(std::max(n0, n1), std::max(n2, n3));
            if (defects.count(index) != 0U || value > static_cast<uint16_t>(hi + config_.dpc_threshold) ||
                (value + config_.dpc_threshold < lo)) {
                const uint16_t average = static_cast<uint16_t>((static_cast<uint32_t>(n0) + n1 + n2 + n3) / 4U);
                (*raw)[index] = average;
            }
        }
    }
}

void SoftwareIsp::apply_lsc(std::vector<uint16_t> *raw) const {
    if (!config_.enable_lsc || !raw || config_.width == 0 || config_.height == 0) return;
    const float cx = (config_.width - 1U) * 0.5F;
    const float cy = (config_.height - 1U) * 0.5F;
    const float max_r2 = cx * cx + cy * cy;
    for (uint32_t y = 0; y < config_.height; ++y) {
        for (uint32_t x = 0; x < config_.width; ++x) {
            const float dx = (static_cast<float>(x) - cx);
            const float dy = (static_cast<float>(y) - cy);
            const float r2 = (dx * dx + dy * dy) / std::max(1.0F, max_r2);
            const float gain = 1.0F + config_.lsc_edge_gain * clamp_value(r2, 0.0F, 1.0F);
            uint16_t &value = (*raw)[static_cast<size_t>(y) * config_.width + x];
            value = static_cast<uint16_t>(clamp_value(std::lround(value * gain), 0L, 1023L));
        }
    }
}

void SoftwareIsp::demosaic(const std::vector<uint16_t> &raw, RgbImage *rgb) const {
    rgb->width = config_.width;
    rgb->height = config_.height;
    rgb->stride = config_.width * 3U;
    rgb->data.assign(static_cast<size_t>(rgb->stride) * rgb->height, 0U);
    const auto average = [&](uint32_t x, uint32_t y, bool want_red, bool want_blue) {
        uint32_t sum = 0;
        uint32_t count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = static_cast<int>(x) + dx;
                const int ny = static_cast<int>(y) + dy;
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(config_.width) || ny >= static_cast<int>(config_.height)) continue;
                const bool red = is_red(config_.bayer, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                const bool blue = is_blue(config_.bayer, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                if ((want_red && red) || (want_blue && blue)) {
                    sum += raw[static_cast<size_t>(ny) * config_.width + static_cast<size_t>(nx)];
                    ++count;
                }
            }
        }
        return count ? static_cast<uint16_t>(sum / count) : 0U;
    };
    for (uint32_t y = 0; y < config_.height; ++y) {
        for (uint32_t x = 0; x < config_.width; ++x) {
            const uint16_t center = raw[static_cast<size_t>(y) * config_.width + x];
            uint16_t r = is_red(config_.bayer, x, y) ? center : average(x, y, true, false);
            uint16_t b = is_blue(config_.bayer, x, y) ? center : average(x, y, false, true);
            uint16_t g = center;
            if (!is_red(config_.bayer, x, y) && !is_blue(config_.bayer, x, y)) {
                g = center;
            } else {
                if (config_.demosaic_mode == DemosaicMode::EdgeAware) {
                    const uint16_t left = raw_value(raw, config_.width, static_cast<int>(x) - 1, y);
                    const uint16_t right = raw_value(raw, config_.width, static_cast<int>(x) + 1, y);
                    const uint16_t up = raw_value(raw, config_.width, x, static_cast<int>(y) - 1);
                    const uint16_t down = raw_value(raw, config_.width, x, static_cast<int>(y) + 1);
                    const uint32_t horizontal = static_cast<uint32_t>(std::abs(static_cast<int>(left) - static_cast<int>(right)));
                    const uint32_t vertical = static_cast<uint32_t>(std::abs(static_cast<int>(up) - static_cast<int>(down)));
                    if (horizontal < vertical) g = static_cast<uint16_t>((left + right) / 2U);
                    else if (vertical < horizontal) g = static_cast<uint16_t>((up + down) / 2U);
                    else g = static_cast<uint16_t>((left + right + up + down) / 4U);
                } else {
                    uint32_t sum = 0, count = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(config_.width) || ny >= static_cast<int>(config_.height)) continue;
                        const bool green = !is_red(config_.bayer, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)) && !is_blue(config_.bayer, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                        if (green) { sum += raw[static_cast<size_t>(ny) * config_.width + static_cast<size_t>(nx)]; ++count; }
                    }
                    g = count ? static_cast<uint16_t>(sum / count) : center;
                }
            }
            uint8_t *dst = rgb->data.data() + static_cast<size_t>(y) * rgb->stride + x * 3U;
            dst[0] = static_cast<uint8_t>(clamp_value((static_cast<uint32_t>(r) * 255U + 511U) / 1023U, 0U, 255U));
            dst[1] = static_cast<uint8_t>(clamp_value((static_cast<uint32_t>(g) * 255U + 511U) / 1023U, 0U, 255U));
            dst[2] = static_cast<uint8_t>(clamp_value((static_cast<uint32_t>(b) * 255U + 511U) / 1023U, 0U, 255U));
        }
    }
}

AwbResult SoftwareIsp::calculate_awb(const RgbImage &rgb) const {
    AwbResult result;
    if (!rgb.valid() || config_.awb_algorithm == AwbAlgorithm::Manual) {
        result.red_gain = config_.awb_manual_r; result.green_gain = config_.awb_manual_g; result.blue_gain = config_.awb_manual_b;
        result.confidence = 1.0F; result.converged = true; return result;
    }
    const float r = channel_average(rgb, 0), g = channel_average(rgb, 1), b = channel_average(rgb, 2);
    const float eps = 1e-3F;
    if (config_.awb_algorithm == AwbAlgorithm::WhitePatch) {
        float max_r = 1.0F, max_g = 1.0F, max_b = 1.0F;
        for (uint32_t y = 0; y < rgb.height; ++y) for (uint32_t x = 0; x < rgb.width; ++x) {
            const uint8_t *p = rgb.data.data() + static_cast<size_t>(y) * rgb.stride + x * 3U;
            max_r = std::max(max_r, static_cast<float>(p[0])); max_g = std::max(max_g, static_cast<float>(p[1])); max_b = std::max(max_b, static_cast<float>(p[2]));
        }
        result.red_gain = max_g / std::max(eps, max_r); result.green_gain = 1.0F; result.blue_gain = max_g / std::max(eps, max_b);
    } else {
        const float mean = (r + g + b) / 3.0F;
        result.red_gain = mean / std::max(eps, r); result.green_gain = mean / std::max(eps, g); result.blue_gain = mean / std::max(eps, b);
    }
    result.red_gain = clamp_value(result.red_gain, 0.25F, 4.0F);
    result.green_gain = clamp_value(result.green_gain, 0.25F, 4.0F);
    result.blue_gain = clamp_value(result.blue_gain, 0.25F, 4.0F);
    const float spread = (std::max(r, std::max(g, b)) - std::min(r, std::min(g, b))) / std::max(eps, (r + g + b) / 3.0F);
    result.confidence = clamp_value(1.0F - spread, 0.0F, 1.0F);
    result.converged = result.confidence > 0.75F;
    return result;
}

void SoftwareIsp::apply_awb(RgbImage *rgb, const AwbResult &awb) const {
    if (!config_.enable_awb || !rgb) return;
    for (uint32_t y = 0; y < rgb->height; ++y) {
        uint8_t *row = rgb->data.data() + static_cast<size_t>(y) * rgb->stride;
        for (uint32_t x = 0; x < rgb->width; ++x) {
            uint8_t *p = row + x * 3U;
            p[0] = static_cast<uint8_t>(clamp_value(std::lround(p[0] * awb.red_gain), 0L, 255L));
            p[1] = static_cast<uint8_t>(clamp_value(std::lround(p[1] * awb.green_gain), 0L, 255L));
            p[2] = static_cast<uint8_t>(clamp_value(std::lround(p[2] * awb.blue_gain), 0L, 255L));
        }
    }
}

void SoftwareIsp::apply_ccm(RgbImage *rgb) const {
    if (!config_.enable_ccm || !rgb) return;
    for (uint32_t y = 0; y < rgb->height; ++y) { uint8_t *row = rgb->data.data() + static_cast<size_t>(y) * rgb->stride;
        for (uint32_t x = 0; x < rgb->width; ++x) { uint8_t *p = row + x * 3U; const float in[3] = {p[0] / 255.0F, p[1] / 255.0F, p[2] / 255.0F};
            for (int c = 0; c < 3; ++c) { float v = config_.ccm[c * 3] * in[0] + config_.ccm[c * 3 + 1] * in[1] + config_.ccm[c * 3 + 2] * in[2]; p[c] = static_cast<uint8_t>(clamp_value(std::lround(v * 255.0F), 0L, 255L)); }
        }
    }
}

void SoftwareIsp::apply_gamma(RgbImage *rgb) const {
    if (!config_.enable_gamma || !rgb || config_.gamma <= 0.01F) return;
    uint8_t lut[256]; for (int i = 0; i < 256; ++i) lut[i] = static_cast<uint8_t>(clamp_value(std::lround(std::pow(i / 255.0F, 1.0F / config_.gamma) * 255.0F), 0L, 255L));
    for (size_t i = 0; i < rgb->data.size(); ++i) rgb->data[i] = lut[rgb->data[i]];
}

void SoftwareIsp::apply_denoise(RgbImage *rgb) const {
    if (!config_.enable_denoise || !rgb || config_.denoise_mode == DenoiseMode::Off || config_.width < 3 || config_.height < 3) return;
    RgbImage source = *rgb;
    const int radius = 1;
    const float strength = clamp_value(config_.denoise_strength, 0.0F, 1.0F);
    for (uint32_t y = 1; y + 1 < rgb->height; ++y) for (uint32_t x = 1; x + 1 < rgb->width; ++x) {
        uint8_t *dst = rgb->data.data() + static_cast<size_t>(y) * rgb->stride + x * 3U;
        for (int c = 0; c < 3; ++c) {
            int values[9]; int count = 0; float sum = 0.0F, wsum = 0.0F;
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) { const uint8_t *p = source.data.data() + static_cast<size_t>(y + dy) * source.stride + (x + dx) * 3U; values[count++] = p[c]; float d = static_cast<float>(p[c]) - dst[c]; float w = std::exp(-(d * d) / 400.0F); sum += p[c] * w; wsum += w; }
            float filtered = 0.0F;
            if (config_.denoise_mode == DenoiseMode::Median) { std::sort(values, values + count); filtered = static_cast<float>(values[count / 2]); }
            else if (config_.denoise_mode == DenoiseMode::Bilateral) filtered = sum / std::max(0.001F, wsum);
            else filtered = sum / 9.0F;
            dst[c] = static_cast<uint8_t>(clamp_value(std::lround(dst[c] * (1.0F - strength) + filtered * strength), 0L, 255L));
        }
    }
}

void SoftwareIsp::apply_temporal(RgbImage *rgb) const {
    if (!config_.enable_temporal_denoise || !rgb || !previous_rgb_.valid() || previous_rgb_.width != rgb->width || previous_rgb_.height != rgb->height) return;
    const float a = clamp_value(config_.temporal_strength, 0.0F, 1.0F);
    for (uint32_t y = 0; y < rgb->height; ++y) for (uint32_t x = 0; x < rgb->width; ++x) {
        uint8_t *p = rgb->data.data() + static_cast<size_t>(y) * rgb->stride + x * 3U; const uint8_t *q = previous_rgb_.data.data() + static_cast<size_t>(y) * previous_rgb_.stride + x * 3U;
        const int motion = std::abs(static_cast<int>(p[0]) - q[0]) + std::abs(static_cast<int>(p[1]) - q[1]) + std::abs(static_cast<int>(p[2]) - q[2]);
        const float w = motion > 45 ? 0.0F : a;
        for (int c = 0; c < 3; ++c) p[c] = static_cast<uint8_t>(std::lround(p[c] * (1.0F - w) + q[c] * w));
    }
}

void SoftwareIsp::apply_sharpen(RgbImage *rgb) const {
    if (!config_.enable_sharpen || !rgb || config_.sharpen_strength <= 0.0F || config_.width < 3 || config_.height < 3) return;
    RgbImage source = *rgb; const float s = config_.sharpen_strength;
    for (uint32_t y = 1; y + 1 < rgb->height; ++y) for (uint32_t x = 1; x + 1 < rgb->width; ++x) {
        uint8_t *p = rgb->data.data() + static_cast<size_t>(y) * rgb->stride + x * 3U;
        for (int c = 0; c < 3; ++c) { const auto at = [&](int xx, int yy) { return source.data[static_cast<size_t>(yy) * source.stride + static_cast<size_t>(xx) * 3U + c]; }; float lap = 4.0F * p[c] - at(x - 1, y) - at(x + 1, y) - at(x, y - 1) - at(x, y + 1); if (std::abs(lap) >= config_.sharpen_threshold) p[c] = static_cast<uint8_t>(clamp_value(std::lround(p[c] + s * lap), 0L, 255L)); }
    }
}

void SoftwareIsp::convert_nv12(const RgbImage &rgb, Nv12Image *nv12) const {
    nv12->width = rgb.width; nv12->height = rgb.height; nv12->stride_y = rgb.width; nv12->stride_uv = (rgb.width + 1U) & ~1U;
    nv12->y.assign(static_cast<size_t>(nv12->stride_y) * rgb.height, 0U); nv12->uv.assign(static_cast<size_t>(nv12->stride_uv) * ((rgb.height + 1U) / 2U), 128U);
    for (uint32_t y = 0; y < rgb.height; ++y) for (uint32_t x = 0; x < rgb.width; ++x) { const uint8_t *p = rgb.data.data() + static_cast<size_t>(y) * rgb.stride + x * 3U; float Y, U, V; if (config_.yuv_matrix == YuvMatrix::BT601) { Y = 0.299F*p[0] + 0.587F*p[1] + 0.114F*p[2]; U = -0.169F*p[0] - 0.331F*p[1] + 0.5F*p[2] + 128.0F; V = 0.5F*p[0] - 0.419F*p[1] - 0.081F*p[2] + 128.0F; } else { Y = 0.2126F*p[0] + 0.7152F*p[1] + 0.0722F*p[2]; U = -0.1146F*p[0] - 0.3854F*p[1] + 0.5F*p[2] + 128.0F; V = 0.5F*p[0] - 0.4542F*p[1] - 0.0458F*p[2] + 128.0F; } if (config_.yuv_limited_range) Y = 16.0F + Y * (219.0F / 255.0F); nv12->y[static_cast<size_t>(y) * nv12->stride_y + x] = static_cast<uint8_t>(clamp_value(std::lround(Y), 0L, 255L)); if ((y % 2U == 0U) && (x % 2U == 0U)) { const size_t uv = static_cast<size_t>(y / 2U) * nv12->stride_uv + x; nv12->uv[uv] = static_cast<uint8_t>(clamp_value(std::lround(U), 0L, 255L)); nv12->uv[uv + 1U] = static_cast<uint8_t>(clamp_value(std::lround(V), 0L, 255L)); } }
}

IspStatistics SoftwareIsp::calculate_stats(const RgbImage &rgb, const AwbResult &awb) const {
    IspStatistics stats; stats.awb = awb; if (!rgb.valid()) return stats; const size_t n = static_cast<size_t>(rgb.width) * rgb.height; std::vector<uint8_t> luma; luma.reserve(n); double sum = 0.0, sq = 0.0; uint64_t hi = 0, lo = 0;
    for (uint32_t y = 0; y < rgb.height; ++y) for (uint32_t x = 0; x < rgb.width; ++x) { const uint8_t *p = rgb.data.data() + static_cast<size_t>(y) * rgb.stride + x * 3U; const uint8_t lum = static_cast<uint8_t>(clamp_value(std::lround(0.2126*p[0] + 0.7152*p[1] + 0.0722*p[2]), 0L, 255L)); luma.push_back(lum); sum += lum; sq += lum * lum; hi += lum >= 250U; lo += lum <= 5U; }
    std::nth_element(luma.begin(), luma.begin() + luma.size()/2, luma.end()); stats.mean_luma = sum / n; stats.median_luma = luma[luma.size()/2]; stats.highlight_ratio = static_cast<double>(hi)/n; stats.shadow_ratio = static_cast<double>(lo)/n; stats.noise_variance = sq/n - stats.mean_luma*stats.mean_luma; double sharp = 0.0; double edge = 0.0; double r_mean = 0.0, g_mean = 0.0, b_mean = 0.0; for (uint32_t y = 1; y + 1 < rgb.height; ++y) for (uint32_t x = 1; x + 1 < rgb.width; ++x) { const auto lum = [&](uint32_t xx, uint32_t yy) { const uint8_t *p = rgb.data.data() + static_cast<size_t>(yy) * rgb.stride + xx * 3U; return 0.2126*p[0] + 0.7152*p[1] + 0.0722*p[2]; }; const double lap = 4.0*lum(x,y)-lum(x-1,y)-lum(x+1,y)-lum(x,y-1)-lum(x,y+1); sharp += lap*lap; edge += std::abs(lum(x+1,y)-lum(x-1,y)) + std::abs(lum(x,y+1)-lum(x,y-1)); }
    for (uint32_t y = 0; y < rgb.height; ++y) for (uint32_t x = 0; x < rgb.width; ++x) { const uint8_t *p = rgb.data.data() + static_cast<size_t>(y) * rgb.stride + x * 3U; r_mean += p[0]; g_mean += p[1]; b_mean += p[2]; }
    const double area = static_cast<double>(std::max<uint64_t>(1U, static_cast<uint64_t>(rgb.width-2U)*(rgb.height-2U))); stats.sharpness = sharp / area; stats.edge_strength = edge / area; stats.color_balance_error = (std::abs(r_mean - g_mean) + std::abs(b_mean - g_mean)) / std::max(1.0, static_cast<double>(n)); stats.pixel_count = n; return stats;
}

bool SoftwareIsp::process_raw(const std::vector<uint16_t> &raw, RgbImage *rgb, Nv12Image *nv12, IspStatistics *stats, std::string *error) {
    if (!rgb || !valid_dimensions(config_.width, config_.height) || raw.size() < static_cast<size_t>(config_.width) * config_.height) { if (error) *error = "invalid RAW frame or ISP dimensions"; return false; }
    std::vector<uint16_t> working(raw.begin(), raw.begin() + static_cast<size_t>(config_.width) * config_.height); apply_blc(&working); apply_dpc(&working); apply_lsc(&working); demosaic(working, rgb); AwbResult measured = calculate_awb(*rgb); if (last_awb_.confidence > 0.0F && config_.awb_smoothing > 0.0F) { const float s = clamp_value(config_.awb_smoothing, 0.0F, 1.0F); measured.red_gain = last_awb_.red_gain*(1.0F-s)+measured.red_gain*s; measured.green_gain = last_awb_.green_gain*(1.0F-s)+measured.green_gain*s; measured.blue_gain = last_awb_.blue_gain*(1.0F-s)+measured.blue_gain*s; } last_awb_ = measured; apply_awb(rgb, measured); apply_ccm(rgb); apply_gamma(rgb); apply_denoise(rgb); apply_temporal(rgb); apply_sharpen(rgb); if (nv12) convert_nv12(*rgb, nv12); if (stats) { *stats = calculate_stats(*rgb, measured); if (previous_rgb_.valid() && previous_rgb_.width == rgb->width && previous_rgb_.height == rgb->height) { double delta = 0.0; for (size_t i = 0; i < rgb->data.size(); ++i) delta += std::abs(static_cast<int>(rgb->data[i]) - static_cast<int>(previous_rgb_.data[i])); stats->temporal_stability = delta / std::max<size_t>(1, rgb->data.size()); } } previous_rgb_ = *rgb; return true;
}

bool SoftwareIsp::process_raw10(const uint8_t *packed, size_t packed_size, uint32_t stride, RgbImage *rgb, Nv12Image *nv12, IspStatistics *stats, std::string *error) {
    std::vector<uint16_t> raw; if (!unpack_raw10(packed, packed_size, config_.width, config_.height, stride, &raw, error)) return false; return process_raw(raw, rgb, nv12, stats, error);
}

}  // namespace isp
