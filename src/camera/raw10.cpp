#include "camera/raw10.h"

#include <algorithm>
#include <limits>

namespace camera {
namespace raw10 {
namespace {

void set_error(std::string *error, const char *message) {
    if (error) *error = message;
}

bool checked_mul(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) return false;
    *out = a * b;
    return true;
}

}  // namespace

size_t row_bytes(uint32_t width) {
    if (width == 0) return 0;
    const size_t groups = (static_cast<size_t>(width) + 3U) / 4U;
    if (groups > std::numeric_limits<size_t>::max() / 5U) return 0;
    return groups * 5U;
}

size_t packed_size(uint32_t width, uint32_t height, size_t stride) {
    const size_t minimum = row_bytes(width);
    if (minimum == 0) return 0;
    if (stride == 0) stride = minimum;
    if (stride < minimum) return 0;
    size_t total = 0;
    if (!checked_mul(stride, static_cast<size_t>(height), &total)) return 0;
    return total;
}

bool pack(const uint16_t *pixels, uint32_t width, uint32_t height,
          std::vector<uint8_t> *packed, size_t stride, std::string *error) {
    if (!pixels || !packed) {
        set_error(error, "RAW10 pack requires non-null input and output");
        return false;
    }
    const size_t minimum = row_bytes(width);
    if (minimum == 0 || height == 0) {
        set_error(error, "RAW10 dimensions must be non-zero");
        return false;
    }
    if (stride == 0) stride = minimum;
    const size_t total = packed_size(width, height, stride);
    if (total == 0) {
        set_error(error, "RAW10 stride or image size overflows");
        return false;
    }
    const size_t pixel_count = static_cast<size_t>(width) * height;
    // The caller owns the input buffer; this check avoids arithmetic overflow
    // in the only size computation used below.
    if (width != 0 && pixel_count / width != height) {
        set_error(error, "RAW10 pixel count overflows");
        return false;
    }
    packed->assign(total, 0U);
    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t *src = pixels + static_cast<size_t>(y) * width;
        uint8_t *dst = packed->data() + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < width; x += 4U) {
            const uint32_t remaining = width - x;
            const uint32_t count = std::min<uint32_t>(remaining, 4U);
            uint16_t p[4] = {0U, 0U, 0U, 0U};
            for (uint32_t i = 0; i < count; ++i) {
                if (src[x + i] > 1023U) {
                    set_error(error, "RAW10 pixel exceeds 10-bit range");
                    packed->clear();
                    return false;
                }
                p[i] = src[x + i];
            }
            const size_t group = static_cast<size_t>(x / 4U) * 5U;
            dst[group + 0U] = static_cast<uint8_t>(p[0] & 0xFFU);
            if (count > 1U) dst[group + 1U] = static_cast<uint8_t>(p[1] & 0xFFU);
            if (count > 2U) dst[group + 2U] = static_cast<uint8_t>(p[2] & 0xFFU);
            if (count > 3U) dst[group + 3U] = static_cast<uint8_t>(p[3] & 0xFFU);
            dst[group + 4U] = static_cast<uint8_t>(((p[0] >> 8U) & 3U) |
                (((p[1] >> 8U) & 3U) << 2U) | (((p[2] >> 8U) & 3U) << 4U) |
                (((p[3] >> 8U) & 3U) << 6U));
        }
    }
    return true;
}

bool unpack(const uint8_t *packed, size_t packed_length, uint32_t width,
            uint32_t height, std::vector<uint16_t> *pixels, size_t stride,
            std::string *error) {
    if (!packed || !pixels) {
        set_error(error, "RAW10 unpack requires non-null input and output");
        return false;
    }
    const size_t minimum = row_bytes(width);
    if (minimum == 0 || height == 0) {
        set_error(error, "RAW10 dimensions must be non-zero");
        return false;
    }
    if (stride == 0) stride = minimum;
    const size_t total = packed_size(width, height, stride);
    if (total == 0 || packed_length < total) {
        set_error(error, "RAW10 input is shorter than width/height/stride require");
        return false;
    }
    size_t pixel_count = 0;
    if (!checked_mul(static_cast<size_t>(width), static_cast<size_t>(height), &pixel_count)) {
        set_error(error, "RAW10 pixel count overflows");
        return false;
    }
    pixels->assign(pixel_count, 0U);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *src = packed + static_cast<size_t>(y) * stride;
        uint16_t *dst = pixels->data() + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; x += 4U) {
            const uint32_t count = std::min<uint32_t>(width - x, 4U);
            const size_t group = static_cast<size_t>(x / 4U) * 5U;
            const uint8_t low = src[group + 4U];
            const uint16_t values[4] = {
                static_cast<uint16_t>(src[group + 0U] | ((low & 0x03U) << 8U)),
                static_cast<uint16_t>(src[group + 1U] | (((low >> 2U) & 0x03U) << 8U)),
                static_cast<uint16_t>(src[group + 2U] | (((low >> 4U) & 0x03U) << 8U)),
                static_cast<uint16_t>(src[group + 3U] | (((low >> 6U) & 0x03U) << 8U)),
            };
            for (uint32_t i = 0; i < count; ++i) dst[x + i] = values[i];
        }
    }
    return true;
}

}  // namespace raw10
}  // namespace camera

