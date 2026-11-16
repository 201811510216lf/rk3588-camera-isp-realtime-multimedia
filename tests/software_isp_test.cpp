#include "isp/isp_pipeline.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void test_raw10_round_trip() {
    const uint32_t width = 7, height = 3, stride = 12;
    std::vector<uint16_t> input(width * height);
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint16_t>((i * 137U) & 1023U);
    std::vector<uint8_t> packed;
    assert(isp::pack_raw10(input.data(), input.size(), width, height, stride, &packed));
    std::vector<uint16_t> output;
    assert(isp::unpack_raw10(packed.data(), packed.size(), width, height, stride, &output));
    assert(input == output);
    std::string error;
    assert(!isp::unpack_raw10(packed.data(), packed.size() - 1U, width, height, stride, &output, &error));
}

void test_pipeline_and_nv12() {
    isp::IspConfig config;
    config.width = 8;
    config.height = 8;
    config.enable_lsc = false;
    config.enable_denoise = true;
    config.denoise_mode = isp::DenoiseMode::Median;
    config.enable_sharpen = true;
    isp::SoftwareIsp pipeline(config);
    std::vector<uint16_t> raw(config.width * config.height, 512U);
    for (uint32_t y = 0; y < config.height; ++y) for (uint32_t x = 0; x < config.width; ++x) {
        if ((x + y) % 2U == 0U) raw[y * config.width + x] = 700U;
    }
    isp::RgbImage rgb;
    isp::Nv12Image nv12;
    isp::IspStatistics stats;
    std::string error;
    assert(pipeline.process_raw(raw, &rgb, &nv12, &stats, &error));
    assert(rgb.valid());
    assert(nv12.valid());
    assert(stats.pixel_count == config.width * config.height);
    assert(stats.mean_luma > 0.0);
    assert(stats.edge_strength >= 0.0 && stats.color_balance_error >= 0.0);
}

void test_all_bayer_patterns() {
    for (const auto pattern : {isp::BayerPattern::RGGB, isp::BayerPattern::GRBG,
                               isp::BayerPattern::GBRG, isp::BayerPattern::BGGR}) {
        isp::IspConfig config;
        config.width = 4; config.height = 4; config.bayer = pattern;
        config.enable_blc = false; config.enable_lsc = false; config.enable_awb = false;
        isp::SoftwareIsp pipeline(config);
        std::vector<uint16_t> raw(config.width * config.height, 128U);
        isp::RgbImage rgb;
        assert(pipeline.process_raw(raw, &rgb));
        assert(rgb.valid());
    }
}

void test_controls_change_output() {
    isp::IspConfig base;
    base.width = 8; base.height = 8; base.enable_blc = false; base.enable_lsc = false; base.enable_awb = false;
    std::vector<uint16_t> raw(base.width * base.height, 256U);
    isp::RgbImage linear, gamma;
    isp::SoftwareIsp first(base); assert(first.process_raw(raw, &linear));
    base.gamma = 2.2F; isp::SoftwareIsp second(base); assert(second.process_raw(raw, &gamma));
    assert(linear.data != gamma.data);
    base.enable_gamma = false; base.awb_algorithm = isp::AwbAlgorithm::Manual; base.enable_awb = true; base.awb_manual_r = 2.0F;
    isp::SoftwareIsp manual(base); isp::RgbImage wb; assert(manual.process_raw(raw, &wb)); assert(wb.data[0] >= linear.data[0]);
}

}  // namespace

int main() {
    test_raw10_round_trip();
    test_pipeline_and_nv12();
    test_all_bayer_patterns();
    test_controls_change_output();
    return 0;
}
