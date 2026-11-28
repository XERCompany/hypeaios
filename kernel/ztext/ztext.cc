#include "kernel/ztext/ztext.h"

// ZText codec core: RFC 1951 DEFLATE decoder (stored / fixed / dynamic
// blocks, canonical count-limited Huffman tables, 32KB LZ77 window via
// the output buffer) plus a real encoder — greedy hash-chain LZ77 match
// finder emitting a fixed-Huffman block, with a stored-block fallback —
// and zlib/gzip framing. All state is bounded; no heap, no libc.

namespace hype::ztext {

    namespace {

        constexpr int max_bits = 15;        // RFC 1951 max code length
        constexpr int max_litlen = 288;     // literal/length alphabet
        constexpr int max_dist = 30;        // distance alphabet
        constexpr int max_clen = 19;        // code-length alphabet
        constexpr hcpp::size_t window_size = 32768;
        constexpr hcpp::u32 adler_mod = 65521;

        const hcpp::u16 len_base[29] = {
            3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
            15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
            67,  83,  99,  115, 131, 163, 195, 227, 258,
        };
        const hcpp::u8 len_extra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
        };
        const hcpp::u16 dist_base[max_dist] = {
            1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
            33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
            1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
        };
        const hcpp::u8 dist_extra[max_dist] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
        };
        const hcpp::u8 clen_order[max_clen] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
        };

        // ------------------------------------------------------------------
        // CRC-32 (reflected poly 0xEDB88320) — compile-time table.
        // ------------------------------------------------------------------

        struct CrcTable {
            hcpp::u32 entry[256];
            constexpr CrcTable() : entry{} {
                for (hcpp::u32 i = 0; i < 256; ++i) {
                    hcpp::u32 c = i;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
                    entry[i] = c;
                }
            }
        };
        constexpr CrcTable crc_table{};

        // ------------------------------------------------------------------
        // Inflater
        // ------------------------------------------------------------------

        // LSB-first bit reader over the source buffer.
        struct BitReader {
            const hcpp::u8 *data;
            hcpp::size_t size;
            hcpp::size_t pos;   // next byte to pull into bitbuf
            hcpp::u32 bitbuf;   // pending bits, LSB = next stream bit
            hcpp::u32 bitcnt;   // valid bits in bitbuf (invariant: < 8 after bits())
        };

        int bits(BitReader &reader, int need) noexcept {
            while (reader.bitcnt < static_cast<hcpp::u32>(need)) {
                if (reader.pos >= reader.size) return -1;
                reader.bitbuf |=
                    static_cast<hcpp::u32>(reader.data[reader.pos++]) << reader.bitcnt;
                reader.bitcnt += 8;
            }
            const int value =
                static_cast<int>(reader.bitbuf & ((1u << need) - 1u));
            reader.bitbuf >>= need;
            reader.bitcnt -= static_cast<hcpp::u32>(need);
            return value;
        }

        // Canonical Huffman decode table: symbol counts per code length
        // (bounded at 15) plus symbols sorted by (length, symbol) order.
        struct Huffman {
            hcpp::u16 count[max_bits + 1];
            hcpp::u16 symbol[max_litlen];
        };

        // Build a canonical table from code lengths. Returns the number
        // of unused codes (>= 0, incomplete-but-decodable) or < 0 for an
        // over-subscribed / invalid set.
        int huff_build(Huffman &huff, const hcpp::u8 *lengths, int n) noexcept {
            for (int len = 0; len <= max_bits; ++len) huff.count[len] = 0;
            for (int sym = 0; sym < n; ++sym) ++huff.count[lengths[sym]];
            if (huff.count[0] == n) return 0;  // no symbols coded at all
            int left = 1;
            for (int len = 1; len <= max_bits; ++len) {
                left <<= 1;
                left -= huff.count[len];
                if (left < 0) return -1;  // over-subscribed
            }
            hcpp::u16 offsets[max_bits + 1];
            offsets[1] = 0;
            for (int len = 1; len < max_bits; ++len)
                offsets[len + 1] =
                    static_cast<hcpp::u16>(offsets[len] + huff.count[len]);
            for (int sym = 0; sym < n; ++sym)
                if (lengths[sym] != 0)
                    huff.symbol[offsets[lengths[sym]]++] =
                        static_cast<hcpp::u16>(sym);
            return left;
        }

        int huff_decode(BitReader &reader, const Huffman &huff) noexcept {
            int code = 0;    // code bits read so far (reversed)
            int first = 0;   // first canonical code of this length
            int index = 0;   // index of first code of this length in symbol[]
            for (int len = 1; len <= max_bits; ++len) {
                const int bit = bits(reader, 1);
                if (bit < 0) return -1;
                code |= bit;
                const int count = huff.count[len];
                if (code - first < count)
                    return huff.symbol[index + (code - first)];
                index += count;
                first = (first + count) << 1;
                code <<= 1;
            }
            return -1;
        }

        struct OutBuf {
            hcpp::u8 *data;
            hcpp::size_t cap;
            hcpp::size_t pos;
        };

        // LZ77 back-reference copy; the output stream is the window, so
        // overlapping copies replicate runs correctly.
        bool copy_match(OutBuf &out, hcpp::size_t dist,
                        hcpp::size_t len) noexcept {
            if (dist == 0 || dist > out.pos || len > out.cap - out.pos)
                return false;
            for (hcpp::size_t i = 0; i < len; ++i) {
                out.data[out.pos] = out.data[out.pos - dist];
                ++out.pos;
            }
            return true;
        }

        bool inflate_block_codes(BitReader &reader, OutBuf &out,
                                 const Huffman &litlen,
                                 const Huffman &dist) noexcept {
            for (;;) {
                const int sym = huff_decode(reader, litlen);
                if (sym < 0) return false;
                if (sym < 256) {
                    if (out.pos >= out.cap) return false;
                    out.data[out.pos++] = static_cast<hcpp::u8>(sym);
                    continue;
                }
                if (sym == 256) return true;  // end of block
                const int li = sym - 257;
                if (li >= 29) return false;
                const int lextra = bits(reader, len_extra[li]);
                if (lextra < 0) return false;
                const int dsym = huff_decode(reader, dist);
                if (dsym < 0 || dsym >= max_dist) return false;
                const int dextra = bits(reader, dist_extra[dsym]);
                if (dextra < 0) return false;
                if (!copy_match(
                        out,
                        static_cast<hcpp::size_t>(dist_base[dsym] + dextra),
                        static_cast<hcpp::size_t>(len_base[li] + lextra)))
                    return false;
            }
        }

        bool stored_block(BitReader &reader, OutBuf &out) noexcept {
            // Drop the partial byte, then LEN/NLEN arrive byte-aligned.
            reader.bitbuf = 0;
            reader.bitcnt = 0;
            if (reader.size - reader.pos < 4) return false;
            const hcpp::u32 len = static_cast<hcpp::u32>(reader.data[reader.pos]) |
                                  (static_cast<hcpp::u32>(reader.data[reader.pos + 1]) << 8);
            const hcpp::u32 nlen = static_cast<hcpp::u32>(reader.data[reader.pos + 2]) |
                                   (static_cast<hcpp::u32>(reader.data[reader.pos + 3]) << 8);
            if (len != ((~nlen) & 0xFFFFu)) return false;
            reader.pos += 4;
            if (len > reader.size - reader.pos) return false;
            if (len > out.cap - out.pos) return false;
            for (hcpp::u32 i = 0; i < len; ++i)
                out.data[out.pos++] = reader.data[reader.pos++];
            return true;
        }

        void fixed_tables(Huffman &litlen, Huffman &dist) noexcept {
            hcpp::u8 lengths[max_litlen];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < max_litlen; ++i) lengths[i] = 8;
            huff_build(litlen, lengths, max_litlen);
            hcpp::u8 dlengths[max_dist];
            for (i = 0; i < max_dist; ++i) dlengths[i] = 5;
            huff_build(dist, dlengths, max_dist);
        }

        bool dynamic_tables(BitReader &reader, Huffman &litlen,
                            Huffman &dist) noexcept {
            const int nlen = bits(reader, 5);
            const int ndist = bits(reader, 5);
            const int ncode = bits(reader, 4);
            if ((nlen | ndist | ncode) < 0) return false;
            const int lit_count = nlen + 257;
            const int dist_count = ndist + 1;
            const int clen_count = ncode + 4;
            if (lit_count > 286 || dist_count > max_dist) return false;
            hcpp::u8 lengths[max_litlen + max_dist];
            for (int i = 0; i < max_litlen + max_dist; ++i) lengths[i] = 0;
            for (int i = 0; i < clen_count; ++i) {
                const int value = bits(reader, 3);
                if (value < 0) return false;
                lengths[clen_order[i]] = static_cast<hcpp::u8>(value);
            }
            Huffman clen;
            if (huff_build(clen, lengths, max_clen) < 0) return false;
            int index = 0;
            const int total = lit_count + dist_count;
            while (index < total) {
                const int sym = huff_decode(reader, clen);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[index++] = static_cast<hcpp::u8>(sym);
                } else if (sym == 16) {
                    if (index == 0) return false;
                    const int reps = bits(reader, 2);
                    if (reps < 0 || index + reps + 3 > total) return false;
                    const hcpp::u8 prev = lengths[index - 1];
                    for (int r = 0; r < reps + 3; ++r) lengths[index++] = prev;
                } else {
                    const int reps = bits(reader, sym == 17 ? 3 : 7);
                    const int base = sym == 17 ? 3 : 11;
                    if (reps < 0 || index + reps + base > total) return false;
                    // Must write the zeros: positions still hold the
                    // code-length-code lengths from above.
                    for (int r = 0; r < reps + base; ++r)
                        lengths[index++] = 0;
                }
            }
            if (huff_build(litlen, lengths, lit_count) < 0) return false;
            if (huff_build(dist, lengths + lit_count, dist_count) < 0)
                return false;
            return true;
        }

        // Decode a complete DEFLATE stream. `consumed` receives the byte
        // offset one past the end of the stream (needed by the zlib/gzip
        // trailers); `produced` receives decompressed size.
        bool inflate_core(hcpp::u8 *dst, hcpp::size_t dst_cap,
                          const hcpp::u8 *src, hcpp::size_t src_len,
                          hcpp::size_t &produced,
                          hcpp::size_t &consumed) noexcept {
            BitReader reader{src, src_len, 0, 0, 0};
            OutBuf out{dst, dst_cap, 0};
            bool last = false;
            do {
                const int final_bit = bits(reader, 1);
                const int type = bits(reader, 2);
                if ((final_bit | type) < 0) return false;
                last = final_bit != 0;
                bool ok = false;
                if (type == 0) {
                    ok = stored_block(reader, out);
                } else if (type == 1) {
                    Huffman litlen, dist;
                    fixed_tables(litlen, dist);
                    ok = inflate_block_codes(reader, out, litlen, dist);
                } else if (type == 2) {
                    Huffman litlen, dist;
                    ok = dynamic_tables(reader, litlen, dist) &&
                         inflate_block_codes(reader, out, litlen, dist);
                }
                if (!ok) return false;
            } while (!last);
            produced = out.pos;
            consumed = reader.pos - reader.bitcnt / 8;
            return true;
        }

        // ------------------------------------------------------------------
        // Deflater: greedy hash-chain LZ77 -> single fixed-Huffman block.
        // ------------------------------------------------------------------

        struct BitWriter {
            hcpp::u8 *dst;
            hcpp::size_t cap;
            hcpp::size_t pos;
            hcpp::u32 bitbuf;   // pending bits, LSB = next stream bit
            hcpp::u32 bitcnt;
            bool overflow;
        };

        void put_bits(BitWriter &writer, hcpp::u32 value, int n) noexcept {
            writer.bitbuf |= (value & ((1u << n) - 1u)) << writer.bitcnt;
            writer.bitcnt += static_cast<hcpp::u32>(n);
            while (writer.bitcnt >= 8) {
                if (writer.pos < writer.cap)
                    writer.dst[writer.pos] =
                        static_cast<hcpp::u8>(writer.bitbuf & 0xFFu);
                else
                    writer.overflow = true;
                ++writer.pos;
                writer.bitbuf >>= 8;
                writer.bitcnt -= 8;
            }
        }

        void align_byte(BitWriter &writer) noexcept {
            if (writer.bitcnt != 0) put_bits(writer, 0, 8 - writer.bitcnt);
        }

        hcpp::u32 bit_reverse(hcpp::u32 code, int len) noexcept {
            hcpp::u32 rev = 0;
            for (int i = 0; i < len; ++i) {
                rev = (rev << 1) | (code & 1u);
                code >>= 1;
            }
            return rev;
        }

        // Huffman codes are packed most-significant-bit first.
        void emit_code(BitWriter &writer, hcpp::u32 code, int len) noexcept {
            put_bits(writer, bit_reverse(code, len), len);
        }

        void emit_fixed_literal(BitWriter &writer, int sym) noexcept {
            if (sym < 144) {
                emit_code(writer, 0x30u + static_cast<hcpp::u32>(sym), 8);
            } else if (sym < 256) {
                emit_code(writer,
                          0x190u + static_cast<hcpp::u32>(sym - 144), 9);
            } else if (sym < 280) {
                emit_code(writer, static_cast<hcpp::u32>(sym - 256), 7);
            } else {
                emit_code(writer,
                          0xC0u + static_cast<hcpp::u32>(sym - 280), 8);
            }
        }

        void emit_match(BitWriter &writer, hcpp::size_t len,
                        hcpp::size_t dist) noexcept {
            int li = 28;  // len == 258 must use symbol 285
            while (li > 0 && len_base[li] > len) --li;
            if (len != 258 && len_base[li + 1] <= len) ++li;
            emit_fixed_literal(writer, 257 + li);
            put_bits(writer, static_cast<hcpp::u32>(len - len_base[li]),
                     len_extra[li]);
            int di = max_dist - 1;
            while (di > 0 && dist_base[di] > dist) --di;
            emit_code(writer, static_cast<hcpp::u32>(di), 5);
            put_bits(writer, static_cast<hcpp::u32>(dist - dist_base[di]),
                     dist_extra[di]);
        }

        // LZ77 workspace: hash head + cyclic previous-position chain.
        // Static arena keeps the codec heap-free; not re-entrant (the
        // kernel calls it from a single selftest/service context).
        constexpr hcpp::u32 hash_bits = 15;
        constexpr hcpp::size_t hash_size = hcpp::size_t{1} << hash_bits;
        constexpr int max_chain = 128;
        constexpr hcpp::size_t min_match = 3;
        constexpr hcpp::size_t max_match = 258;

        hcpp::i32 lz_head[hash_size];
        hcpp::i32 lz_prev[window_size];

        hcpp::u32 hash3(const hcpp::u8 *p) noexcept {
            const hcpp::u32 v = static_cast<hcpp::u32>(p[0]) |
                                (static_cast<hcpp::u32>(p[1]) << 8) |
                                (static_cast<hcpp::u32>(p[2]) << 16);
            return (v * 0x1E35A7BDu) >> (32 - hash_bits);
        }

        void lz_insert(const hcpp::u8 *src, hcpp::size_t src_len,
                       hcpp::size_t pos) noexcept {
            if (pos + min_match > src_len) return;
            const hcpp::u32 h = hash3(src + pos);
            lz_prev[pos & (window_size - 1)] = lz_head[h];
            lz_head[h] = static_cast<hcpp::i32>(pos);
        }

        // Emit src as a single BFINAL fixed-Huffman block. Returns false
        // only when dst_cap is too small for the coded stream.
        bool deflate_fixed_block(hcpp::u8 *dst, hcpp::size_t dst_cap,
                                 const hcpp::u8 *src, hcpp::size_t src_len,
                                 hcpp::size_t &out_len) noexcept {
            for (hcpp::size_t i = 0; i < hash_size; ++i) lz_head[i] = -1;
            BitWriter writer{dst, dst_cap, 0, 0, 0, false};
            put_bits(writer, 1, 1);  // BFINAL
            put_bits(writer, 1, 2);  // BTYPE = 01 fixed Huffman
            hcpp::size_t pos = 0;
            while (pos < src_len) {
                hcpp::size_t best_len = 0;
                hcpp::size_t best_dist = 0;
                if (pos + min_match <= src_len) {
                    const hcpp::u32 h = hash3(src + pos);
                    hcpp::i32 cand = lz_head[h];
                    lz_prev[pos & (window_size - 1)] = cand;
                    lz_head[h] = static_cast<hcpp::i32>(pos);
                    const hcpp::size_t max_len =
                        src_len - pos < max_match ? src_len - pos : max_match;
                    int steps = 0;
                    while (cand >= 0 && steps < max_chain) {
                        const hcpp::size_t dist =
                            pos - static_cast<hcpp::size_t>(cand);
                        if (dist > window_size) break;
                        hcpp::size_t l = 0;
                        while (l < max_len && src[cand + l] == src[pos + l])
                            ++l;
                        if (l > best_len) {
                            best_len = l;
                            best_dist = dist;
                            if (l >= max_len) break;
                        }
                        cand = lz_prev[static_cast<hcpp::size_t>(cand) &
                                       (window_size - 1)];
                        ++steps;
                    }
                }
                if (best_len >= min_match) {
                    emit_match(writer, best_len, best_dist);
                    const hcpp::size_t end = pos + best_len;
                    for (hcpp::size_t p = pos + 1; p < end; ++p)
                        lz_insert(src, src_len, p);
                    pos = end;
                } else {
                    emit_fixed_literal(writer, src[pos]);
                    ++pos;
                }
            }
            emit_fixed_literal(writer, 256);  // end of block
            align_byte(writer);
            if (writer.overflow) return false;
            out_len = writer.pos;
            return true;
        }

        // RFC 1951 stored (uncompressed) blocks; always valid output.
        bool deflate_stored_blocks(hcpp::u8 *dst, hcpp::size_t dst_cap,
                                   const hcpp::u8 *src,
                                   hcpp::size_t src_len,
                                   hcpp::size_t &out_len) noexcept {
            BitWriter writer{dst, dst_cap, 0, 0, 0, false};
            hcpp::size_t offset = 0;
            do {
                const hcpp::size_t left = src_len - offset;
                const hcpp::u32 block =
                    left > 65535 ? 65535u : static_cast<hcpp::u32>(left);
                const bool last = offset + block == src_len;
                put_bits(writer, last ? 1u : 0u, 1);
                put_bits(writer, 0, 2);  // BTYPE = 00 stored
                align_byte(writer);
                if (writer.cap - writer.pos < block + 4) return false;
                dst[writer.pos++] = static_cast<hcpp::u8>(block & 0xFFu);
                dst[writer.pos++] = static_cast<hcpp::u8>((block >> 8) & 0xFFu);
                const hcpp::u32 nlen = (~block) & 0xFFFFu;
                dst[writer.pos++] = static_cast<hcpp::u8>(nlen & 0xFFu);
                dst[writer.pos++] = static_cast<hcpp::u8>((nlen >> 8) & 0xFFu);
                for (hcpp::u32 i = 0; i < block; ++i)
                    dst[writer.pos++] = src[offset + i];
                offset += block;
            } while (offset < src_len);
            out_len = writer.pos;
            return !writer.overflow;
        }

        hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
            return (static_cast<hcpp::u32>(p[0]) << 24) |
                   (static_cast<hcpp::u32>(p[1]) << 16) |
                   (static_cast<hcpp::u32>(p[2]) << 8) |
                   static_cast<hcpp::u32>(p[3]);
        }

        hcpp::u32 read_le32(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u32>(p[0]) |
                   (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }

        void write_be32(hcpp::u8 *p, hcpp::u32 value) noexcept {
            p[0] = static_cast<hcpp::u8>((value >> 24) & 0xFFu);
            p[1] = static_cast<hcpp::u8>((value >> 16) & 0xFFu);
            p[2] = static_cast<hcpp::u8>((value >> 8) & 0xFFu);
            p[3] = static_cast<hcpp::u8>(value & 0xFFu);
        }

        void write_le32(hcpp::u8 *p, hcpp::u32 value) noexcept {
            p[0] = static_cast<hcpp::u8>(value & 0xFFu);
            p[1] = static_cast<hcpp::u8>((value >> 8) & 0xFFu);
            p[2] = static_cast<hcpp::u8>((value >> 16) & 0xFFu);
            p[3] = static_cast<hcpp::u8>((value >> 24) & 0xFFu);
        }

    }  // namespace

    hcpp::u32 ztext_crc32(const void *data, hcpp::size_t size) noexcept {
        const auto *bytes = static_cast<const hcpp::u8 *>(data);
        hcpp::u32 crc = 0xFFFFFFFFu;
        for (hcpp::size_t i = 0; i < size; ++i)
            crc = crc_table.entry[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
        return ~crc;
    }

    hcpp::u32 ztext_adler32(const void *data, hcpp::size_t size) noexcept {
        const auto *bytes = static_cast<const hcpp::u8 *>(data);
        hcpp::u32 a = 1;
        hcpp::u32 b = 0;
        hcpp::size_t i = 0;
        while (i < size) {
            // NMAX: largest n keeping b < 2^32 (RFC 1950).
            hcpp::size_t chunk = size - i;
            if (chunk > 5552) chunk = 5552;
            for (hcpp::size_t j = 0; j < chunk; ++j) {
                a += bytes[i + j];
                b += a;
            }
            a %= adler_mod;
            b %= adler_mod;
            i += chunk;
        }
        return (b << 16) | a;
    }

    bool ztext_inflate(hcpp::u8 *dst, hcpp::size_t dst_cap,
                       const hcpp::u8 *src, hcpp::size_t src_len,
                       hcpp::size_t &out_len) noexcept {
        hcpp::size_t consumed = 0;
        out_len = 0;
        if (dst == nullptr || src == nullptr) return false;
        return inflate_core(dst, dst_cap, src, src_len, out_len, consumed);
    }

    bool ztext_deflate(hcpp::u8 *dst, hcpp::size_t dst_cap,
                       const hcpp::u8 *src, hcpp::size_t src_len,
                       hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (dst == nullptr || (src == nullptr && src_len != 0)) return false;
        // Hash-chain positions are tracked as i32; beyond that range the
        // stored-block encoder still produces a valid stream.
        if (src_len < 0x7FFF0000 &&
            deflate_fixed_block(dst, dst_cap, src, src_len, out_len))
            return true;
        // Coded form did not fit (incompressible data into a tight
        // buffer): stored blocks are the minimal-size fallback.
        return deflate_stored_blocks(dst, dst_cap, src, src_len, out_len);
    }

    hcpp::size_t ztext_deflate_bound(hcpp::size_t src_len) noexcept {
        // Coded worst case ~9/8 src (literal expansion); stored worst
        // case src + 5 bytes per 64KB block + alignment.
        return src_len + (src_len >> 3) +
               5 * ((src_len + 65534) / 65535) + 16;
    }

    bool ztext_zlib_decode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (dst == nullptr || src == nullptr || src_len < 6) return false;
        const hcpp::u32 cmf = src[0];
        const hcpp::u32 flg = src[1];
        if ((cmf & 0x0Fu) != 8) return false;       // method = deflate
        if ((cmf >> 4) > 7) return false;           // window <= 32KB
        if (((cmf << 8) | flg) % 31 != 0) return false;
        if ((flg & 0x20u) != 0) return false;       // preset dictionary
        hcpp::size_t consumed = 0;
        if (!inflate_core(dst, dst_cap, src + 2, src_len - 2, out_len,
                          consumed))
            return false;
        if (src_len - 2 - consumed < 4) return false;
        const hcpp::u32 want = read_be32(src + 2 + consumed);
        return want == ztext_adler32(dst, out_len);
    }

    bool ztext_zlib_encode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (dst == nullptr || dst_cap < 7) return false;
        const hcpp::u8 cmf = 0x78;  // 32KB window, deflate
        const hcpp::u8 flg_base = 0x80;  // FLEVEL=2, FDICT=0
        const hcpp::u8 fcheck = static_cast<hcpp::u8>(
            (31 - ((static_cast<hcpp::u32>(cmf) * 256 + flg_base) % 31)) % 31);
        dst[0] = cmf;
        dst[1] = static_cast<hcpp::u8>(flg_base | fcheck);
        hcpp::size_t inner = 0;
        if (!ztext_deflate(dst + 2, dst_cap - 2 - 4, src, src_len, inner))
            return false;
        write_be32(dst + 2 + inner, ztext_adler32(src, src_len));
        out_len = 2 + inner + 4;
        return true;
    }

    bool ztext_gzip_decode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (dst == nullptr || src == nullptr || src_len < 18) return false;
        if (src[0] != 0x1F || src[1] != 0x8B || src[2] != 8) return false;
        const hcpp::u8 flg = src[3];
        if ((flg & 0xE0u) != 0) return false;  // reserved bits
        hcpp::size_t pos = 10;  // fixed header: magic,CM,FLG,MTIME,XFL,OS
        if ((flg & 0x04u) != 0) {  // FEXTRA
            if (src_len - pos < 2) return false;
            const hcpp::u32 xlen = static_cast<hcpp::u32>(src[pos]) |
                                   (static_cast<hcpp::u32>(src[pos + 1]) << 8);
            pos += 2;
            if (src_len - pos < xlen) return false;
            pos += xlen;
        }
        if ((flg & 0x08u) != 0) {  // FNAME: zero-terminated
            while (pos < src_len && src[pos] != 0) ++pos;
            if (pos >= src_len) return false;
            ++pos;
        }
        if ((flg & 0x10u) != 0) {  // FCOMMENT: zero-terminated
            while (pos < src_len && src[pos] != 0) ++pos;
            if (pos >= src_len) return false;
            ++pos;
        }
        if ((flg & 0x02u) != 0) {  // FHCRC: low 16 bits of header crc32
            if (src_len - pos < 2) return false;
            const hcpp::u32 want = static_cast<hcpp::u32>(src[pos]) |
                                   (static_cast<hcpp::u32>(src[pos + 1]) << 8);
            if (want != (ztext_crc32(src, pos) & 0xFFFFu)) return false;
            pos += 2;
        }
        hcpp::size_t consumed = 0;
        if (!inflate_core(dst, dst_cap, src + pos, src_len - pos, out_len,
                          consumed))
            return false;
        pos += consumed;
        if (src_len - pos < 8) return false;
        if (read_le32(src + pos) != ztext_crc32(dst, out_len)) return false;
        if (read_le32(src + pos + 4) !=
            static_cast<hcpp::u32>(out_len & 0xFFFFFFFFu))
            return false;
        return true;
    }

    bool ztext_gzip_encode(hcpp::u8 *dst, hcpp::size_t dst_cap,
                           const hcpp::u8 *src, hcpp::size_t src_len,
                           hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (dst == nullptr || dst_cap < 18) return false;
        const hcpp::u8 header[10] = {0x1F, 0x8B, 0x08, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x02, 0xFF};
        for (int i = 0; i < 10; ++i) dst[i] = header[i];
        hcpp::size_t inner = 0;
        if (!ztext_deflate(dst + 10, dst_cap - 10 - 8, src, src_len, inner))
            return false;
        write_le32(dst + 10 + inner, ztext_crc32(src, src_len));
        write_le32(dst + 10 + inner + 4,
                   static_cast<hcpp::u32>(src_len & 0xFFFFFFFFu));
        out_len = 10 + inner + 8;
        return true;
    }

}  // namespace hype::ztext
