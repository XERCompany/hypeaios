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

    int abs(int x) noexcept { return x < 0 ? -x : x; }

    namespace {

        constexpr double libm_ln2 = 0.6931471805599453;
        constexpr double libm_pi = 3.14159265358979323846;
        constexpr double libm_sqrt2 = 1.4142135623730951;

    }

    double ldexp(double x, int e) noexcept {
        if (x == 0.0 || e == 0) return x;
        union {
            double d;
            hcpp::u64 u;
        } f{};
        while (e > 300) {
            f.u = (hcpp::u64)(1023 + 300) << 52;
            x *= f.d;
            e -= 300;
        }
        while (e < -300) {
            f.u = (hcpp::u64)(1023 - 300) << 52;
            x *= f.d;
            e += 300;
        }
        f.u = (hcpp::u64)(1023 + e) << 52;
        return x * f.d;
    }

    double floor(double x) noexcept {
        constexpr double big = 4503599627370496.0;
        if (x != x || x >= big || x <= -big) return x;
        const hcpp::i64 t = static_cast<hcpp::i64>(x);
        const double d = static_cast<double>(t);
        return d > x ? d - 1.0 : d;
    }

    double exp(double x) noexcept {
        if (x != x) return x;
        if (x > 709.0) return __builtin_huge_val();
        if (x < -745.0) return 0.0;
        const double k = floor(x / libm_ln2 + 0.5);
        const double r = x - k * libm_ln2;
        double sum = 1.0;
        double term = 1.0;
        for (int i = 1; i <= 18; ++i) {
            term *= r / i;
            sum += term;
        }
        return ldexp(sum, static_cast<int>(k));
    }

    double log(double x) noexcept {
        if (x != x || x < 0.0) return __builtin_nan("");
        if (x == 0.0) return -__builtin_huge_val();
        union {
            double d;
            hcpp::u64 u;
        } f{x};
        int e = static_cast<int>((f.u >> 52) & 0x7FF) - 1023;
        f.u = (f.u & ~((hcpp::u64)0x7FF << 52)) | ((hcpp::u64)1023 << 52);
        double m = f.d;
        if (m > libm_sqrt2) {
            m *= 0.5;
            ++e;
        }
        const double t = (m - 1.0) / (m + 1.0);
        const double t2 = t * t;
        double term = t;
        double sum = t;
        for (int i = 3; i <= 33; i += 2) {
            term *= t2;
            sum += term / i;
        }
        return 2.0 * sum + static_cast<double>(e) * libm_ln2;
    }

    double pow(double x, double y) noexcept {
        if (y == 0.0 || x == 1.0) return 1.0;
        const hcpp::i64 yi = static_cast<hcpp::i64>(y);
        if (y == static_cast<double>(yi) && yi >= -64 && yi <= 64) {
            const bool negative = yi < 0;
            hcpp::u64 n = negative ? static_cast<hcpp::u64>(-yi)
                                   : static_cast<hcpp::u64>(yi);
            double base = x;
            double result = 1.0;
            while (n != 0) {
                if ((n & 1) != 0) result *= base;
                base *= base;
                n >>= 1;
            }
            return negative ? 1.0 / result : result;
        }
        if (x <= 0.0) return __builtin_nan("");
        return exp(y * log(x));
    }

    double sin(double x) noexcept {
        if (x != x) return x;
        const double q = floor(x / libm_pi + 0.5);
        const double r = x - q * libm_pi;
        const double r2 = r * r;
        double sum = r;
        double term = r;
        for (int i = 1; i <= 9; ++i) {
            term *= -r2 / (2.0 * i * (2.0 * i + 1.0));
            sum += term;
        }
        return (static_cast<hcpp::i64>(q) & 1) != 0 ? -sum : sum;
    }

    double cos(double x) noexcept {
        if (x != x) return x;
        const double q = floor(x / libm_pi + 0.5);
        const double r = x - q * libm_pi;
        const double r2 = r * r;
        double sum = 1.0;
        double term = 1.0;
        for (int i = 1; i <= 10; ++i) {
            term *= -r2 / ((2.0 * i - 1.0) * (2.0 * i));
            sum += term;
        }
        return (static_cast<hcpp::i64>(q) & 1) != 0 ? -sum : sum;
    }

    void qsort(void *base, hcpp::size_t count, hcpp::size_t size,
               int (*compare)(const void *, const void *)) noexcept {
        auto *bytes = static_cast<hcpp::u8 *>(base);
        for (hcpp::size_t i = 1; i < count; ++i) {
            hcpp::size_t j = i;
            while (j > 0 && compare(bytes + j * size, bytes + (j - 1) * size) < 0) {
                for (hcpp::size_t k = 0; k < size; ++k) {
                    const hcpp::u8 t = bytes[j * size + k];
                    bytes[j * size + k] = bytes[(j - 1) * size + k];
                    bytes[(j - 1) * size + k] = t;
                }
                --j;
            }
        }
    }

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
