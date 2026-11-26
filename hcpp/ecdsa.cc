#include "hcpp/ecdsa.hpp"

namespace hcpp {
    namespace {

        using u128 = unsigned __int128;

        constexpr u64 prime_p[4] = {
            0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull,
            0xFFFFFFFF00000001ull,
        };
        constexpr u64 order_n[4] = {
            0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull, 0xFFFFFFFFFFFFFFFFull,
            0xFFFFFFFF00000000ull,
        };
        constexpr u64 curve_b[4] = {
            0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull, 0xB3EBBD55769886BCull,
            0x5AC635D8AA3A93E7ull,
        };
        constexpr u64 base_x[4] = {
            0xF4A13945D898C296ull, 0x77037D812DEB33A0ull, 0xF8BCE6E563A440F2ull,
            0x6B17D1F2E12C4247ull,
        };
        constexpr u64 base_y[4] = {
            0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull, 0x8EE7EB4A7C0F9E16ull,
            0x4FE342E2FE1A7F9Bull,
        };

        struct Point {
            u64 x[4];
            u64 y[4];
            u64 z[4];
        };

        int compare(const u64 a[4], const u64 b[4]) noexcept {
            for (int i = 3; i >= 0; --i) {
                if (a[i] != b[i]) {
                    return a[i] > b[i] ? 1 : -1;
                }
            }
            return 0;
        }

        bool is_zero(const u64 a[4]) noexcept {
            return (a[0] | a[1] | a[2] | a[3]) == 0;
        }

        u64 sub_raw(u64 r[4], const u64 a[4], const u64 b[4]) noexcept {
            u64 borrow = 0;
            for (u8 i = 0; i < 4; ++i) {
                const u64 bi = b[i];
                const u64 first = a[i] - bi;
                const u64 result = first - borrow;
                borrow = (a[i] < bi || (borrow != 0 && first == 0)) ? 1 : 0;
                r[i] = result;
            }
            return borrow;
        }

        void add_mod(const u64 a[4], const u64 b[4], const u64 m[4], u64 r[4]) noexcept {
            u64 carry = 0;
            for (u8 i = 0; i < 4; ++i) {
                const u64 sum1 = a[i] + carry;
                const u64 c1 = sum1 < carry ? 1 : 0;
                const u64 sum = sum1 + b[i];
                const u64 c2 = sum < sum1 ? 1 : 0;
                carry = c1 | c2;
                r[i] = sum;
            }
            if (carry != 0 || compare(r, m) >= 0) {
                sub_raw(r, r, m);
            }
        }

        void sub_mod(const u64 a[4], const u64 b[4], const u64 m[4], u64 r[4]) noexcept {
            const u64 borrow = sub_raw(r, a, b);
            if (borrow != 0) {
                u64 carry = 0;
                for (u8 i = 0; i < 4; ++i) {
                    const u64 sum1 = r[i] + carry;
                    const u64 c1 = sum1 < carry ? 1 : 0;
                    const u64 sum = sum1 + m[i];
                    const u64 c2 = sum < sum1 ? 1 : 0;
                    carry = c1 | c2;
                    r[i] = sum;
                }
            }
        }

        void mul_wide(const u64 a[4], const u64 b[4], u64 t[8]) noexcept {
            for (u8 i = 0; i < 8; ++i) {
                t[i] = 0;
            }
            for (u8 i = 0; i < 4; ++i) {
                u128 carry = 0;
                for (u8 j = 0; j < 4; ++j) {
                    const u128 cur =
                        static_cast<u128>(a[i]) * b[j] + t[i + j] + carry;
                    t[i + j] = static_cast<u64>(cur);
                    carry = cur >> 64;
                }
                t[i + 4] = static_cast<u64>(carry);
            }
        }

        void reduce_wide(const u64 x[8], const u64 m[4], u64 r[4]) noexcept {
            u64 rem[5] = {0, 0, 0, 0, 0};
            for (int i = 511; i >= 0; --i) {
                const u64 bit = (x[i >> 6] >> (i & 63)) & 1;
                for (int k = 4; k > 0; --k) {
                    rem[k] = (rem[k] << 1) | (rem[k - 1] >> 63);
                }
                rem[0] = (rem[0] << 1) | bit;
                if (rem[4] != 0 || compare(rem, m) >= 0) {
                    u64 borrow = 0;
                    for (u8 k = 0; k < 4; ++k) {
                        const u64 mk = m[k];
                        const u64 first = rem[k] - mk;
                        const u64 result = first - borrow;
                        borrow = (rem[k] < mk || (borrow != 0 && first == 0)) ? 1 : 0;
                        rem[k] = result;
                    }
                    rem[4] -= borrow;
                }
            }
            for (u8 i = 0; i < 4; ++i) {
                r[i] = rem[i];
            }
        }

        void mul_mod(const u64 a[4], const u64 b[4], const u64 m[4], u64 r[4]) noexcept {
            u64 product[8];
            mul_wide(a, b, product);
            reduce_wide(product, m, r);
        }

        void inv_mod(const u64 a[4], const u64 m[4], u64 r[4]) noexcept {
            u64 base[4];
            for (u8 i = 0; i < 4; ++i) {
                base[i] = a[i];
            }
            u64 exponent[4];
            exponent[0] = m[0] - 2;
            exponent[1] = m[1];
            exponent[2] = m[2];
            exponent[3] = m[3];
            if (m[0] < 2) {
                u64 borrow = m[0] < 2 ? 1 : 0;
                for (u8 i = 1; i < 4 && borrow != 0; ++i) {
                    const u64 before = exponent[i];
                    exponent[i] = before - borrow;
                    borrow = before < borrow ? 1 : 0;
                }
            }
            r[0] = 1;
            r[1] = 0;
            r[2] = 0;
            r[3] = 0;
            for (int i = 255; i >= 0; --i) {
                mul_mod(r, r, m, r);
                if (((exponent[i >> 6] >> (i & 63)) & 1) != 0) {
                    mul_mod(r, base, m, r);
                }
            }
        }

        void from_bytes(const u8 bytes[32], u64 out[4]) noexcept {
            for (u8 i = 0; i < 4; ++i) {
                u64 value = 0;
                for (u8 j = 0; j < 8; ++j) {
                    value = (value << 8) | bytes[(3 - i) * 8 + j];
                }
                out[i] = value;
            }
        }

        void point_double(const Point& p, Point& r) noexcept {
            u64 delta[4], gamma[4], beta[4], alpha[4], t[4];
            mul_mod(p.z, p.z, prime_p, delta);
            mul_mod(p.y, p.y, prime_p, gamma);
            mul_mod(p.x, gamma, prime_p, beta);
            u64 u[4];
            sub_mod(p.x, delta, prime_p, u);
            add_mod(p.x, delta, prime_p, t);
            mul_mod(u, t, prime_p, alpha);
            add_mod(alpha, alpha, prime_p, t);
            add_mod(t, alpha, prime_p, alpha);
            mul_mod(alpha, alpha, prime_p, r.x);
            add_mod(beta, beta, prime_p, t);
            add_mod(t, t, prime_p, u);
            add_mod(u, u, prime_p, t);
            sub_mod(r.x, t, prime_p, r.x);
            add_mod(p.y, p.z, prime_p, r.z);
            mul_mod(r.z, r.z, prime_p, r.z);
            sub_mod(r.z, gamma, prime_p, r.z);
            sub_mod(r.z, delta, prime_p, r.z);
            mul_mod(gamma, gamma, prime_p, t);
            add_mod(t, t, prime_p, t);
            add_mod(t, t, prime_p, t);
            add_mod(t, t, prime_p, t);
            sub_mod(u, r.x, prime_p, u);
            mul_mod(alpha, u, prime_p, r.y);
            sub_mod(r.y, t, prime_p, r.y);
        }

        bool point_add(const Point& a, const Point& b, Point& r) noexcept {
            const bool a_inf = is_zero(a.z);
            const bool b_inf = is_zero(b.z);
            if (a_inf && b_inf) {
                r = a;
                return true;
            }
            if (a_inf) {
                r = b;
                return true;
            }
            if (b_inf) {
                r = a;
                return true;
            }
            u64 z1z1[4], z2z2[4], u1[4], u2[4], s1[4], s2[4], h[4], t[4];
            mul_mod(a.z, a.z, prime_p, z1z1);
            mul_mod(b.z, b.z, prime_p, z2z2);
            mul_mod(a.x, z2z2, prime_p, u1);
            mul_mod(b.x, z1z1, prime_p, u2);
            mul_mod(b.z, z2z2, prime_p, t);
            mul_mod(a.y, t, prime_p, s1);
            mul_mod(a.z, z1z1, prime_p, t);
            mul_mod(b.y, t, prime_p, s2);
            sub_mod(u2, u1, prime_p, h);
            if (is_zero(h)) {
                if (compare(s1, s2) == 0) {
                    point_double(a, r);
                } else {
                    r.x[0] = r.x[1] = r.x[2] = r.x[3] = 0;
                    r.y[0] = 1;
                    r.y[1] = r.y[2] = r.y[3] = 0;
                    r.z[0] = r.z[1] = r.z[2] = r.z[3] = 0;
                }
                return true;
            }
            u64 i[4], j[4], v[4], rr[4];
            add_mod(h, h, prime_p, t);
            mul_mod(t, t, prime_p, i);
            mul_mod(h, i, prime_p, j);
            sub_mod(s2, s1, prime_p, rr);
            add_mod(rr, rr, prime_p, rr);
            mul_mod(u1, i, prime_p, v);
            mul_mod(rr, rr, prime_p, r.x);
            sub_mod(r.x, j, prime_p, r.x);
            sub_mod(r.x, v, prime_p, r.x);
            sub_mod(r.x, v, prime_p, r.x);
            sub_mod(v, r.x, prime_p, t);
            mul_mod(rr, t, prime_p, r.y);
            mul_mod(s1, j, prime_p, t);
            add_mod(t, t, prime_p, t);
            sub_mod(r.y, t, prime_p, r.y);
            add_mod(a.z, b.z, prime_p, r.z);
            mul_mod(r.z, r.z, prime_p, r.z);
            sub_mod(r.z, z1z1, prime_p, r.z);
            sub_mod(r.z, z2z2, prime_p, r.z);
            mul_mod(r.z, h, prime_p, r.z);
            return true;
        }

        void scalar_mul(const u64 k[4], const u64 px[4], const u64 py[4], Point& r) noexcept {
            Point base;
            for (u8 i = 0; i < 4; ++i) {
                base.x[i] = px[i];
                base.y[i] = py[i];
                base.z[i] = 0;
                r.x[i] = 0;
                r.z[i] = 0;
            }
            base.z[0] = 1;
            r.y[0] = 1;
            r.y[1] = r.y[2] = r.y[3] = 0;
            for (int i = 255; i >= 0; --i) {
                point_double(r, r);
                if (((k[i >> 6] >> (i & 63)) & 1) != 0) {
                    point_add(r, base, r);
                }
            }
        }

        bool on_curve(const u64 x[4], const u64 y[4]) noexcept {
            u64 left[4], right[4], t[4];
            mul_mod(y, y, prime_p, left);
            mul_mod(x, x, prime_p, right);
            mul_mod(right, x, prime_p, right);
            add_mod(x, x, prime_p, t);
            add_mod(t, x, prime_p, t);
            sub_mod(right, t, prime_p, right);
            add_mod(right, curve_b, prime_p, right);
            return compare(left, right) == 0;
        }

        constexpr u8 selftest_qx[32] = {
            0x23, 0x73, 0xEA, 0x59, 0x77, 0x4C, 0x9F, 0xB3, 0x4C, 0x43, 0x6D, 0x7A, 0x7F, 0x01,
            0xBF, 0xDB, 0x7E, 0x8B, 0x53, 0xFB, 0x3F, 0xF6, 0x4C, 0x9C, 0x37, 0x1C, 0xEA, 0x4E,
            0x4B, 0xAE, 0x6E, 0x1B,
        };
        constexpr u8 selftest_qy[32] = {
            0x05, 0x87, 0xCD, 0x4B, 0xED, 0xAA, 0x57, 0xDA, 0x14, 0x89, 0x37, 0x49, 0x2C, 0x42,
            0xF1, 0x0C, 0xAE, 0xC6, 0x60, 0x63, 0x7E, 0x22, 0x36, 0x6A, 0x28, 0xCE, 0x86, 0x0D,
            0x34, 0xA6, 0x07, 0x64,
        };
        constexpr u8 selftest_hash[32] = {
            0x71, 0x83, 0x62, 0xE6, 0xE0, 0x94, 0x34, 0xAC, 0xA8, 0xA0, 0x7B, 0x37, 0x9E, 0xC1,
            0xC7, 0x20, 0x6D, 0x1F, 0xDE, 0x61, 0xAF, 0xED, 0xE2, 0x9F, 0x18, 0xA7, 0x1A, 0x46,
            0x30, 0x37, 0x93, 0xBD,
        };
        constexpr u8 selftest_r[32] = {
            0xAC, 0x42, 0xBC, 0x60, 0x37, 0x63, 0xD9, 0xD5, 0x24, 0x0F, 0x14, 0x06, 0x2B, 0xFE,
            0xA9, 0x8E, 0x15, 0xB1, 0x8E, 0xD0, 0x1A, 0x40, 0x56, 0x6B, 0x54, 0x6B, 0x9E, 0x34,
            0xC4, 0x84, 0xA9, 0x00,
        };
        constexpr u8 selftest_s[32] = {
            0x4F, 0x9A, 0x08, 0x46, 0x11, 0xA4, 0x64, 0x53, 0x9E, 0xB9, 0xB4, 0xAE, 0x90, 0x69,
            0x19, 0x4A, 0xD5, 0x5D, 0xDE, 0xB9, 0x4C, 0xD5, 0x21, 0x35, 0xCD, 0xC7, 0xAE, 0x27,
            0xA9, 0x62, 0xA4, 0x70,
        };

    }  // namespace

    bool ecdsa_p256_verify(const u8 pubkey[64], const u8 hash[32], const u8 sig_r[32],
                           const u8 sig_s[32]) noexcept {
        u64 r[4], s[4], qx[4], qy[4], z[4];
        from_bytes(sig_r, r);
        from_bytes(sig_s, s);
        from_bytes(pubkey, qx);
        from_bytes(pubkey + 32, qy);
        from_bytes(hash, z);
        if (is_zero(r) || compare(r, order_n) >= 0 || is_zero(s) || compare(s, order_n) >= 0) {
            return false;
        }
        if (compare(qx, prime_p) >= 0 || compare(qy, prime_p) >= 0 || !on_curve(qx, qy)) {
            return false;
        }
        u64 w[4], u1[4], u2[4];
        inv_mod(s, order_n, w);
        mul_mod(z, w, order_n, u1);
        mul_mod(r, w, order_n, u2);
        Point sum_g, sum_q, result;
        scalar_mul(u1, base_x, base_y, sum_g);
        scalar_mul(u2, qx, qy, sum_q);
        point_add(sum_g, sum_q, result);
        if (is_zero(result.z)) {
            return false;
        }
        u64 z2[4], x[4];
        mul_mod(result.z, result.z, prime_p, z2);
        inv_mod(z2, prime_p, z2);
        mul_mod(result.x, z2, prime_p, x);
        if (compare(x, order_n) >= 0) {
            sub_raw(x, x, order_n);
        }
        return compare(x, r) == 0;
    }

    bool ecdsa_selftest() noexcept {
        u8 pubkey[64];
        for (u8 i = 0; i < 32; ++i) {
            pubkey[i] = selftest_qx[i];
            pubkey[32 + i] = selftest_qy[i];
        }
        if (!ecdsa_p256_verify(pubkey, selftest_hash, selftest_r, selftest_s)) {
            return false;
        }
        u8 bad[64];
        for (u8 i = 0; i < 64; ++i) {
            bad[i] = pubkey[i];
        }
        bad[0] ^= 0x01;
        if (ecdsa_p256_verify(bad, selftest_hash, selftest_r, selftest_s)) {
            return false;
        }
        u8 bad_hash[32];
        for (u8 i = 0; i < 32; ++i) {
            bad_hash[i] = selftest_hash[i];
        }
        bad_hash[31] ^= 0x01;
        if (ecdsa_p256_verify(pubkey, bad_hash, selftest_r, selftest_s)) {
            return false;
        }
        u8 bad_r[32];
        for (u8 i = 0; i < 32; ++i) {
            bad_r[i] = selftest_r[i];
        }
        bad_r[0] ^= 0x80;
        if (ecdsa_p256_verify(pubkey, selftest_hash, bad_r, selftest_s)) {
            return false;
        }
        u8 bad_s[32];
        for (u8 i = 0; i < 32; ++i) {
            bad_s[i] = selftest_s[i];
        }
        bad_s[31] ^= 0x01;
        if (ecdsa_p256_verify(pubkey, selftest_hash, selftest_r, bad_s)) {
            return false;
        }
        u8 zero[32] = {};
        if (ecdsa_p256_verify(pubkey, selftest_hash, zero, selftest_s)) {
            return false;
        }
        if (ecdsa_p256_verify(pubkey, selftest_hash, selftest_r, zero)) {
            return false;
        }
        return true;
    }

}  // namespace hcpp
