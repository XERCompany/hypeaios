#include "hcpp/types.hpp"

extern "C" {

    void* memset(void* dest, int value, hcpp::size_t n) noexcept {
        auto* d = static_cast<hcpp::u8*>(dest);
        const hcpp::u8 v = static_cast<hcpp::u8>(value);
        for (hcpp::size_t i = 0; i < n; ++i) d[i] = v;
        return dest;
    }

    void* memcpy(void* dest, const void* src, hcpp::size_t n) noexcept {
        auto* d = static_cast<hcpp::u8*>(dest);
        const auto* s = static_cast<const hcpp::u8*>(src);
        for (hcpp::size_t i = 0; i < n; ++i) d[i] = s[i];
        return dest;
    }

    void* memmove(void* dest, const void* src, hcpp::size_t n) noexcept {
        auto* d = static_cast<hcpp::u8*>(dest);
        const auto* s = static_cast<const hcpp::u8*>(src);
        if (d < s) {
            for (hcpp::size_t i = 0; i < n; ++i) d[i] = s[i];
        } else {
            for (hcpp::size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
        }
        return dest;
    }

    int memcmp(const void* lhs, const void* rhs, hcpp::size_t n) noexcept {
        const auto* a = static_cast<const hcpp::u8*>(lhs);
        const auto* b = static_cast<const hcpp::u8*>(rhs);
        for (hcpp::size_t i = 0; i < n; ++i)
            if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
        return 0;
    }

}
