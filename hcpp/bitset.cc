#include "hcpp/bitset.h"
namespace hcpp {
    void bitset256_clear(Bitset256& bits) noexcept {
        for (auto& w : bits.words) w = 0;
    }
    void bitset256_set(Bitset256& bits, u16 bit) noexcept {
        if (bit < 256) bits.words[bit / 64] |= u64(1) << (bit % 64);
    }
    void bitset256_reset(Bitset256& bits, u16 bit) noexcept {
        if (bit < 256) bits.words[bit / 64] &= ~(u64(1) << (bit % 64));
    }
    bool bitset256_test(const Bitset256& bits, u16 bit) noexcept {
        return bit < 256 && (bits.words[bit / 64] & (u64(1) << (bit % 64))) != 0;
    }
    u16 bitset256_count(const Bitset256& bits) noexcept {
        u16 n = 0;
        for (u64 w : bits.words)
            while (w) {
                n = static_cast<u16>(n + (w & 1));
                w >>= 1;
            }
        return n;
    }
    u16 bitset256_first_set(const Bitset256& bits) noexcept {
        for (u16 word = 0; word < 4; ++word) {
            if (bits.words[word] == 0) continue;
            for (u16 bit = 0; bit < 64; ++bit)
                if ((bits.words[word] & (u64(1) << bit)) != 0) return word * 64 + bit;
        }
        return 0xFFFF;
    }
}  // namespace hcpp
