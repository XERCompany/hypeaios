#include "kernel/codecapps/image/inflate.h"

namespace hype::codec::image {

    namespace {

        constexpr int max_bits = 15;
        constexpr int max_symbols = 288;
        constexpr int max_dist_symbols = 30;
        constexpr hcpp::size_t max_stored = 65535;
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
        const hcpp::u16 dist_base[max_dist_symbols] = {
            1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
            33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
            1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
        };
        const hcpp::u8 dist_extra[max_dist_symbols] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
        };
        const hcpp::u8 clen_order[19] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
        };

        struct BitReader {
            const hcpp::u8 *data;
            hcpp::size_t size;
            hcpp::size_t pos;
            hcpp::u32 bitbuf;
            hcpp::u32 bitcnt;
        };

        struct OutBuf {
            hcpp::u8 *data;
            hcpp::size_t cap;
            hcpp::size_t pos;
        };

        struct Huffman {
            hcpp::u16 count[max_bits + 1];
            hcpp::u16 symbol[max_symbols];
        };

        int bits(BitReader &reader, int need) noexcept {
            hcpp::u32 value = reader.bitbuf;
            while (reader.bitcnt < static_cast<hcpp::u32>(need)) {
                if (reader.pos >= reader.size) return -1;
                value |= static_cast<hcpp::u32>(reader.data[reader.pos++]) << reader.bitcnt;
                reader.bitcnt += 8;
            }
            reader.bitbuf = value >> need;
            reader.bitcnt -= static_cast<hcpp::u32>(need);
            return static_cast<int>(value & ((1u << need) - 1));
        }

        int construct(Huffman &huff, const hcpp::u8 *lengths, int n) noexcept {
            for (int len = 0; len <= max_bits; ++len) huff.count[len] = 0;
            for (int sym = 0; sym < n; ++sym) ++huff.count[lengths[sym]];
            if (huff.count[0] == n) return 0;
            int left = 1;
            for (int len = 1; len <= max_bits; ++len) {
                left <<= 1;
                left -= huff.count[len];
                if (left < 0) return left;
            }
            hcpp::u16 offsets[max_bits + 1];
            offsets[1] = 0;
            for (int len = 1; len < max_bits; ++len)
                offsets[len + 1] = static_cast<hcpp::u16>(offsets[len] + huff.count[len]);
            for (int sym = 0; sym < n; ++sym)
                if (lengths[sym] != 0)
                    huff.symbol[offsets[lengths[sym]]++] = static_cast<hcpp::u16>(sym);
            return left;
        }

        int decode(BitReader &reader, const Huffman &huff) noexcept {
            int code = 0;
            int first = 0;
            int index = 0;
            for (int len = 1; len <= max_bits; ++len) {
                const int got = bits(reader, 1);
                if (got < 0) return -1;
                code |= got;
                const int count = huff.count[len];
                if (code - first < count) return huff.symbol[index + (code - first)];
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }

        bool copy_match(OutBuf &out, hcpp::size_t dist, hcpp::size_t len) noexcept {
            if (dist == 0 || dist > out.pos || len > out.cap - out.pos) return false;
            for (hcpp::size_t i = 0; i < len; ++i) {
                out.data[out.pos] = out.data[out.pos - dist];
                ++out.pos;
            }
            return true;
        }

        bool inflate_codes(BitReader &reader, OutBuf &out, const Huffman &litlen,
                           const Huffman &dist) noexcept {
            for (;;) {
                const int sym = decode(reader, litlen);
                if (sym < 0) return false;
                if (sym < 256) {
                    if (out.pos >= out.cap) return false;
                    out.data[out.pos++] = static_cast<hcpp::u8>(sym);
                    continue;
                }
                if (sym == 256) return true;
                const int li = sym - 257;
                if (li >= 29) return false;
                const int extra_len = bits(reader, len_extra[li]);
                if (extra_len < 0) return false;
                const int dsym = decode(reader, dist);
                if (dsym < 0 || dsym >= max_dist_symbols) return false;
                const int extra_dist = bits(reader, dist_extra[dsym]);
                if (extra_dist < 0) return false;
                const hcpp::size_t len =
                    static_cast<hcpp::size_t>(len_base[li] + extra_len);
                const hcpp::size_t span =
                    static_cast<hcpp::size_t>(dist_base[dsym] + extra_dist);
                if (!copy_match(out, span, len)) return false;
            }
        }

        bool stored_block(BitReader &reader, OutBuf &out) noexcept {
            const hcpp::u32 drop = reader.bitcnt & 7;
            reader.bitbuf >>= drop;
            reader.bitcnt -= drop;
            const int len_lo = bits(reader, 8);
            const int len_hi = bits(reader, 8);
            const int nlen_lo = bits(reader, 8);
            const int nlen_hi = bits(reader, 8);
            if ((len_lo | len_hi | nlen_lo | nlen_hi) < 0) return false;
            const hcpp::u32 len =
                static_cast<hcpp::u32>(len_lo) | (static_cast<hcpp::u32>(len_hi) << 8);
            const hcpp::u32 nlen =
                static_cast<hcpp::u32>(nlen_lo) | (static_cast<hcpp::u32>(nlen_hi) << 8);
            if (len != ((~nlen) & 0xFFFFu)) return false;
            if (len > out.cap - out.pos) return false;
            for (hcpp::u32 i = 0; i < len; ++i) {
                const int byte = bits(reader, 8);
                if (byte < 0) return false;
                out.data[out.pos++] = static_cast<hcpp::u8>(byte);
            }
            return true;
        }

        void fixed_tables(Huffman &litlen, Huffman &dist) noexcept {
            hcpp::u8 lengths[max_symbols];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < max_symbols; ++i) lengths[i] = 8;
            construct(litlen, lengths, max_symbols);
            hcpp::u8 dist_lengths[max_dist_symbols];
            for (i = 0; i < max_dist_symbols; ++i) dist_lengths[i] = 5;
            construct(dist, dist_lengths, max_dist_symbols);
        }

        bool dynamic_tables(BitReader &reader, Huffman &litlen, Huffman &dist) noexcept {
            const int nlen = bits(reader, 5);
            const int ndist = bits(reader, 5);
            const int ncode = bits(reader, 4);
            if ((nlen | ndist | ncode) < 0) return false;
            const int lit_count = nlen + 257;
            const int dist_count = ndist + 1;
            const int clen_count = ncode + 4;
            if (lit_count > 286 || dist_count > max_dist_symbols) return false;
            hcpp::u8 lengths[max_symbols + max_dist_symbols];
            for (int i = 0; i < clen_count; ++i) {
                const int value = bits(reader, 3);
                if (value < 0) return false;
                lengths[clen_order[i]] = static_cast<hcpp::u8>(value);
            }
            for (int i = clen_count; i < 19; ++i) lengths[clen_order[i]] = 0;
            Huffman clen;
            if (construct(clen, lengths, 19) < 0) return false;
            int index = 0;
            const int total = lit_count + dist_count;
            while (index < total) {
                const int sym = decode(reader, clen);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[index++] = static_cast<hcpp::u8>(sym);
                    continue;
                }
                hcpp::u8 fill = 0;
                int repeat = 0;
                if (sym == 16) {
                    if (index == 0) return false;
                    fill = lengths[index - 1];
                    repeat = bits(reader, 2);
                    if (repeat < 0) return false;
                    repeat += 3;
                } else if (sym == 17) {
                    repeat = bits(reader, 3);
                    if (repeat < 0) return false;
                    repeat += 3;
                } else {
                    repeat = bits(reader, 7);
                    if (repeat < 0) return false;
                    repeat += 11;
                }
                if (repeat > total - index) return false;
                while (repeat-- > 0) lengths[index++] = fill;
            }
            if (lengths[256] == 0) return false;
            const int lit_err = construct(litlen, lengths, lit_count);
            if (lit_err < 0 ||
                (lit_err > 0 && lit_count != litlen.count[0] + litlen.count[1]))
                return false;
            const int dist_err = construct(dist, lengths + lit_count, dist_count);
            if (dist_err < 0 ||
                (dist_err > 0 && dist_count != dist.count[0] + dist.count[1]))
                return false;
            return true;
        }

    }  // namespace

    hcpp::u32 inflate_adler32(const hcpp::u8 *data, hcpp::size_t size) noexcept {
        hcpp::u32 a = 1;
        hcpp::u32 b = 0;
        while (size != 0) {
            const hcpp::size_t block = size > 5552 ? 5552 : size;
            size -= block;
            for (hcpp::size_t i = 0; i < block; ++i) {
                a += data[i];
                b += a;
            }
            a %= adler_mod;
            b %= adler_mod;
            data += block;
        }
        return (b << 16) | a;
    }

    bool inflate_deflate(const hcpp::u8 *input, hcpp::size_t input_size,
                         hcpp::size_t &consumed, hcpp::u8 *output,
                         hcpp::size_t output_cap, hcpp::size_t &produced) noexcept {
        consumed = 0;
        produced = 0;
        if (input == nullptr || (output == nullptr && output_cap != 0)) return false;
        BitReader reader{input, input_size, 0, 0, 0};
        OutBuf out{output, output_cap, 0};
        bool last = false;
        do {
            const int final = bits(reader, 1);
            const int type = bits(reader, 2);
            if ((final | type) < 0) return false;
            last = final != 0;
            bool ok = false;
            if (type == 0) {
                ok = stored_block(reader, out);
            } else if (type == 1) {
                Huffman litlen;
                Huffman dist;
                fixed_tables(litlen, dist);
                ok = inflate_codes(reader, out, litlen, dist);
            } else if (type == 2) {
                Huffman litlen;
                Huffman dist;
                ok = dynamic_tables(reader, litlen, dist) &&
                     inflate_codes(reader, out, litlen, dist);
            }
            if (!ok) return false;
        } while (!last);
        produced = out.pos;
        consumed = reader.pos - reader.bitcnt / 8;
        return true;
    }

    bool inflate_zlib(const hcpp::u8 *input, hcpp::size_t input_size, hcpp::u8 *output,
                      hcpp::size_t output_cap, hcpp::size_t &produced) noexcept {
        produced = 0;
        if (input == nullptr || input_size < 2) return false;
        const hcpp::u8 cmf = input[0];
        const hcpp::u8 flg = input[1];
        if ((cmf & 0x0F) != 8) return false;
        if ((static_cast<hcpp::u32>(cmf) * 256 + flg) % 31 != 0) return false;
        if ((flg & 0x20) != 0) return false;
        hcpp::size_t consumed = 0;
        if (!inflate_deflate(input + 2, input_size - 2, consumed, output, output_cap,
                             produced))
            return false;
        const hcpp::size_t adler_at = 2 + consumed;
        if (adler_at + 4 <= input_size) {
            const hcpp::u32 want =
                (static_cast<hcpp::u32>(input[adler_at]) << 24) |
                (static_cast<hcpp::u32>(input[adler_at + 1]) << 16) |
                (static_cast<hcpp::u32>(input[adler_at + 2]) << 8) |
                static_cast<hcpp::u32>(input[adler_at + 3]);
            if (want != inflate_adler32(output, produced)) return false;
        }
        return true;
    }

    hcpp::size_t deflate_stored(const hcpp::u8 *input, hcpp::size_t input_size,
                                hcpp::u8 *output, hcpp::size_t output_cap) noexcept {
        if (input == nullptr || output == nullptr) return 0;
        hcpp::size_t ipos = 0;
        hcpp::size_t opos = 0;
        for (;;) {
            const hcpp::size_t remaining = input_size - ipos;
            const hcpp::size_t block = remaining > max_stored ? max_stored : remaining;
            const bool last = ipos + block == input_size;
            if (block > output_cap || 5 > output_cap - block ||
                opos > output_cap - block - 5)
                return 0;
            output[opos++] = last ? 1 : 0;
            output[opos++] = static_cast<hcpp::u8>(block & 0xFF);
            output[opos++] = static_cast<hcpp::u8>((block >> 8) & 0xFF);
            const hcpp::u16 nlen = static_cast<hcpp::u16>(~static_cast<hcpp::u16>(block));
            output[opos++] = static_cast<hcpp::u8>(nlen & 0xFF);
            output[opos++] = static_cast<hcpp::u8>((nlen >> 8) & 0xFF);
            for (hcpp::size_t i = 0; i < block; ++i) output[opos++] = input[ipos++];
            if (last) break;
        }
        return opos;
    }

    hcpp::size_t zlib_stored(const hcpp::u8 *input, hcpp::size_t input_size,
                             hcpp::u8 *output, hcpp::size_t output_cap) noexcept {
        if (input == nullptr || output == nullptr || output_cap < 6) return 0;
        output[0] = 0x78;
        output[1] = 0x01;
        const hcpp::size_t body =
            deflate_stored(input, input_size, output + 2, output_cap - 2);
        if (body == 0) return 0;
        hcpp::size_t opos = 2 + body;
        if (output_cap - opos < 4) return 0;
        const hcpp::u32 adler = inflate_adler32(input, input_size);
        output[opos++] = static_cast<hcpp::u8>((adler >> 24) & 0xFF);
        output[opos++] = static_cast<hcpp::u8>((adler >> 16) & 0xFF);
        output[opos++] = static_cast<hcpp::u8>((adler >> 8) & 0xFF);
        output[opos++] = static_cast<hcpp::u8>(adler & 0xFF);
        return opos;
    }

}  // namespace hype::codec::image
