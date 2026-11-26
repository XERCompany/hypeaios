#include "hcpp/crc32.h"
namespace hcpp {
    u32 crc32_update(u32 crc, const void* data, size_t size) noexcept {
        const auto* bytes = static_cast<const u8*>(data);
        crc = ~crc;
        for (size_t i = 0; i < size; ++i) {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (-static_cast<i32>(crc & 1)));
        }
        return ~crc;
    }
    u32 crc32(const void* data, size_t size) noexcept { return crc32_update(0, data, size); }
}  // namespace hcpp
