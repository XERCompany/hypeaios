#include "hcpp/sha256.hpp"

#include "hcpp/memory.hpp"

namespace hcpp {
    namespace {

        constexpr u32 round_constants[64] = {
            0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4,
            0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE,
            0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F,
            0x4A7484AA, 0x5CB0A9DC, 0x76F988DA, 0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
            0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC,
            0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B,
            0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070, 0x19A4C116,
            0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
            0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7,
            0xC67178F2,
        };

        u32 rotate_right(u32 value, u8 bits) noexcept {
            return (value >> bits) | (value << (32 - bits));
        }

        u32 read_be32(const u8* data) noexcept {
            return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
                   (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
        }

        void write_be32(u8* out, u32 value) noexcept {
            out[0] = static_cast<u8>(value >> 24);
            out[1] = static_cast<u8>(value >> 16);
            out[2] = static_cast<u8>(value >> 8);
            out[3] = static_cast<u8>(value);
        }

        void compress(u32 state[8], const u8 block[64]) noexcept {
            u32 w[64];
            for (u8 i = 0; i < 16; ++i) {
                w[i] = read_be32(block + i * 4);
            }
            for (u8 i = 16; i < 64; ++i) {
                const u32 s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^
                               (w[i - 15] >> 3);
                const u32 s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^
                               (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            u32 a = state[0], b = state[1], c = state[2], d = state[3];
            u32 e = state[4], f = state[5], g = state[6], h = state[7];
            for (u8 i = 0; i < 64; ++i) {
                const u32 sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
                const u32 choose = (e & f) ^ (~e & g);
                const u32 temp1 = h + sum1 + choose + round_constants[i] + w[i];
                const u32 sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
                const u32 majority = (a & b) ^ (a & c) ^ (b & c);
                const u32 temp2 = sum0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        bool digest_equals(const u8* actual, const u8* expected) noexcept {
            u8 diff = 0;
            for (u8 i = 0; i < 32; ++i) {
                diff |= static_cast<u8>(actual[i] ^ expected[i]);
            }
            return diff == 0;
        }

        constexpr u8 digest_empty[32] = {
            0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F,
            0xB9, 0x24, 0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B,
            0x78, 0x52, 0xB8, 0x55,
        };

        constexpr u8 digest_abc[32] = {
            0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE,
            0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61,
            0xF2, 0x00, 0x15, 0xAD,
        };

        constexpr u8 digest_million_a[32] = {
            0xCD, 0xC7, 0x6E, 0x5C, 0x99, 0x14, 0xFB, 0x92, 0x81, 0xA1, 0xC7, 0xE2, 0x84, 0xD7,
            0x3E, 0x67, 0xF1, 0x80, 0x9A, 0x48, 0xA4, 0x97, 0x20, 0x0E, 0x04, 0x6D, 0x39, 0xCC,
            0xC7, 0x11, 0x2C, 0xD0,
        };

        constexpr u8 hmac_rfc4231_case1[32] = {
            0xB0, 0x34, 0x4C, 0x61, 0xD8, 0xDB, 0x38, 0x53, 0x5C, 0xA8, 0xAF, 0xCE, 0xAF, 0x0B,
            0xF1, 0x2B, 0x88, 0x1D, 0xC2, 0x00, 0xC9, 0x83, 0x3D, 0xA7, 0x26, 0xE9, 0x37, 0x6C,
            0x2E, 0x32, 0xCF, 0xF7,
        };

        constexpr u8 pbkdf2_c1[32] = {
            0x12, 0x0F, 0xB6, 0xCF, 0xFC, 0xF8, 0xB3, 0x2C, 0x43, 0xE7, 0x22, 0x52, 0x56, 0xC4,
            0xF8, 0x37, 0xA8, 0x65, 0x48, 0xC9, 0x2C, 0xCC, 0x35, 0x48, 0x08, 0x05, 0x98, 0x7C,
            0xB7, 0x0B, 0xE1, 0x7B,
        };

        constexpr u8 pbkdf2_c2[32] = {
            0xAE, 0x4D, 0x0C, 0x95, 0xAF, 0x6B, 0x46, 0xD3, 0x2D, 0x0A, 0xDF, 0xF9, 0x28, 0xF0,
            0x6D, 0xD0, 0x2A, 0x30, 0x3F, 0x8E, 0xF3, 0xC2, 0x51, 0xDF, 0xD6, 0xE2, 0xD8, 0x5A,
            0x95, 0x47, 0x4C, 0x43,
        };

        constexpr u8 pbkdf2_c4096[32] = {
            0xC5, 0xE4, 0x78, 0xD5, 0x92, 0x88, 0xC8, 0x41, 0xAA, 0x53, 0x0D, 0xB6, 0x84, 0x5C,
            0x4C, 0x8D, 0x96, 0x28, 0x93, 0xA0, 0x01, 0xCE, 0x4E, 0x11, 0xA4, 0x96, 0x38, 0x73,
            0xAA, 0x98, 0x13, 0x4A,
        };

    }  // namespace

    void sha256_init(Sha256& ctx) noexcept {
        ctx.state[0] = 0x6A09E667;
        ctx.state[1] = 0xBB67AE85;
        ctx.state[2] = 0x3C6EF372;
        ctx.state[3] = 0xA54FF53A;
        ctx.state[4] = 0x510E527F;
        ctx.state[5] = 0x9B05688C;
        ctx.state[6] = 0x1F83D9AB;
        ctx.state[7] = 0x5BE0CD19;
        ctx.total = 0;
    }

    void sha256_update(Sha256& ctx, const void* data, size_t size) noexcept {
        const auto* bytes = static_cast<const u8*>(data);
        size_t buffered = static_cast<size_t>(ctx.total % 64);
        ctx.total += size;
        if (buffered != 0) {
            const size_t take = 64 - buffered < size ? 64 - buffered : size;
            copy(ctx.block + buffered, bytes, take);
            buffered += take;
            bytes += take;
            size -= take;
            if (buffered == 64) {
                compress(ctx.state, ctx.block);
                buffered = 0;
            }
        }
        while (size >= 64) {
            compress(ctx.state, bytes);
            bytes += 64;
            size -= 64;
        }
        if (size != 0) {
            copy(ctx.block + buffered, bytes, size);
        }
    }

    void sha256_final(Sha256& ctx, u8 out[32]) noexcept {
        const u64 bit_length = ctx.total * 8;
        u8 tail = 0x80;
        sha256_update(ctx, &tail, 1);
        u8 zero = 0;
        while ((ctx.total % 64) != 56) {
            sha256_update(ctx, &zero, 1);
        }
        u8 length_bytes[8];
        for (u8 i = 0; i < 8; ++i) {
            length_bytes[i] = static_cast<u8>(bit_length >> (56 - i * 8));
        }
        sha256_update(ctx, length_bytes, 8);
        for (u8 i = 0; i < 8; ++i) {
            write_be32(out + i * 4, ctx.state[i]);
        }
        ctx.total = 0;
    }

    void sha256(const void* data, size_t size, u8 out[32]) noexcept {
        Sha256 ctx;
        sha256_init(ctx);
        sha256_update(ctx, data, size);
        sha256_final(ctx, out);
    }

    void hmac_sha256(const void* key, size_t key_size, const void* data, size_t data_size,
                     u8 out[32]) noexcept {
        u8 key_block[64];
        set(key_block, 0, sizeof(key_block));
        if (key_size > 64) {
            sha256(key, key_size, key_block);
        } else if (key_size != 0) {
            copy(key_block, key, key_size);
        }
        u8 inner_pad[64];
        u8 outer_pad[64];
        for (u8 i = 0; i < 64; ++i) {
            inner_pad[i] = static_cast<u8>(key_block[i] ^ 0x36);
            outer_pad[i] = static_cast<u8>(key_block[i] ^ 0x5C);
        }
        Sha256 ctx;
        sha256_init(ctx);
        sha256_update(ctx, inner_pad, 64);
        sha256_update(ctx, data, data_size);
        u8 inner[32];
        sha256_final(ctx, inner);
        sha256_init(ctx);
        sha256_update(ctx, outer_pad, 64);
        sha256_update(ctx, inner, 32);
        sha256_final(ctx, out);
    }

    void pbkdf2_sha256(const void* password, size_t password_size, const void* salt,
                       size_t salt_size, u32 iterations, u8 out[32]) noexcept {
        set(out, 0, 32);
        if (iterations == 0) {
            return;
        }
        u8 salt_block[512 + 4];
        if (salt_size > 512) {
            salt_size = 512;
        }
        copy(salt_block, salt, salt_size);
        salt_block[salt_size] = 0;
        salt_block[salt_size + 1] = 0;
        salt_block[salt_size + 2] = 0;
        salt_block[salt_size + 3] = 1;
        u8 previous[32];
        hmac_sha256(password, password_size, salt_block, salt_size + 4, previous);
        for (u8 i = 0; i < 32; ++i) {
            out[i] = previous[i];
        }
        for (u32 round = 1; round < iterations; ++round) {
            u8 current[32];
            hmac_sha256(password, password_size, previous, 32, current);
            copy(previous, current, 32);
            for (u8 i = 0; i < 32; ++i) {
                out[i] ^= current[i];
            }
        }
    }

    bool sha256_selftest() noexcept {
        u8 out[32];
        sha256("", 0, out);
        if (!digest_equals(out, digest_empty)) {
            return false;
        }
        sha256("abc", 3, out);
        if (!digest_equals(out, digest_abc)) {
            return false;
        }
        Sha256 ctx;
        sha256_init(ctx);
        u8 chunk[100];
        set(chunk, 'a', sizeof(chunk));
        for (u16 i = 0; i < 10000; ++i) {
            sha256_update(ctx, chunk, sizeof(chunk));
        }
        sha256_final(ctx, out);
        if (!digest_equals(out, digest_million_a)) {
            return false;
        }
        u8 key[20];
        set(key, 0x0B, sizeof(key));
        hmac_sha256(key, sizeof(key), "Hi There", 8, out);
        return digest_equals(out, hmac_rfc4231_case1);
    }

    bool pbkdf2_selftest() noexcept {
        u8 out[32];
        pbkdf2_sha256("password", 8, "salt", 4, 1, out);
        if (!digest_equals(out, pbkdf2_c1)) {
            return false;
        }
        pbkdf2_sha256("password", 8, "salt", 4, 2, out);
        if (!digest_equals(out, pbkdf2_c2)) {
            return false;
        }
        pbkdf2_sha256("password", 8, "salt", 4, 4096, out);
        return digest_equals(out, pbkdf2_c4096);
    }

}  // namespace hcpp
