#include "ai/inference_backend.h"

#include <cassert>

int main() {
    auto demo = ai::create_backend(ai::BackendKind::Demo); ai::InferenceConfig config; assert(demo->initialize(config));
    isp::RgbImage image; image.width = image.height = 8; image.stride = 24; image.data.assign(8 * 24, 220U);
    const auto result = demo->infer(image, 42); assert(result.ok && result.demo_result && result.source_sequence == 42 && !result.detections.empty()); demo->shutdown();
    auto rknn = ai::create_backend(ai::BackendKind::Rknn); assert(!rknn->initialize(config));
    return 0;
}
