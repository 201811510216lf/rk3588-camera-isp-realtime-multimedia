#include "ai/model_contract.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ai {
namespace {
bool fail(std::string *error, const std::string &message) {
    if (error)
        *error = message;
    return false;
}
std::string trim(const std::string &s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}
std::vector<std::string> split(const std::string &text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
        result.push_back(trim(item));
    return result;
}
float number(const std::string &s) {
    size_t used = 0;
    const auto value = std::stof(s, &used);
    if (used != s.size() || !std::isfinite(value))
        throw std::runtime_error("invalid finite number: " + s);
    return value;
}
uint32_t unsigned_number(const std::string &s) {
    if (s.empty() || s[0] == '-')
        throw std::runtime_error("invalid unsigned number");
    size_t used = 0;
    const auto value = std::stoull(s, &used);
    if (used != s.size() || value > UINT32_MAX)
        throw std::runtime_error("unsigned number out of range");
    return static_cast<uint32_t>(value);
}
std::array<float, 3> triple(const std::string &s) {
    const auto values = split(s);
    if (values.size() != 3)
        throw std::runtime_error("expected three channel values");
    return {number(values[0]), number(values[1]), number(values[2])};
}
uint8_t clip(float v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }
} // namespace

bool ModelContract::validate(std::string *error) const {
    if (error)
        error->clear();
    if (!metadata_complete || model_id.empty() || model_version.empty() || input_width == 0 ||
        input_height == 0)
        return fail(error, "RKNN_MODEL_CONTRACT_REQUIRED: complete model sidecar metadata is required");
    if (model_id.size() > 256 || model_version.size() > 256 ||
        (layout != TensorLayout::Nhwc && layout != TensorLayout::Nchw) ||
        (channel_order != ChannelOrder::Rgb && channel_order != ChannelOrder::Bgr) ||
        (tensor_type != TensorType::UInt8 && tensor_type != TensorType::Int8 &&
         tensor_type != TensorType::Float32) ||
        (resize != ResizePolicy::Stretch && resize != ResizePolicy::Letterbox) ||
        (yuv_matrix != YuvMatrix::Bt601 && yuv_matrix != YuvMatrix::Bt709))
        return fail(error, "invalid model metadata enum or identifier length");
    if (input_width > 16384 || input_height > 16384 ||
        static_cast<uint64_t>(input_width) * input_height > 16777216)
        return fail(error, "model tensor exceeds supported memory bound");
    if (!std::isfinite(multiplier) || multiplier <= 0.0F || !std::isfinite(quant_scale) ||
        quant_scale <= 0.0F)
        return fail(error, "invalid multiplier or quantization scale");
    for (size_t c = 0; c < 3; ++c)
        if (!std::isfinite(mean[c]) || !std::isfinite(stddev[c]) || stddev[c] <= 0.0F)
            return fail(error, "normalization requires finite mean and positive standard deviation");
    if ((tensor_type == TensorType::Int8 && (quant_zero_point < -128 || quant_zero_point > 127)) ||
        (tensor_type == TensorType::UInt8 && (quant_zero_point < 0 || quant_zero_point > 255)))
        return fail(error, "quantization zero point is outside tensor range");
    if (output_schema.empty() || class_names.empty() || class_names.size() > 1024)
        return fail(error, "output schema and class names are required");
    for (const auto &name : class_names)
        if (name.empty() || name.size() > 256)
            return fail(error, "empty or overlong class name");
    for (const auto &entry : behavior_classes)
        if (entry.first < 0 || static_cast<size_t>(entry.first) >= class_names.size() ||
            entry.second.empty() || entry.second.size() > 128)
            return fail(error, "behavior class mapping exceeds model classes");
    return true;
}

bool ModelContract::supports_legacy_rknn(std::string *error) const {
    if (!validate(error))
        return false;
    if (layout != TensorLayout::Nhwc || channel_order != ChannelOrder::Rgb ||
        tensor_type != TensorType::UInt8 || resize != ResizePolicy::Stretch || multiplier != 1.0F ||
        quant_scale != 1.0F || quant_zero_point != 0 || mean != std::array<float, 3>{0, 0, 0} ||
        stddev != std::array<float, 3>{1, 1, 1} || output_schema != "yolov5_int8_three_head" ||
        input_width % 32 != 0 || input_height % 32 != 0)
        return fail(error, "RKNN_MODEL_CONTRACT_UNSUPPORTED: legacy adapter requires explicitly declared RGB "
                           "UINT8 NHWC stretch and YOLOv5 three INT8 heads");
    return true;
}

bool load_model_contract(const std::string &path, ModelContract *contract, std::string *error) {
    if (!contract)
        return fail(error, "null model contract destination");
    std::ifstream stream(path);
    if (!stream)
        return fail(error, "cannot open model contract: " + path);
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0 || length > 1048576)
        return fail(error, "model contract exceeds the 1 MiB metadata bound");
    stream.seekg(0, std::ios::beg);
    std::map<std::string, std::string> entries;
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const auto at = line.find('=');
        if (at == std::string::npos ||
            !entries.emplace(trim(line.substr(0, at)), trim(line.substr(at + 1))).second)
            return fail(error, "invalid or duplicate model contract key");
    }
    const std::vector<std::string> keys{"model_id",      "model_version", "input_width",      "input_height",
                                        "layout",        "channel_order", "tensor_type",      "resize",
                                        "yuv_matrix",    "yuv_range",     "multiplier",       "mean",
                                        "stddev",        "quant_scale",   "quant_zero_point", "padding",
                                        "output_schema", "class_names",   "behavior_classes"};
    if (entries.size() != keys.size())
        return fail(error, "model contract has missing or unknown keys");
    for (const auto &key : keys)
        if (!entries.count(key))
            return fail(error, "missing model contract key: " + key);
    try {
        ModelContract c;
        c.model_id = entries.at("model_id");
        c.model_version = entries.at("model_version");
        c.input_width = unsigned_number(entries.at("input_width"));
        c.input_height = unsigned_number(entries.at("input_height"));
        auto choose = [&entries](const char *key, const char *a, const char *b) {
            const auto &v = entries.at(key);
            if (v != a && v != b)
                throw std::runtime_error(std::string("invalid ") + key);
            return v == a;
        };
        c.layout = choose("layout", "NHWC", "NCHW") ? TensorLayout::Nhwc : TensorLayout::Nchw;
        c.channel_order = choose("channel_order", "RGB", "BGR") ? ChannelOrder::Rgb : ChannelOrder::Bgr;
        const auto &type = entries.at("tensor_type");
        if (type != "UINT8" && type != "INT8" && type != "FLOAT32")
            throw std::runtime_error("invalid tensor_type");
        c.tensor_type =
            type == "UINT8" ? TensorType::UInt8 : (type == "INT8" ? TensorType::Int8 : TensorType::Float32);
        c.resize = choose("resize", "stretch", "letterbox") ? ResizePolicy::Stretch : ResizePolicy::Letterbox;
        c.yuv_matrix = choose("yuv_matrix", "BT601", "BT709") ? YuvMatrix::Bt601 : YuvMatrix::Bt709;
        c.limited_range = choose("yuv_range", "limited", "full");
        c.multiplier = number(entries.at("multiplier"));
        c.mean = triple(entries.at("mean"));
        c.stddev = triple(entries.at("stddev"));
        c.quant_scale = number(entries.at("quant_scale"));
        size_t used = 0;
        c.quant_zero_point = std::stoi(entries.at("quant_zero_point"), &used);
        if (used != entries.at("quant_zero_point").size())
            throw std::runtime_error("invalid zero point");
        const auto padding = unsigned_number(entries.at("padding"));
        if (padding > 255)
            throw std::runtime_error("invalid padding");
        c.padding = static_cast<uint8_t>(padding);
        c.output_schema = entries.at("output_schema");
        c.class_names = split(entries.at("class_names"));
        for (const auto &entry : split(entries.at("behavior_classes"))) {
            const auto colon = entry.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("behavior classes require class_id:name");
            const auto id = unsigned_number(trim(entry.substr(0, colon)));
            if (id > INT32_MAX ||
                !c.behavior_classes.emplace(static_cast<int>(id), trim(entry.substr(colon + 1))).second)
                throw std::runtime_error("duplicate behavior class");
        }
        c.metadata_complete = true;
        if (!c.validate(error))
            return false;
        *contract = std::move(c);
        return true;
    } catch (const std::exception &e) {
        return fail(error, std::string("invalid model contract: ") + e.what());
    }
}

bool preprocess_nv12(const camera::CameraFrame &frame, const ModelContract &c, Crop crop,
                     PreparedTensor *output, std::string *error) {
    if (!output || !c.validate(error))
        return false;
    const auto &d = frame.desc;
    if (!frame.valid())
        return fail(error, "invalid NV12 frame lease");
    if (!camera::validate_nv12_layout(d, error))
        return false;
    const auto *y_plane = static_cast<const uint8_t *>(d.data);
    const auto *uv_plane =
        d.plane_count == 2 ? static_cast<const uint8_t *>(d.data_uv) : y_plane + d.offset_uv;
    if (crop.width == 0 && crop.height == 0)
        crop = {0, 0, d.width, d.height};
    if (!crop.width || !crop.height || crop.x > d.width || crop.y > d.height ||
        crop.width > d.width - crop.x || crop.height > d.height - crop.y)
        return fail(error, "crop is outside the source frame");
    PreparedTensor result;
    result.width = c.input_width;
    result.height = c.input_height;
    result.crop = crop;
    uint32_t resized_w = c.input_width, resized_h = c.input_height;
    if (c.resize == ResizePolicy::Letterbox) {
        const double scale = std::min(static_cast<double>(c.input_width) / crop.width,
                                      static_cast<double>(c.input_height) / crop.height);
        resized_w = std::clamp(static_cast<uint32_t>(std::lround(crop.width * scale)), 1U, c.input_width);
        resized_h = std::clamp(static_cast<uint32_t>(std::lround(crop.height * scale)), 1U, c.input_height);
    }
    result.scale_x = static_cast<float>(resized_w) / crop.width;
    result.scale_y = static_cast<float>(resized_h) / crop.height;
    result.pad_x = (c.input_width - resized_w) / 2;
    result.pad_y = (c.input_height - resized_h) / 2;
    const size_t pixels = static_cast<size_t>(c.input_width) * c.input_height;
    if (c.tensor_type == TensorType::Float32)
        result.floats.resize(pixels * 3);
    else
        result.bytes.resize(pixels * 3);
    for (uint32_t row = 0; row < c.input_height; ++row)
        for (uint32_t col = 0; col < c.input_width; ++col) {
            std::array<uint8_t, 3> rgb{c.padding, c.padding, c.padding};
            if (col >= result.pad_x && col < result.pad_x + resized_w && row >= result.pad_y &&
                row < result.pad_y + resized_h) {
                const uint32_t sx =
                    crop.x +
                    std::min(crop.width - 1,
                             static_cast<uint32_t>((static_cast<uint64_t>(col - result.pad_x) * crop.width) /
                                                   resized_w));
                const uint32_t sy =
                    crop.y +
                    std::min(crop.height - 1,
                             static_cast<uint32_t>((static_cast<uint64_t>(row - result.pad_y) * crop.height) /
                                                   resized_h));
                const auto uv = uv_plane + static_cast<size_t>(sy / 2) * d.stride_uv + (sx & ~1U);
                const float yy =
                    (y_plane[static_cast<size_t>(sy) * d.stride_y + sx] - (c.limited_range ? 16.0F : 0.0F)) *
                    (c.limited_range ? 255.0F / 219.0F : 1.0F);
                const float u = (uv[0] - 128.0F) * (c.limited_range ? 255.0F / 224.0F : 1.0F);
                const float v = (uv[1] - 128.0F) * (c.limited_range ? 255.0F / 224.0F : 1.0F);
                if (c.yuv_matrix == YuvMatrix::Bt601)
                    rgb = {clip(yy + 1.402F * v), clip(yy - 0.344136F * u - 0.714136F * v),
                           clip(yy + 1.772F * u)};
                else
                    rgb = {clip(yy + 1.5748F * v), clip(yy - 0.187324F * u - 0.468124F * v),
                           clip(yy + 1.8556F * u)};
            }
            for (size_t ch = 0; ch < 3; ++ch) {
                const float value =
                    (rgb[c.channel_order == ChannelOrder::Rgb ? ch : 2 - ch] * c.multiplier - c.mean[ch]) /
                    c.stddev[ch];
                if (!std::isfinite(value))
                    return fail(error, "normalization produced a nonfinite tensor value");
                const size_t at = c.layout == TensorLayout::Nhwc
                                      ? (static_cast<size_t>(row) * c.input_width + col) * 3 + ch
                                      : ch * pixels + static_cast<size_t>(row) * c.input_width + col;
                if (c.tensor_type == TensorType::Float32)
                    result.floats[at] = value;
                else {
                    const float minimum = c.tensor_type == TensorType::Int8 ? -128.0F : 0.0F;
                    const float maximum = c.tensor_type == TensorType::Int8 ? 127.0F : 255.0F;
                    const auto quantized = static_cast<int>(
                        std::round(std::clamp(value / c.quant_scale + c.quant_zero_point, minimum, maximum)));
                    result.bytes[at] = static_cast<uint8_t>(quantized);
                }
            }
        }
    *output = std::move(result);
    if (error)
        error->clear();
    return true;
}

} // namespace ai
