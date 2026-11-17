#pragma once

#include "camera_frame.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ai {

enum class TensorLayout { Nhwc, Nchw };
enum class ChannelOrder { Rgb, Bgr };
enum class TensorType { UInt8, Int8, Float32 };
enum class ResizePolicy { Stretch, Letterbox };
enum class YuvMatrix { Bt601, Bt709 };

// No model dimensions, normalization, YUV interpretation or output schema are
// inferred from a filename. A validated sidecar is required at the hardware boundary.
struct ModelContract {
    std::string model_id;
    std::string model_version;
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    TensorLayout layout = TensorLayout::Nhwc;
    ChannelOrder channel_order = ChannelOrder::Rgb;
    TensorType tensor_type = TensorType::UInt8;
    ResizePolicy resize = ResizePolicy::Letterbox;
    YuvMatrix yuv_matrix = YuvMatrix::Bt601;
    bool limited_range = true;
    float multiplier = 0.0F;
    std::array<float, 3> mean{};
    std::array<float, 3> stddev{};
    float quant_scale = 0.0F;
    int quant_zero_point = 0;
    uint8_t padding = 0;
    std::string output_schema;
    std::vector<std::string> class_names;
    std::map<int, std::string> behavior_classes; // empty for detection-only models
    bool metadata_complete = false;
    bool validate(std::string *error = nullptr) const;
    bool supports_legacy_rknn(std::string *error = nullptr) const;
};

// Strict key=value sidecar; unknown/duplicate/missing keys are errors.
bool load_model_contract(const std::string &path, ModelContract *contract, std::string *error);

struct Crop {
    uint32_t x = 0, y = 0, width = 0, height = 0;
};
struct PreparedTensor {
    std::vector<uint8_t> bytes; // UInt8 or two's-complement Int8 storage
    std::vector<float> floats;
    uint32_t width = 0, height = 0;
    float scale_x = 0.0F, scale_y = 0.0F;
    uint32_t pad_x = 0, pad_y = 0;
    Crop crop;
};

// CPU reference preprocessor, explicitly not an RGA backend. Lease remains
// held throughout the read. Dimensions, strides and payload lengths are checked.
bool preprocess_nv12(const camera::CameraFrame &frame, const ModelContract &contract, Crop crop,
                     PreparedTensor *output, std::string *error = nullptr);

} // namespace ai
