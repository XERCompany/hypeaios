#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

    bool ecdsa_p256_verify(const u8 pubkey[64], const u8 hash[32], const u8 sig_r[32],
                           const u8 sig_s[32]) noexcept;
    bool ecdsa_selftest() noexcept;

}  // namespace hcpp
