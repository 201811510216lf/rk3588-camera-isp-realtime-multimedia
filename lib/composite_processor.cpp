#include "composite_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

bool valid_even(uint32_t value) { return value > 0U && (value & 1U) == 0U; }

void clear_error(std::string *error) {
    if (error) error->clear();
}

bool valid_view(const CompositeView &view, uint32_t output_width,
               uint32_t output_height) {
    return valid_even(view.src_width) && valid_even(view.src_height) &&
           view.dst.x >= 0.0F && view.dst.y >= 0.0F && view.dst.width > 0.0F &&
           view.dst.height > 0.0F && view.dst.x + view.dst.width <= output_width + 0.01F &&
           view.dst.y + view.dst.height <= output_height + 0.01F;
}

void scale_plane(const uint8_t *src, uint32_t src_width, uint32_t src_height,
                 uint8_t *dst, uint32_t dst_width, uint32_t dst_height,
                 const CompositeView &view, uint8_t fill) {
    (void)fill;
    const uint32_t x0 = static_cast<uint32_t>(std::lround(view.dst.x));
    const uint32_t y0 = static_cast<uint32_t>(std::lround(view.dst.y));
    const uint32_t dw = static_cast<uint32_t>(std::lround(view.dst.width));
    const uint32_t dh = static_cast<uint32_t>(std::lround(view.dst.height));
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t sy = std::min(src_height - 1U,
                                     static_cast<uint32_t>((static_cast<uint64_t>(y) * src_height) / dh));
        uint8_t *row = dst + static_cast<size_t>(y0 + y) * dst_width + x0;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx = std::min(src_width - 1U,
                                         static_cast<uint32_t>((static_cast<uint64_t>(x) * src_width) / dw));
            row[x] = src[static_cast<size_t>(sy) * src_width + sx];
        }
    }
}

void scale_uv(const uint8_t *src, uint32_t src_width, uint32_t src_height,
              uint8_t *dst, uint32_t dst_width, uint32_t dst_height,
              const CompositeView &view) {
    const uint32_t x0 = static_cast<uint32_t>(std::lround(view.dst.x)) & ~1U;
    const uint32_t y0 = static_cast<uint32_t>(std::lround(view.dst.y)) & ~1U;
    const uint32_t dw = static_cast<uint32_t>(std::lround(view.dst.width)) & ~1U;
    const uint32_t dh = static_cast<uint32_t>(std::lround(view.dst.height)) & ~1U;
    const uint32_t src_h = src_height / 2U;
    const uint32_t out_h = dst_height / 2U;
    const uint32_t yuv0 = y0 / 2U;
    for (uint32_t y = 0; y < dh / 2U; ++y) {
        const uint32_t sy = std::min(src_h - 1U,
                                     static_cast<uint32_t>((static_cast<uint64_t>(y) * src_h) / (dh / 2U)));
        uint8_t *row = dst + static_cast<size_t>(yuv0 + y) * dst_width + x0;
        for (uint32_t x = 0; x < dw; x += 2U) {
            const uint32_t sx = std::min(src_width - 2U,
                                         static_cast<uint32_t>((static_cast<uint64_t>(x) * src_width) / dw) & ~1U);
            const uint8_t *pixel = src + static_cast<size_t>(sy) * src_width + sx;
            row[x] = pixel[0];
            row[x + 1U] = pixel[1];
        }
    }
    (void)out_h;
}

} // namespace

bool CompositeProcessor::make_side_by_side(uint32_t src_width, uint32_t src_height,
                                            uint32_t output_width, uint32_t output_height,
                                            CompositeLayout *layout, std::string *error) {
    clear_error(error);
    if (!layout || !valid_even(src_width) || !valid_even(src_height) ||
        !valid_even(output_width) || !valid_even(output_height) || output_width < 2U) {
        if (error) *error = "all source and output dimensions must be positive and even";
        return false;
    }
    const uint32_t half_width = output_width / 2U;
    const float scale = std::min(static_cast<float>(half_width) / src_width,
                                 static_cast<float>(output_height) / src_height);
    const uint32_t view_width = std::max(2U, static_cast<uint32_t>(std::floor(src_width * scale)) & ~1U);
    const uint32_t view_height = std::max(2U, static_cast<uint32_t>(std::floor(src_height * scale)) & ~1U);
    if (view_width > half_width || view_height > output_height) {
        if (error) *error = "computed view does not fit output";
        return false;
    }
    const uint32_t y = (output_height - view_height) / 2U & ~1U;
    const uint32_t x0 = (half_width - view_width) / 2U & ~1U;
    const uint32_t x1 = half_width + x0;
    *layout = {};
    layout->output_width = output_width;
    layout->output_height = output_height;
    layout->cam0.src_width = src_width;
    layout->cam0.src_height = src_height;
    layout->cam0.dst = {static_cast<float>(x0), static_cast<float>(y),
                        static_cast<float>(view_width), static_cast<float>(view_height)};
    layout->cam0.scale_x = scale;
    layout->cam0.scale_y = scale;
    layout->cam0.offset_x = layout->cam0.dst.x;
    layout->cam0.offset_y = layout->cam0.dst.y;
    layout->cam1 = layout->cam0;
    layout->cam1.dst.x = static_cast<float>(x1);
    layout->cam1.offset_x = layout->cam1.dst.x;
    return true;
}

RectF CompositeProcessor::map_box(const CompositeView &view, const RectF &source_box) {
    RectF result;
    result.x = source_box.x * view.scale_x + view.offset_x;
    result.y = source_box.y * view.scale_y + view.offset_y;
    result.width = source_box.width * view.scale_x;
    result.height = source_box.height * view.scale_y;
    return result;
}

RectF CompositeProcessor::clip_box(const RectF &box, uint32_t width, uint32_t height) {
    const float x0 = std::max(0.0F, std::min(box.x, static_cast<float>(width)));
    const float y0 = std::max(0.0F, std::min(box.y, static_cast<float>(height)));
    const float x1 = std::max(x0, std::min(box.x + box.width, static_cast<float>(width)));
    const float y1 = std::max(y0, std::min(box.y + box.height, static_cast<float>(height)));
    return {x0, y0, x1 - x0, y1 - y0};
}

bool CompositeProcessor::compose_nv12(const uint8_t *cam0, const uint8_t *cam1,
                                       uint32_t src_width, uint32_t src_height,
                                       const CompositeLayout &layout,
                                       std::vector<uint8_t> *output, std::string *error) {
    clear_error(error);
    if (!cam0 || !cam1 || !output || layout.output_width == 0 || layout.output_height == 0 ||
        !valid_even(src_width) || !valid_even(src_height) ||
        !valid_view(layout.cam0, layout.output_width, layout.output_height) ||
        !valid_view(layout.cam1, layout.output_width, layout.output_height) ||
        layout.cam0.src_width != src_width || layout.cam0.src_height != src_height ||
        layout.cam1.src_width != src_width || layout.cam1.src_height != src_height) {
        if (error) *error = "invalid NV12 composite layout or input";
        return false;
    }
    const size_t output_size = static_cast<size_t>(layout.output_width) * layout.output_height * 3U / 2U;
    output->assign(output_size, 128U);
    std::fill(output->begin(), output->begin() + static_cast<size_t>(layout.output_width) * layout.output_height, 16U);
    uint8_t *dst_y = output->data();
    uint8_t *dst_uv = dst_y + static_cast<size_t>(layout.output_width) * layout.output_height;
    const size_t source_plane = static_cast<size_t>(src_width) * src_height;
    scale_plane(cam0, src_width, src_height, dst_y, layout.output_width, layout.output_height, layout.cam0, 16U);
    scale_plane(cam1, src_width, src_height, dst_y, layout.output_width, layout.output_height, layout.cam1, 16U);
    scale_uv(cam0 + source_plane, src_width, src_height, dst_uv,
             layout.output_width, layout.output_height, layout.cam0);
    scale_uv(cam1 + source_plane, src_width, src_height, dst_uv,
             layout.output_width, layout.output_height, layout.cam1);
    return true;
}

