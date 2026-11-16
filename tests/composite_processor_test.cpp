#include "composite_processor.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void test_layout_and_boxes() {
    CompositeLayout layout;
    std::string error;
    assert(CompositeProcessor::make_side_by_side(1920, 1080, 1920, 1080,
                                                  &layout, &error));
    assert(layout.cam0.dst.x == 0.0F);
    assert(layout.cam1.dst.x > layout.cam0.dst.x);
    const RectF mapped = CompositeProcessor::map_box(layout.cam1, {100.0F, 200.0F, 300.0F, 400.0F});
    assert(mapped.x > layout.cam1.dst.x);
    assert(mapped.y >= layout.cam1.dst.y);
    const RectF clipped = CompositeProcessor::clip_box({-10.0F, -2.0F, 50.0F, 20.0F}, 1920, 1080);
    assert(clipped.x == 0.0F && clipped.y == 0.0F);
    assert(clipped.width == 40.0F && clipped.height == 18.0F);
}

void test_nv12_compose() {
    CompositeLayout layout;
    assert(CompositeProcessor::make_side_by_side(4, 4, 8, 4, &layout));
    std::vector<uint8_t> left(4 * 4 * 3 / 2, 16U);
    std::vector<uint8_t> right(4 * 4 * 3 / 2, 16U);
    for (size_t i = 4 * 4; i < left.size(); ++i) left[i] = 90U;
    for (size_t i = 4 * 4; i < right.size(); ++i) right[i] = 170U;
    std::vector<uint8_t> output;
    std::string error;
    assert(CompositeProcessor::compose_nv12(left.data(), right.data(), 4, 4,
                                             layout, &output, &error));
    assert(output.size() == 8 * 4 * 3 / 2);
    const size_t uv = 8 * 4;
    // left and right UV values must stay in their respective halves.
    assert(output[uv] == 90U);
    assert(output[uv + 4U] == 170U);
}

} // namespace

int main() {
    test_layout_and_boxes();
    test_nv12_compose();
    return 0;
}

