#pragma once

#include "hcpp/types.hpp"

// System entropy pool — issue #121 (docs/research/gaps-hardware.md
// section 5.5, top value/effort item).
//
// A 256-byte pool stirred through SHA-256 (hcpp). Entropy sources, in
// priority order:
//   a. RDRAND  (CPUID.01H:ECX bit30, bounded 10-try CF-checked draws)
//   b. RDSEED  (CPUID.07H.0:EBX bit18, seed-grade draws)
//   c. virtio-rng PCI device (kernel/rng/virtio_rng.cc on
//      kernel/virtio/vring.h) drained into the pool at init
//   d. weak stirs — PIT tick count, IRQ dispatch count, TSC loop
//      jitter — used only when no hardware source exists.
//
// rng_bytes() serves output as SHA-256(pool || counter) blocks and
// mixes each produced block back into the pool, so state ratchets
// forward. Reseeds happen every reseed_pull_limit draws or every
// reseed_interval_ms of uptime, whichever comes first.

namespace hype::rng {

    // One-time bring-up: probes RDRAND/RDSEED, attaches virtio-rng,
    // seeds the pool and registers it as the hcpp::rng provider so
    // hcpp::rng_fill()/rng_u64() consumers (ecdsa keygen, hello
    // credentials, hypevfs) draw from the pool. Prints the boot
    // markers "rng rdrand=..", "rng rdseed=..", "virtio-rng=..",
    // "rng pool=..". Idempotent; safe to call again after init.
    void entropy_initialize() noexcept;

    bool entropy_initialized() noexcept;

    // True when RDRAND, RDSEED or virtio-rng produced data.
    bool rng_has_hardware_source() noexcept;

    // Fills buf with len bytes from the pool, reseeding periodically.
    // Lazily seeds on first call so early callers still get output.
    void rng_bytes(void* buf, hcpp::size_t len) noexcept;
    hcpp::u32 rng_u32() noexcept;
    hcpp::u64 rng_u64() noexcept;

    // Mixes caller-supplied entropy (timing jitter, interrupt/event
    // data) into the pool. Safe at any time, including before
    // entropy_initialize().
    void entropy_stir(const void* data, hcpp::size_t len) noexcept;

    // Draws two buffers, checks non-constant output, cross-buffer
    // difference and byte-value diversity. Prints "rng selftest=ok"
    // or "rng selftest=failed" on serial.
    bool rng_selftest() noexcept;

}  // namespace hype::rng
