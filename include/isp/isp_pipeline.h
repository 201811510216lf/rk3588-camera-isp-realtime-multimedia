#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace isp {

enum class BayerPattern { RGGB, GRBG, GBRG, BGGR };
enum class AwbAlgorithm { GrayWorld, WhitePatch, Manual };
enum class DemosaicMode { Bilinear, EdgeAware };
enum class DenoiseMode { Off, Gaussian, Median, Bilateral };
enum class YuvMatrix { BT601, BT709 };

struct IspConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    BayerPattern bayer = BayerPattern::RGGB;
    uint16_t black_level_r = 64;
    uint16_t black_level_gr = 64;
    uint16_t black_level_gb = 64;
    uint16_t black_level_b = 64;
    bool enable_blc = true;
    bool enable_dpc = true;
    bool enable_lsc = true;
    bool enable_awb = true;
    bool enable_ccm = true;
    bool enable_gamma = true;
    bool enable_denoise = false;
    bool enable_temporal_denoise = false;
    bool enable_sharpen = false;
    DemosaicMode demosaic_mode = DemosaicMode::Bilinear;
    float lsc_edge_gain = 1.25F;
    uint16_t dpc_threshold = 160;
    std::vector<uint32_t> defect_pixels;
    AwbAlgorithm awb_algorithm = AwbAlgorithm::GrayWorld;
    float awb_manual_r = 1.0F;
    float awb_manual_g = 1.0F;
    float awb_manual_b = 1.0F;
    float awb_smoothing = 0.25F;
    float ccm[9] = {1.0F, 0.0F, 0.0F,
                    0.0F, 1.0F, 0.0F,
                    0.0F, 0.0F, 1.0F};
    float gamma = 2.2F;
    DenoiseMode denoise_mode = DenoiseMode::Off;
    float denoise_strength = 0.35F;
    float temporal_strength = 0.3F;
    float sharpen_strength = 0.2F;
    float sharpen_threshold = 5.0F;
    YuvMatrix yuv_matrix = YuvMatrix::BT709;
    bool yuv_limited_range = false;
};

struct RgbImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    std::vector<uint8_t> data;  // interleaved RGB888
    bool valid() const { return width > 0 && height > 0 && stride >= width * 3 && data.size() >= static_cast<size_t>(stride) * height; }
};

struct Nv12Image {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_y = 0;
    uint32_t stride_uv = 0;
    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;
    bool valid() const { return width > 0 && height > 0 && stride_y >= width && stride_uv >= width && y.size() >= static_cast<size_t>(stride_y) * height && uv.size() >= static_cast<size_t>(stride_uv) * ((height + 1) / 2); }
};

struct AwbResult {
    float red_gain = 1.0F;
    float green_gain = 1.0F;
    float blue_gain = 1.0F;
    float confidence = 0.0F;
    bool converged = false;
};

struct IspStatistics {
    double mean_luma = 0.0;
    double median_luma = 0.0;
    double highlight_ratio = 0.0;
    double shadow_ratio = 0.0;
    double noise_variance = 0.0;
    double sharpness = 0.0;
    double edge_strength = 0.0;
    double temporal_stability = 0.0;
    double color_balance_error = 0.0;
    uint64_t pixel_count = 0;
    AwbResult awb;
};

// Standard four-pixel RAW10 packing: p0..p3 low bytes, then 2-bit MSBs
// packed in one byte as p0[9:8],p1[9:8],p2[9:8],p3[9:8].
bool pack_raw10(const uint16_t *pixels, size_t pixel_count, uint32_t width,
                uint32_t height, uint32_t stride, std::vector<uint8_t> *packed,
                std::string *error = nullptr);
bool unpack_raw10(const uint8_t *packed, size_t packed_size, uint32_t width,
                  uint32_t height, uint32_t stride, std::vector<uint16_t> *pixels,
                  std::string *error = nullptr);

class SoftwareIsp {
public:
    explicit SoftwareIsp(IspConfig config = {});

    void set_config(const IspConfig &config);
    const IspConfig &config() const { return config_; }
    void reset();

    bool process_raw(const std::vector<uint16_t> &raw, RgbImage *rgb,
                     Nv12Image *nv12 = nullptr, IspStatistics *stats = nullptr,
                     std::string *error = nullptr);
    bool process_raw10(const uint8_t *packed, size_t packed_size, uint32_t stride,
                       RgbImage *rgb, Nv12Image *nv12 = nullptr,
                       IspStatistics *stats = nullptr, std::string *error = nullptr);

    AwbResult last_awb() const { return last_awb_; }

private:
    IspConfig config_;
    AwbResult last_awb_;
    RgbImage previous_rgb_;

    void apply_blc(std::vector<uint16_t> *raw) const;
    void apply_dpc(std::vector<uint16_t> *raw) const;
    void apply_lsc(std::vector<uint16_t> *raw) const;
    void demosaic(const std::vector<uint16_t> &raw, RgbImage *rgb) const;
    AwbResult calculate_awb(const RgbImage &rgb) const;
    void apply_awb(RgbImage *rgb, const AwbResult &awb) const;
    void apply_ccm(RgbImage *rgb) const;
    void apply_gamma(RgbImage *rgb) const;
    void apply_denoise(RgbImage *rgb) const;
    void apply_temporal(RgbImage *rgb) const;
    void apply_sharpen(RgbImage *rgb) const;
    void convert_nv12(const RgbImage &rgb, Nv12Image *nv12) const;
    IspStatistics calculate_stats(const RgbImage &rgb, const AwbResult &awb) const;
};

}  // namespace isp
