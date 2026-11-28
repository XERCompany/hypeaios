#pragma once

#include "hcpp/types.hpp"

// HypeAI ZText — in-kernel DEFLATE (RFC 1951) compressor/decompressor
// with zlib (RFC 1950) and gzip (RFC 1952) container wrappers.
// Freestanding: no libc, no zlib, no dynamic allocation inside the
// codec itself (the LZ77 workspace is a bounded static arena).
// Issue #176.

namespace hype::ztext {

    // CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320) as used by
    // gzip/png/zlib. crc32("") == 0.
    hcpp::u32 ztext_crc32(const void *data, hcpp::size_t size) noexcept;

    // Adler-32 (RFC 1950). adler32("") == 1.
    hcpp::u32 ztext_adler32(const void *data, hcpp::size_t size) noexcept;

    // Raw DEFLATE stream decode. `dst` doubles as the 32KB LZ77 window
    // (matches copy from already-produced output). `out_len` receives
    // the number of bytes written. Returns false on malformed input,
    // truncated input, or insufficient dst_cap.
    bool ztext_inflate(hcpp::u8 *dst, hcpp::size_t dst_cap,
                       const hcpp::u8 *src, hcpp::size_t src_len,
                       hcpp::size_t &out_len) noexcept;

    // Raw DEFLATE stream encode. Emits a fixed-Huffman block fed by a
    // greedy hash-chain LZ77 match finder (32KB window); falls back to
    // stored blocks when the coded form would not fit dst_cap.
    bool ztext_deflate(hcpp::u8 *dst, hcpp::size_t dst_cap,
                       const hcpp::u8 *src, hcpp::size_t src_len,
                       hcpp::size_t &out_len) noexcept;

    // Worst-case dst_cap that guarantees ztext_deflate cannot fail for
    // space reasons (covers both coded and stored encodings).
    hcpp::size_t ztext_deflate_bound(hcpp::size_t src_len) noexcept;

    // RFC 1950 zlib stream: 2-byte header + deflate + big-endian adler32.
    bool ztext_zlib_decode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept;
    bool ztext_zlib_encode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept;

    // RFC 1952 gzip member: 10-byte header + deflate + crc32/isize.
    // Decode honours FEXTRA/FNAME/FCOMMENT/FHCRC header flags and
    // rejects reserved flag bits.
    bool ztext_gzip_decode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept;
    bool ztext_gzip_encode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept;

    // Boot selftest: checksum vectors, cross-check streams produced by
    // a real zlib, encode/decode round-trips including >64KB data that
    // exercises the LZ77 window. Prints `ztext <key>=ok|fail` markers.
    bool ztext_selftest() noexcept;

}  // namespace hype::ztext
