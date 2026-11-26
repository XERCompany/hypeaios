#include "hcpp/rng.hpp"

namespace hcpp {
    namespace {

        constexpr u8 rdrand_max_retries = 16;

        RngBackend active_backend = RngBackend::Tsc;
        bool probed = false;
        bool seeded = false;
        u64 xorshift_state[4];
        u64 draw_counter = 0;

        u64 read_tsc() noexcept {
            u32 low, high;
            asm volatile("rdtsc" : "=a"(low), "=d"(high));
            return (static_cast<u64>(high) << 32) | low;
        }

        u64 mix64(u64 value) noexcept {
            value ^= value >> 30;
            value *= 0xBF58476D1CE4E5B9ull;
            value ^= value >> 27;
            value *= 0x94D049BB133111EBull;
            value ^= value >> 31;
            return value;
        }

        void probe() noexcept {
            u32 eax, ebx, ecx, edx;
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
            active_backend = (ecx & (1u << 30)) != 0 ? RngBackend::Rdrand : RngBackend::Tsc;
            probed = true;
        }

        bool rdrand_u64(u64& out) noexcept {
            for (u8 attempt = 0; attempt < rdrand_max_retries; ++attempt) {
                u8 success;
                asm volatile("rdrand %0\n\tsetc %1" : "=r"(out), "=q"(success));
                if (success != 0) {
                    return true;
                }
            }
            return false;
        }

        void seed() noexcept {
            const volatile u8* bios_data = reinterpret_cast<const volatile u8*>(0x400);
            u64 seed_a = read_tsc();
            u64 spin = 0;
            for (u16 i = 0; i < 997; ++i) {
                spin = spin * 33 + bios_data[(i * 37) & 0xFF];
            }
            const u64 seed_b = read_tsc();
            u64 bios_ticks = 0;
            for (u8 i = 0; i < 8; ++i) {
                bios_ticks = (bios_ticks << 8) | bios_data[0x6C + (i & 3)];
            }
            xorshift_state[0] =
                mix64(seed_a ^ spin) ^ mix64(bios_ticks ^ 0x9E3779B97F4A7C15ull);
            xorshift_state[1] =
                mix64(seed_b ^ (seed_a << 1)) ^ mix64(spin ^ 0xD1B54A32D192ED03ull);
            xorshift_state[2] = mix64(bios_ticks ^ seed_b ^ 0x8BB84B93962EACC9ull);
            xorshift_state[3] = mix64(read_tsc() ^ seed_a ^ 0x2545F4914F6CDD1Dull);
            xorshift_state[0] ^= xorshift_state[2];
            xorshift_state[1] ^= xorshift_state[3];
            if ((xorshift_state[0] | xorshift_state[1]) == 0) {
                xorshift_state[0] = 0x0123456789ABCDEFull;
                xorshift_state[1] = 0xFEDCBA9876543210ull;
            }
            seeded = true;
        }

        u64 xorshift_next() noexcept {
            u64 x = xorshift_state[0];
            const u64 y = xorshift_state[1];
            xorshift_state[0] = y;
            x ^= x << 23;
            xorshift_state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
            return xorshift_state[1] + y;
        }

        u64 tsc_draw() noexcept {
            if (!seeded) {
                seed();
            }
            const u64 sample = mix64(read_tsc() ^ (++draw_counter * 0x9E3779B97F4A7C15ull));
            xorshift_state[0] ^= sample;
            xorshift_state[2] ^= sample >> 1;
            return mix64(xorshift_next() ^ sample);
        }

    }  // namespace

    RngBackend rng_backend() noexcept {
        if (!probed) {
            probe();
        }
        return active_backend;
    }

    u64 rng_u64() noexcept {
        if (!probed) {
            probe();
        }
        if (active_backend == RngBackend::Rdrand) {
            u64 value;
            if (rdrand_u64(value)) {
                return value;
            }
            active_backend = RngBackend::Tsc;
        }
        return tsc_draw();
    }

    void rng_fill(void* out, size_t bytes) noexcept {
        auto* target = static_cast<u8*>(out);
        while (bytes != 0) {
            const u64 value = rng_u64();
            const size_t take = bytes < 8 ? bytes : 8;
            for (size_t i = 0; i < take; ++i) {
                target[i] = static_cast<u8>(value >> (i * 8));
            }
            target += take;
            bytes -= take;
        }
    }

    bool rng_selftest() noexcept {
        u64 values[16];
        for (u8 i = 0; i < 16; ++i) {
            values[i] = rng_u64();
        }
        bool any_nonzero = false;
        bool all_same = true;
        for (u8 i = 0; i < 16; ++i) {
            any_nonzero = any_nonzero || values[i] != 0;
            all_same = all_same && values[i] == values[0];
        }
        if (!any_nonzero || all_same) {
            return false;
        }
        u8 buffer[64];
        rng_fill(buffer, sizeof(buffer));
        u8 second[64];
        rng_fill(second, sizeof(second));
        u8 diff = 0;
        for (u8 i = 0; i < 64; ++i) {
            diff |= static_cast<u8>(buffer[i] ^ second[i]);
        }
        return diff != 0;
    }

}  // namespace hcpp
