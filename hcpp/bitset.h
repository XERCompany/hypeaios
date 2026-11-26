#pragma once
#include "hcpp/types.hpp"
// Non-template fixed variants keep implementation outside headers.
namespace hcpp {
    struct Bitset256 {
        u64 words[4];
    };
    void bitset256_clear(Bitset256& bits) noexcept;
    void bitset256_set(Bitset256& bits, u16 bit) noexcept;
    void bitset256_reset(Bitset256& bits, u16 bit) noexcept;
    bool bitset256_test(const Bitset256& bits, u16 bit) noexcept;
    u16 bitset256_count(const Bitset256& bits) noexcept;
    u16 bitset256_first_set(const Bitset256& bits) noexcept;
}  // namespace hcpp
