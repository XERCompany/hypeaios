#include "hcpp/memory.hpp"

namespace hcpp {

    void *copy(void *destination, const void *source, size_t bytes) noexcept {
        auto *dst = static_cast<u8 *>(destination);
        const auto *src = static_cast<const u8 *>(source);
        for (size_t i = 0; i < bytes; ++i) {
            dst[i] = src[i];
        }
        return destination;
    }

    void *move(void *destination, const void *source, size_t bytes) noexcept {
        auto *dst = static_cast<u8 *>(destination);
        const auto *src = static_cast<const u8 *>(source);

        if (dst == src || bytes == 0) {
            return destination;
        }

        if (dst < src) {
            for (size_t i = 0; i < bytes; ++i) {
                dst[i] = src[i];
            }
        } else {
            for (size_t i = bytes; i > 0; --i) {
                dst[i - 1] = src[i - 1];
            }
        }
        return destination;
    }

    void *set(void *destination, u8 value, size_t bytes) noexcept {
        auto *dst = static_cast<u8 *>(destination);
        for (size_t i = 0; i < bytes; ++i) {
            dst[i] = value;
        }
        return destination;
    }

    int compare(const void *left, const void *right, size_t bytes) noexcept {
        const auto *a = static_cast<const u8 *>(left);
        const auto *b = static_cast<const u8 *>(right);
        for (size_t i = 0; i < bytes; ++i) {
            if (a[i] != b[i]) {
                return static_cast<int>(a[i]) - static_cast<int>(b[i]);
            }
        }
        return 0;
    }

}  // namespace hcpp
