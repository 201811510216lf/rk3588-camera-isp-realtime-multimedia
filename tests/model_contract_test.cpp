#include "ai/model_contract.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
ai::ModelContract contract() {
    ai::ModelContract c;
    c.metadata_complete = true;
    c.model_id = "fixture";
    c.model_version = "test-only";
    c.input_width = c.input_height = 2;
    c.resize = ai::ResizePolicy::Stretch;
    c.multiplier = 1;
    c.stddev = {1, 1, 1};
    c.quant_scale = 1;
    c.class_names = {"rat"};
    c.output_schema = "test";
    return c;
}
camera::CameraFrame frame(std::vector<uint8_t> &y, std::vector<uint8_t> &uv) {
    camera::CameraFrame f;
    auto pool = std::make_shared<camera::BufferLeaseState>();
    assert(pool->acquire(0));
    f.lease = std::make_shared<camera::FrameLease>(pool, 0);
    f.desc.data = y.data();
    f.desc.data_length = y.size();
    f.desc.data_uv = uv.data();
    f.desc.data_uv_length = uv.size();
    f.desc.width = f.desc.height = 2;
    f.desc.bytesused = y.size();
    f.desc.bytesused_uv = uv.size();
    f.desc.stride_y = f.desc.stride_uv = 4;
    f.desc.fourcc = 0x32314d4e;
    f.desc.plane_count = 2;
    return f;
}
} // namespace
int main() {
    auto c = contract();
    std::string error;
    assert(c.validate(&error));
    assert(!c.supports_legacy_rknn(&error));
    c.metadata_complete = false;
    assert(!c.validate(&error));
    c = contract();
    c.stddev[0] = 0;
    assert(!c.validate());
    c = contract();
    c.layout = static_cast<ai::TensorLayout>(9);
    assert(!c.validate());
    c = contract();
    std::vector<uint8_t> y{16, 235, 0, 0, 81, 145, 0, 0}, uv{128, 128, 0, 0};
    auto f = frame(y, uv);
    ai::PreparedTensor out;
    assert(ai::preprocess_nv12(f, c, {}, &out, &error));
    assert(out.bytes.size() == 12 && out.bytes[0] == 0 && out.bytes[3] == 255);
    uv[0] = 90;
    uv[1] = 240;
    y[0] = 81;
    assert(ai::preprocess_nv12(f, c, {}, &out));
    const auto red = out.bytes;
    c.channel_order = ai::ChannelOrder::Bgr;
    assert(ai::preprocess_nv12(f, c, {}, &out));
    assert(out.bytes[2] == red[0] && out.bytes[0] == red[2] && red[0] > red[2]);
    uv[0] = uv[1] = 128;
    y[0] = 16;
    c.channel_order = ai::ChannelOrder::Rgb;
    f.desc.data_uv_length = 1;
    assert(!ai::preprocess_nv12(f, c, {}, &out));
    f.desc.data_uv_length = uv.size();
    assert(!ai::preprocess_nv12(f, c, {1, 1, 2, 2}, &out));
    c.input_width = 4;
    c.resize = ai::ResizePolicy::Letterbox;
    assert(ai::preprocess_nv12(f, c, {}, &out));
    assert(out.pad_x == 1 && out.pad_y == 0 && out.bytes.size() == 24);
    c = contract();
    c.tensor_type = ai::TensorType::Float32;
    c.layout = ai::TensorLayout::Nchw;
    c.channel_order = ai::ChannelOrder::Bgr;
    c.multiplier = 1.0F / 255;
    c.mean = {0.5F, 0.5F, 0.5F};
    c.stddev = {0.5F, 0.5F, 0.5F};
    assert(ai::preprocess_nv12(f, c, {}, &out));
    assert(out.bytes.empty() && out.floats.size() == 12 && out.floats[0] == -1 && out.floats[1] == 1);
    c = contract();
    c.tensor_type = ai::TensorType::Int8;
    c.quant_zero_point = -128;
    assert(ai::preprocess_nv12(f, c, {}, &out));
    assert(out.bytes[0] == 128 && out.bytes[3] == 127);
    // Single-plane NV12 with a larger aligned UV offset must not use packed size.
    std::vector<uint8_t> single(20, 0);
    single[0] = single[1] = single[4] = single[5] = 235;
    single[16] = single[17] = 128;
    f.desc.data = single.data();
    f.desc.data_length = single.size();
    f.desc.data_uv = nullptr;
    f.desc.offset_uv = 16;
    f.desc.bytesused = single.size();
    f.desc.bytesused_uv = 0;
    f.desc.fourcc = 0x3231564e;
    f.desc.plane_count = 1;
    c = contract();
    assert(ai::preprocess_nv12(f, c, {}, &out));
    assert(out.bytes[0] == 255);
    f.desc.offset_uv = 19;
    assert(!ai::preprocess_nv12(f, c, {}, &out));
    const auto path = std::filesystem::temp_directory_path() / "rk-model-contract-unit-test.conf";
    {
        std::ofstream file(path);
        file << "model_id=fixture\nmodel_version=test-only\ninput_width=32\ninput_height=32\nlayout="
                "NHWC\nchannel_order=RGB\ntensor_type=UINT8\nresize=stretch\nyuv_matrix=BT601\nyuv_range="
                "limited\nmultiplier=1\nmean=0,0,0\nstddev=1,1,1\nquant_scale=1\nquant_zero_point=0\npadding="
                "114\noutput_schema=yolov5_int8_three_head\nclass_names=rat\nbehavior_classes=\n";
    }
    assert(ai::load_model_contract(path.string(), &c, &error));
    assert(c.supports_legacy_rknn());
    {
        std::ofstream file(path, std::ios::app);
        file << "unknown=1\n";
    }
    assert(!ai::load_model_contract(path.string(), &c, &error));
    std::filesystem::remove(path);
    return 0;
}
