#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Geometry and CPU fallback used by the host tests and as the conservative
// path when the target RGA SDK cannot be imported safely. The target pipeline
// may replace compose_nv12() with an RGA implementation while retaining the
// exact same mapping contract.
struct RectF {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct DetectionBox {
    RectF box;
    int class_id = -1;
    float confidence = 0.0F;
    std::string label;
};

struct CompositeView {
    uint32_t src_width = 0;
    uint32_t src_height = 0;
    RectF dst;
    // Scale and translation map a source point to composite coordinates.
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float offset_x = 0.0F;
    float offset_y = 0.0F;
};

struct CompositeLayout {
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    CompositeView cam0;
    CompositeView cam1;
};

class CompositeProcessor {
public:
    // Creates an aspect-preserving side-by-side layout. Each source is
    // letterboxed into half of the output; no source pixels are stretched.
    static bool make_side_by_side(uint32_t src_width, uint32_t src_height,
                                  uint32_t output_width, uint32_t output_height,
                                  CompositeLayout *layout, std::string *error = nullptr);

    static RectF map_box(const CompositeView &view, const RectF &source_box);
    static RectF clip_box(const RectF &box, uint32_t width, uint32_t height);

    // Packed NV12 (Y plane followed by UV plane) CPU fallback. Inputs must be
    // tightly packed width*height*3/2 buffers. This function is deterministic
    // and suitable for validating RGA coordinate/stride decisions on host.
    static bool compose_nv12(const uint8_t *cam0, const uint8_t *cam1,
                             uint32_t src_width, uint32_t src_height,
                             const CompositeLayout &layout,
                             std::vector<uint8_t> *output,
                             std::string *error = nullptr);
};

