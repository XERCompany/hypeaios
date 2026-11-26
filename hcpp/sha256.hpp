#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

    struct Sha256 {
        u32 state[8];
        u64 total;
        u8 block[64];
    };

    void sha256_init(Sha256& ctx) noexcept;
    void sha256_update(Sha256& ctx, const void* data, size_t size) noexcept;
    void sha256_final(Sha256& ctx, u8 out[32]) noexcept;
    void sha256(const void* data, size_t size, u8 out[32]) noexcept;
    void hmac_sha256(const void* key, size_t key_size, const void* data, size_t data_size,
                     u8 out[32]) noexcept;
    void pbkdf2_sha256(const void* password, size_t password_size, const void* salt,
                       size_t salt_size, u32 iterations, u8 out[32]) noexcept;
    bool sha256_selftest() noexcept;
    bool pbkdf2_selftest() noexcept;

}  // namespace hcpp
