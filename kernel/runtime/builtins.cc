#include "hcpp/types.hpp"
#include "kernel/memory/heap.h"

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

    void* memchr(const void* data, int ch, hcpp::size_t n) noexcept {
        const auto* d = static_cast<const hcpp::u8*>(data);
        const hcpp::u8 v = static_cast<hcpp::u8>(ch);
        for (hcpp::size_t i = 0; i < n; ++i)
            if (d[i] == v) return const_cast<hcpp::u8*>(d + i);
        return nullptr;
    }

    hcpp::size_t strlen(const char* text) noexcept {
        hcpp::size_t n = 0;
        while (text[n] != '\0') ++n;
        return n;
    }

    int strcmp(const char* left, const char* right) noexcept {
        while (*left != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        return static_cast<hcpp::u8>(*left) - static_cast<hcpp::u8>(*right);
    }

    int strncmp(const char* left, const char* right, hcpp::size_t n) noexcept {
        for (hcpp::size_t i = 0; i < n; ++i) {
            if (left[i] != right[i] || left[i] == '\0')
                return static_cast<hcpp::u8>(left[i]) - static_cast<hcpp::u8>(right[i]);
        }
        return 0;
    }

    char* strchr(const char* text, int ch) noexcept {
        for (;; ++text) {
            if (*text == static_cast<char>(ch)) return const_cast<char*>(text);
            if (*text == '\0') return nullptr;
        }
    }

    char* strrchr(const char* text, int ch) noexcept {
        const char* last = nullptr;
        for (; *text != '\0'; ++text)
            if (*text == static_cast<char>(ch)) last = text;
        if (static_cast<char>(ch) == '\0') return const_cast<char*>(text);
        return const_cast<char*>(last);
    }

    char* strstr(const char* text, const char* needle) noexcept {
        const hcpp::size_t length = strlen(needle);
        if (length == 0) return const_cast<char*>(text);
        for (; *text != '\0'; ++text)
            if (strncmp(text, needle, length) == 0) return const_cast<char*>(text);
        return nullptr;
    }

    hcpp::size_t strspn(const char* text, const char* accept) noexcept {
        hcpp::size_t n = 0;
        while (text[n] != '\0' && strchr(accept, text[n]) != nullptr) ++n;
        return n;
    }

    hcpp::size_t strcspn(const char* text, const char* reject) noexcept {
        hcpp::size_t n = 0;
        while (text[n] != '\0' && strchr(reject, text[n]) == nullptr) ++n;
        return n;
    }

    void* malloc(hcpp::size_t n) noexcept { return hype::memory::heap_alloc(n); }

    void* calloc(hcpp::size_t count, hcpp::size_t n) noexcept {
        const hcpp::size_t total = count * n;
        void* block = hype::memory::heap_alloc(total);
        if (block != nullptr) memset(block, 0, total);
        return block;
    }

    void* realloc(void* ptr, hcpp::size_t n) noexcept {
        return hype::memory::heap_realloc(ptr, n);
    }

    void free(void* ptr) noexcept { hype::memory::heap_free(ptr); }

    double fabs(double x) noexcept { return x < 0.0 ? -x : x; }

    struct tm;

    struct tm* localtime(const void* value) noexcept {
        static char storage[64];
        memset(storage, 0, sizeof(storage));
        if (value != nullptr) {
            const hcpp::i64 seconds = *static_cast<const hcpp::i64*>(value);
            const hcpp::i64 days = seconds / 86400;
            const hcpp::i64 within = seconds - days * 86400;
            int* fields = reinterpret_cast<int*>(storage);
            fields[0] = static_cast<int>(within % 60);
            fields[1] = static_cast<int>((within / 60) % 60);
            fields[2] = static_cast<int>((within / 3600) % 24);
            hcpp::i64 z = days + 719468;
            const hcpp::i64 era = (z >= 0 ? z : z - 146096) / 146097;
            const hcpp::u64 doe = static_cast<hcpp::u64>(z - era * 146097);
            const hcpp::u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const hcpp::i64 year = static_cast<hcpp::i64>(yoe) + era * 400;
            const hcpp::u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const hcpp::u64 mp = (5 * doy + 2) / 153;
            fields[3] = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
            fields[4] = static_cast<int>(mp + (mp < 10 ? 2 : -10));
            fields[5] = static_cast<int>(year + (mp < 10 ? 0 : 1) - 1900);
            fields[6] = static_cast<int>((z + 4) % 7);
            fields[7] = static_cast<int>(doy);
        }
        return reinterpret_cast<struct tm*>(storage);
    }

}
