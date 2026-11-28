#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

    enum class RngBackend : u8 {
        Rdrand,
        Tsc,
    };

    RngBackend rng_backend() noexcept;
    u64 rng_u64() noexcept;
    void rng_fill(void* out, size_t bytes) noexcept;
    bool rng_selftest() noexcept;

    // Optional output provider hook. The kernel registers its SHA-256
    // entropy pool (kernel/rng/entropy.cc) at boot; once set, rng_u64()
    // and rng_fill() draw from it instead of the local RDRAND/TSC
    // backend. Passing nullptr restores the local backend. Standalone
    // (non-kernel) users never set a provider and see no change.
    using RngProvider = void (*)(void* out, size_t bytes) noexcept;
    void rng_set_provider(RngProvider provider) noexcept;

}  // namespace hcpp
