#include "kernel/rng/entropy.h"

#include "hcpp/memory.hpp"
#include "hcpp/rng.hpp"
#include "hcpp/sha256.hpp"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/interrupts/idt.h"
#include "kernel/irq.h"
#include "kernel/memory/pmm.h"
#include "kernel/rng/virtio_rng.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

namespace hype::rng {
    namespace {

        constexpr hcpp::size_t pool_size = 256;
        constexpr hcpp::u8 hw_retries = 10;         // bounded RDRAND/RDSEED retry
        constexpr hcpp::u64 reseed_pull_limit = 64;  // draws between reseeds
        constexpr hcpp::u64 reseed_interval_ms = 1000;

        hcpp::u8 pool[pool_size];
        hcpp::size_t mix_offset = 0;
        hcpp::u64 stir_counter = 0;
        hcpp::u64 extract_counter = 0;
        hcpp::u64 pulls_since_reseed = 0;
        hcpp::u64 last_reseed_ms = 0;

        bool rdrand_ok = false;
        bool rdseed_ok = false;
        bool seeded = false;
        bool initialized = false;
        bool markers_printed = false;

        hcpp::u64 read_tsc() noexcept {
            hcpp::u32 low, high;
            asm volatile("rdtsc" : "=a"(low), "=d"(high));
            return (static_cast<hcpp::u64>(high) << 32) | low;
        }

        bool rdrand64(hcpp::u64& out) noexcept {
            for (hcpp::u8 attempt = 0; attempt < hw_retries; ++attempt) {
                hcpp::u8 success;
                asm volatile("rdrand %0\n\tsetc %1" : "=r"(out), "=q"(success));
                if (success != 0) {
                    return true;
                }
            }
            return false;
        }

        bool rdseed64(hcpp::u64& out) noexcept {
            for (hcpp::u8 attempt = 0; attempt < hw_retries; ++attempt) {
                hcpp::u8 success;
                asm volatile("rdseed %0\n\tsetc %1" : "=r"(out), "=q"(success));
                if (success != 0) {
                    return true;
                }
            }
            return false;
        }

        // pool <- pool ^ SHA-256(pool || stir_counter || data), folded
        // 32 bytes at a time at a rotating offset. XOR (not overwrite)
        // preserves entropy already in the pool when the new material
        // turns out weak.
        void pool_mix(const void* data, hcpp::size_t len) noexcept {
            hcpp::Sha256 ctx;
            hcpp::u8 digest[32];
            hcpp::sha256_init(ctx);
            hcpp::sha256_update(ctx, pool, pool_size);
            hcpp::sha256_update(ctx, &stir_counter, sizeof(stir_counter));
            if (data != nullptr && len != 0) {
                hcpp::sha256_update(ctx, data, len);
            }
            hcpp::sha256_final(ctx, digest);
            ++stir_counter;
            for (hcpp::size_t i = 0; i < sizeof(digest); ++i) {
                pool[(mix_offset + i) % pool_size] ^= digest[i];
            }
            mix_offset = (mix_offset + sizeof(digest)) % pool_size;
        }

        // Gathers fresh source material. Weak stirs (PIT ticks, IRQ
        // dispatch count, TSC loop jitter) are collected only when no
        // hardware source exists — they add nothing once RDRAND or the
        // virtio device is live.
        hcpp::size_t collect_sources(hcpp::u8* out, hcpp::size_t cap) noexcept {
            hcpp::size_t n = 0;
            hcpp::u64 value = 0;
            if (rdrand_ok) {
                for (hcpp::u8 i = 0; i < 4 && n + 8 <= cap; ++i) {
                    if (rdrand64(value)) {
                        hcpp::copy(out + n, &value, 8);
                        n += 8;
                    }
                }
            }
            if (rdseed_ok && n + 8 <= cap && rdseed64(value)) {
                hcpp::copy(out + n, &value, 8);
                n += 8;
            }
            if (virtio_rng_ready() && n < cap) {
                const hcpp::size_t want = cap - n > 64 ? 64 : cap - n;
                n += virtio_rng_read(out + n, want);
            }
            if (!rdrand_ok && !rdseed_ok && !virtio_rng_ready()) {
                for (hcpp::u8 i = 0; i < 8 && n + 8 <= cap; ++i) {
                    const hcpp::u64 before = read_tsc();
                    volatile hcpp::u32 spin = 0;
                    for (hcpp::u32 s = 0; s < 64u + hcpp::u32(i) * 37u; ++s) {
                        spin = spin * 33u + s;
                    }
                    hcpp::u64 j = read_tsc() ^ (before >> 1);
                    j ^= interrupts::pit_ticks() * 0x9E3779B97F4A7C15ull;
                    j ^= kernel::irq_dispatch_count() *
                         0xD1B54A32D192ED03ull;
                    j ^= static_cast<hcpp::u64>(spin) << 32;
                    hcpp::copy(out + n, &j, 8);
                    n += 8;
                }
            }
            return n;
        }

        void reseed() noexcept {
            hcpp::u8 material[128];
            const hcpp::size_t n = collect_sources(material, sizeof(material));
            pool_mix(material, n);
            hcpp::set(material, 0, sizeof(material));
            pulls_since_reseed = 0;
            last_reseed_ms = time::uptime_ms();
        }

        void ensure_seeded() noexcept {
            if (seeded) {
                return;
            }
            hcpp::u64 probe = 0;
            rdrand_ok =
                x86::cpuid_has(x86::CpuFeature::Rdrand) && rdrand64(probe);
            rdseed_ok =
                x86::cpuid_has(x86::CpuFeature::Rdseed) && rdseed64(probe);
            // Opportunistic: attaches only once PCI + PMM are up; the
            // periodic reseeds retry via this path too.
            (void)virtio_rng_probe();
            reseed();
            seeded = true;
        }

    }  // namespace

    void entropy_initialize() noexcept {
        ensure_seeded();
        // A later call (the rng.entropy boot stage) may be the first one
        // with PCI + PMM live; retry the virtio probe before reporting.
        (void)virtio_rng_probe();
        if (virtio_rng_ready()) {
            reseed();  // drain the device into the pool at init
        }
        if (!markers_printed && memory::pmm_initialized()) {
            markers_printed = true;
            serial::write_line(rdrand_ok ? "rng rdrand=ok" : "rng rdrand=absent");
            serial::write_line(rdseed_ok ? "rng rdseed=ok" : "rng rdseed=absent");
            serial::write_line(virtio_rng_ready() ? "virtio-rng=ok"
                                                  : "virtio-rng=absent");
            serial::write("rng pool=ok bytes=");
            serial::write_dec32(static_cast<hcpp::u32>(pool_size));
            serial::write("\r\n");
        }
        if (!initialized) {
            initialized = true;
            // Wire the pool through the existing hcpp rng API: every
            // hcpp::rng_fill()/rng_u64() consumer now draws from here.
            hcpp::rng_set_provider(&rng_bytes);
        }
    }

    bool entropy_initialized() noexcept { return initialized; }

    bool rng_has_hardware_source() noexcept {
        return rdrand_ok || rdseed_ok || virtio_rng_ready();
    }

    void rng_bytes(void* buf, hcpp::size_t len) noexcept {
        if (buf == nullptr || len == 0) {
            return;
        }
        if (!initialized) {
            entropy_initialize();
        } else {
            ensure_seeded();
        }
        if (pulls_since_reseed >= reseed_pull_limit ||
            time::uptime_ms() - last_reseed_ms >= reseed_interval_ms) {
            reseed();
        }
        ++pulls_since_reseed;
        auto* out = static_cast<hcpp::u8*>(buf);
        while (len != 0) {
            hcpp::Sha256 ctx;
            hcpp::u8 block[32];
            hcpp::sha256_init(ctx);
            hcpp::sha256_update(ctx, pool, pool_size);
            hcpp::sha256_update(ctx, &extract_counter,
                                sizeof(extract_counter));
            hcpp::sha256_final(ctx, block);
            ++extract_counter;
            const hcpp::size_t take = len < sizeof(block) ? len : sizeof(block);
            hcpp::copy(out, block, take);
            out += take;
            len -= take;
            // Ratchet: mixing the produced block back in makes the pool
            // state depend on everything already emitted.
            pool_mix(block, sizeof(block));
        }
    }

    hcpp::u32 rng_u32() noexcept {
        hcpp::u32 value = 0;
        rng_bytes(&value, sizeof(value));
        return value;
    }

    hcpp::u64 rng_u64() noexcept {
        hcpp::u64 value = 0;
        rng_bytes(&value, sizeof(value));
        return value;
    }

    void entropy_stir(const void* data, hcpp::size_t len) noexcept {
        if (data == nullptr || len == 0) {
            return;
        }
        ensure_seeded();
        pool_mix(data, len);
    }

    bool rng_selftest() noexcept {
        entropy_initialize();
        hcpp::u8 first[256];
        hcpp::u8 second[256];
        rng_bytes(first, sizeof(first));
        rng_bytes(second, sizeof(second));

        bool any_nonzero = false;
        bool all_same = true;
        bool buffers_differ = false;
        hcpp::u32 seen[8] = {};  // bitmap of distinct byte values
        for (hcpp::size_t i = 0; i < sizeof(first); ++i) {
            any_nonzero = any_nonzero || first[i] != 0;
            all_same = all_same && first[i] == first[0];
            buffers_differ = buffers_differ || first[i] != second[i];
            seen[first[i] >> 5] |= 1u << (first[i] & 0x1F);
        }
        hcpp::u32 distinct = 0;
        for (hcpp::size_t i = 0; i < 8; ++i) {
            hcpp::u32 bits = seen[i];
            while (bits != 0) {
                distinct += bits & 1u;
                bits >>= 1;
            }
        }
        // 256 random bytes cover ~162 of 256 values on average; 64 is a
        // floor no real source falls under and no degenerate one reaches.
        const bool ok = any_nonzero && !all_same && buffers_differ &&
                        distinct >= 64;
        serial::write_line(ok ? "rng selftest=ok" : "rng selftest=failed");
        return ok;
    }

}  // namespace hype::rng
