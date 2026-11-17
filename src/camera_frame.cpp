#include "camera_frame.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace camera {
namespace {
bool fail(std::string *error, const char *message) {
    if (error)
        *error = message;
    return false;
}
bool rows_fit(size_t length, uint32_t stride, uint32_t rows, uint32_t width) {
    if (rows == 0 || stride < width)
        return false;
    return static_cast<uint64_t>(rows - 1U) * stride + width <= length;
}
} // namespace

bool validate_nv12_layout(const CameraBufferDesc &d, std::string *error) {
    if (error)
        error->clear();
    if (!d.data || d.width == 0 || d.height == 0 || (d.width & 1U) || (d.height & 1U))
        return fail(error, "NV12 requires non-null data and positive even dimensions");
    if ((d.fourcc != kNv12Fourcc && d.fourcc != kNv12MFourcc) ||
        d.plane_count != (d.fourcc == kNv12MFourcc ? 2U : 1U))
        return fail(error, "NV12/NV12M format and storage-plane count disagree");
    if (d.bytesused > d.data_length || d.bytesused_uv > d.data_uv_length ||
        (d.mapped_length &&
         (d.data_offset > d.mapped_length || d.bytesused > d.mapped_length - d.data_offset)) ||
        (d.plane_count == 2 && d.mapped_length_uv &&
         (d.data_offset_uv > d.mapped_length_uv || d.bytesused_uv > d.mapped_length_uv - d.data_offset_uv)))
        return fail(error, "NV12 plane bytesused exceeds its available mapping");
    const size_t y_bytes = std::min(d.data_length, d.bytesused);
    if (!rows_fit(y_bytes, d.stride_y, d.height, d.width))
        return fail(error, "NV12 luma payload is truncated or stride is too small");
    if (d.plane_count == 2) {
        if (!d.data_uv ||
            !rows_fit(std::min(d.data_uv_length, d.bytesused_uv), d.stride_uv, d.height / 2U, d.width))
            return fail(error, "NV12M chroma payload is truncated or missing");
    } else {
        const uint64_t y_extent = static_cast<uint64_t>(d.stride_y) * d.height;
        if (d.offset_uv < y_extent || d.offset_uv > y_bytes ||
            !rows_fit(y_bytes - d.offset_uv, d.stride_uv, d.height / 2U, d.width))
            return fail(error, "NV12 chroma offset/stride exceeds the single-plane payload");
    }
    const uint64_t pixels = static_cast<uint64_t>(d.width) * d.height;
    if (pixels > (std::numeric_limits<size_t>::max() / 3U) * 2U)
        return fail(error, "NV12 packed image size overflows address space");
    return true;
}

bool copy_nv12_packed(const CameraBufferDesc &d, std::vector<uint8_t> *packed, std::string *error) {
    if (!packed)
        return fail(error, "NV12 output buffer is null");
    if (!validate_nv12_layout(d, error))
        return false;
    const size_t y_size = static_cast<size_t>(d.width) * d.height;
    packed->resize(y_size + y_size / 2U);
    const auto *y = static_cast<const uint8_t *>(d.data);
    const auto *uv = d.plane_count == 2 ? static_cast<const uint8_t *>(d.data_uv) : y + d.offset_uv;
    for (uint32_t row = 0; row < d.height; ++row)
        std::memcpy(packed->data() + static_cast<size_t>(row) * d.width,
                    y + static_cast<size_t>(row) * d.stride_y, d.width);
    for (uint32_t row = 0; row < d.height / 2U; ++row)
        std::memcpy(packed->data() + y_size + static_cast<size_t>(row) * d.width,
                    uv + static_cast<size_t>(row) * d.stride_uv, d.width);
    return true;
}
} // namespace camera
