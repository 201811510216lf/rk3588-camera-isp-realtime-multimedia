#include "camera/raw10.h"
#include "camera/virtual_sensor.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void test_raw10_roundtrip() {
    constexpr uint32_t width = 7;
    constexpr uint32_t height = 3;
    constexpr size_t stride = 16;
    std::vector<uint16_t> input(width * height);
    const uint16_t values[] = {0, 1, 255, 256, 512, 1023, 77};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            input[y * width + x] = values[x];
        }
    }
    std::vector<uint8_t> packed;
    std::string error;
    assert(camera::raw10::pack(input.data(), width, height, &packed, stride, &error));
    assert(packed.size() == stride * height);
    // RAW10 is four low bytes followed by the packed 2-bit MSBs.
    assert(packed[0] == 0U && packed[1] == 1U && packed[2] == 255U &&
           packed[3] == 0U && packed[4] == 64U);
    std::vector<uint16_t> output;
    assert(camera::raw10::unpack(packed.data(), packed.size(), width, height,
                                  &output, stride, &error));
    assert(output == input);
    assert(!camera::raw10::unpack(packed.data(), packed.size() - 1, width, height,
                                  &output, stride, &error));
    input[2] = 1024U;
    assert(!camera::raw10::pack(input.data(), width, height, &packed, stride, &error));
}

void test_virtual_sensor_controls_and_patterns() {
    for (const camera::BayerPattern pattern : {camera::BayerPattern::RGGB,
                                                camera::BayerPattern::GRBG,
                                                camera::BayerPattern::GBRG,
                                                camera::BayerPattern::BGGR}) {
        camera::VirtualSensorConfig config;
        config.width = 19;
        config.height = 11;
        config.bayer = pattern;
        config.scene = camera::VirtualScene::Motion;
        config.noise_sigma = 0.0F;
        camera::VirtualSensor sensor(config);
        std::string error;
        assert(sensor.start(&error));
        camera::Raw10Frame frame0;
        assert(sensor.capture(&frame0, &error));
        assert(frame0.sequence == 0);
        assert(frame0.bayer == pattern);
        assert(frame0.valid());
        const camera::VirtualSensorControls initial = sensor.controls();
        assert(sensor.set_exposure_us(20000.0, &error));
        assert(sensor.set_analog_gain(2.0F, &error));
        assert(sensor.set_digital_gain(1.5F, &error));
        camera::Raw10Frame frame1;
        assert(sensor.capture(&frame1, &error));
        assert(frame1.sequence == 1);
        assert(frame1.controls.exposure_us > initial.exposure_us);
        assert(frame1.controls.analog_gain == 2.0F);
        assert(frame1.controls.digital_gain == 1.5F);
        assert(frame1.data != frame0.data);
        std::vector<uint16_t> pixels0;
        std::vector<uint16_t> pixels1;
        assert(camera::raw10::unpack(frame0.data.data(), frame0.data.size(), frame0.width,
                                     frame0.height, &pixels0, frame0.stride, &error));
        assert(camera::raw10::unpack(frame1.data.data(), frame1.data.size(), frame1.width,
                                     frame1.height, &pixels1, frame1.stride, &error));
        uint64_t sum0 = 0;
        uint64_t sum1 = 0;
        for (size_t i = 0; i < pixels0.size(); ++i) {
            sum0 += pixels0[i];
            sum1 += pixels1[i];
        }
        assert(sum1 > sum0);
        sensor.stop();
        assert(!sensor.capture(&frame1, &error));
    }
}

}  // namespace

int main() {
    test_raw10_roundtrip();
    test_virtual_sensor_controls_and_patterns();
    return 0;
}
