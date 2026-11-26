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

}  // namespace hcpp
