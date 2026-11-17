#include "ai/inference_backend.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ai {
namespace {

class DemoBackend final : public IInferenceBackend {
  public:
    bool initialize(const InferenceConfig &config) override {
        config_ = config;
        initialized_ = config.input_width > 0 && config.input_height > 0;
        return initialized_;
    }
    InferenceResult infer(const isp::RgbImage &image, uint64_t sequence) override {
        InferenceResult result;
        result.source_sequence = sequence;
        result.demo_result = true;
        const auto begin = std::chrono::steady_clock::now();
        if (!initialized_ || !image.valid()) {
            result.error = "demo backend is not initialized or image is invalid";
            return result;
        }
        double sum = 0.0;
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x) {
                const auto *p = image.data.data() + static_cast<size_t>(y) * image.stride + x * 3U;
                sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
            }
        const float mean = static_cast<float>(
            sum / std::max<uint64_t>(1, static_cast<uint64_t>(image.width) * image.height));
        if (mean > config_.confidence_threshold * 255.0F) {
            Detection d;
            d.x = image.width * 0.25F;
            d.y = image.height * 0.2F;
            d.width = image.width * 0.5F;
            d.height = image.height * 0.6F;
            d.class_id = 0;
            d.confidence = std::clamp(mean / 255.0F, 0.0F, 0.99F);
            d.source_sequence = sequence;
            result.detections.push_back(d);
        }
        result.ok = true;
        result.elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        return result;
    }
    void shutdown() override { initialized_ = false; }
    const char *name() const override { return "DemoBackend(SIMULATED)"; }

  private:
    InferenceConfig config_;
    bool initialized_ = false;
};

class UnavailableBackend final : public IInferenceBackend {
  public:
    explicit UnavailableBackend(BackendKind kind) : kind_(kind) {}
    bool initialize(const InferenceConfig &config) override {
        config_ = config;
        return false;
    }
    InferenceResult infer(const isp::RgbImage &, uint64_t sequence) override {
        InferenceResult r;
        r.source_sequence = sequence;
        r.error = error();
        return r;
    }
    void shutdown() override {}
    const char *name() const override {
        return kind_ == BackendKind::Rknn ? "RKNN_BACKEND_UNAVAILABLE"
                                          : "ONNXRuntime(OPTIONAL_NOT_AVAILABLE)";
    }

  private:
    BackendKind kind_;
    InferenceConfig config_;
    const char *error() const {
        return kind_ == BackendKind::Rknn ? "RKNN_BACKEND_UNAVAILABLE: real RKNN SDK backend is not "
                                            "available through the independent Software ISP demo"
                                          : "ONNX Runtime backend unavailable in this host build";
    }
};

} // namespace

std::unique_ptr<IInferenceBackend> create_backend(BackendKind kind) {
    return kind == BackendKind::Demo ? std::unique_ptr<IInferenceBackend>(new DemoBackend())
                                     : std::unique_ptr<IInferenceBackend>(new UnavailableBackend(kind));
}

} // namespace ai
