#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace camera {
namespace raw10 {

// Returns the minimum number of bytes needed to store one packed RAW10 row.
size_t row_bytes(uint32_t width);

// Returns the packed image size. When stride is zero, the minimum row stride is
// used. A stride smaller than row_bytes(width) is rejected and returns zero.
size_t packed_size(uint32_t width, uint32_t height, size_t stride = 0);

// Pack standard RAW10, four-pixels-to-five-bytes: four low bytes followed by
// one byte containing the four 2-bit MSBs. Rows are padded to
// stride bytes and padding is initialized to zero. The function validates all
// dimensions and never writes past the output vector.
bool pack(const uint16_t *pixels, uint32_t width, uint32_t height,
          std::vector<uint8_t> *packed, size_t stride = 0,
          std::string *error = nullptr);

// Unpack standard RAW10 into one uint16 value per pixel. The input may
// contain row padding; only the first row_bytes(width) bytes are consumed.
bool unpack(const uint8_t *packed, size_t packed_length, uint32_t width,
            uint32_t height, std::vector<uint16_t> *pixels,
            size_t stride = 0, std::string *error = nullptr);

inline bool packRaw10(const uint16_t *pixels, uint32_t width, uint32_t height,
                      std::vector<uint8_t> *packed, size_t stride = 0,
                      std::string *error = nullptr) {
    return pack(pixels, width, height, packed, stride, error);
}

inline bool unpackRaw10(const uint8_t *packed, size_t packed_length,
                        uint32_t width, uint32_t height,
                        std::vector<uint16_t> *pixels, size_t stride = 0,
                        std::string *error = nullptr) {
    return unpack(packed, packed_length, width, height, pixels, stride, error);
}

}  // namespace raw10

// Compatibility spellings used by pipeline code and interview exercises.
inline bool packRaw10(const uint16_t *pixels, uint32_t width, uint32_t height,
                      std::vector<uint8_t> *packed, size_t stride = 0,
                      std::string *error = nullptr) {
    return raw10::pack(pixels, width, height, packed, stride, error);
}

inline bool unpackRaw10(const uint8_t *packed, size_t packed_length,
                        uint32_t width, uint32_t height,
                        std::vector<uint16_t> *pixels, size_t stride = 0,
                        std::string *error = nullptr) {
    return raw10::unpack(packed, packed_length, width, height, pixels, stride, error);
}

}  // namespace camera
