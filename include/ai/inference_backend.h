#pragma once

#include "isp/isp_pipeline.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ai {

enum class BackendKind { Demo, OnnxRuntime, Rknn };

struct InferenceConfig {
    BackendKind backend = BackendKind::Demo;
    uint32_t input_width = 640;
    uint32_t input_height = 640;
    bool nchw = true;
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    std::string model_path;
    int core = -1;
};

struct Detection {
    float x = 0.0F, y = 0.0F, width = 0.0F, height = 0.0F;
    int class_id = -1;
    float confidence = 0.0F;
    uint64_t source_sequence = 0;
    uint64_t age_ms = 0;
};

struct InferenceResult {
    bool ok = false;
    bool demo_result = false;
    std::string error;
    std::vector<Detection> detections;
    double elapsed_ms = 0.0;
    uint64_t source_sequence = 0;
};

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual bool initialize(const InferenceConfig &config) = 0;
    virtual InferenceResult infer(const isp::RgbImage &image, uint64_t sequence = 0) = 0;
    virtual void shutdown() = 0;
    virtual const char *name() const = 0;
};

std::unique_ptr<IInferenceBackend> create_backend(BackendKind kind);

} // namespace ai
