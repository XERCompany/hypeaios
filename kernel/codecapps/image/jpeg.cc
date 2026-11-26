// Provenance: baseline JPEG decoder written in the compact style of NanoJPEG
// (Martin J. Fiedler, public domain); reworked for hypeaios hcpp/Arena, fixed-point IDCT.
#include "kernel/codecapps/image/jpeg.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/image/imageutil.h"
#include "kernel/codecapps/image/jpeg_selftest_blob.h"

namespace hype::codec::image {

    namespace {

        constexpr hcpp::size_t max_components = 3;
        constexpr hcpp::size_t max_quant_tables = 4;
        constexpr hcpp::size_t max_huff_tables = 4;
        constexpr hcpp::i32 cos_scale_bits = 12;

        const hcpp::u8 zigzag[64] = {
            0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
            12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
            35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
            58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
        };

        const hcpp::i32 icos[64] = {
            2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896,
            4017, 3406, 2276, 799, -799, -2276, -3406, -4017,
            3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784,
            3406, -799, -4017, -2276, 2276, 4017, 799, -3406,
            2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896,
            2276, -4017, 799, 3406, -3406, -799, 4017, -2276,
            1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567,
            799, -2276, 3406, -4017, 4017, -3406, 2276, -799,
        };

        struct HuffTable {
            hcpp::u8 counts[17];
            hcpp::u8 symbols[256];
            hcpp::i32 min_code[17];
            hcpp::i32 max_code[17];
            hcpp::i32 val_ptr[17];
            bool valid;
        };

        struct Component {
            hcpp::u8 id;
            hcpp::u8 ssx;
            hcpp::u8 ssy;
            hcpp::u8 qt;
            hcpp::u8 dc_tab;
            hcpp::u8 ac_tab;
            hcpp::i32 stride;
            hcpp::i32 plane_h;
            hcpp::i32 dc_pred;
            hcpp::u8 *pixels;
        };

        struct Decoder {
            const hcpp::u8 *pos;
            hcpp::size_t remain;
            hcpp::u32 bit_buf;
            hcpp::i32 bit_num;
            CodecStatus error;
            hcpp::u16 qt[max_quant_tables][64];
            bool qt_set[max_quant_tables];
            HuffTable dc[max_huff_tables];
            HuffTable ac[max_huff_tables];
            hcpp::i32 restart_interval;
            hcpp::i32 width;
            hcpp::i32 height;
            hcpp::i32 comp_count;
            hcpp::i32 mcux;
            hcpp::i32 mcuy;
            Component comp[max_components];
            Arena *arena;
            bool frame_seen;
            bool scan_done;
        };

        hcpp::i32 fail(Decoder &d, CodecStatus status) noexcept {
            if (d.error == CodecStatus::Ok) d.error = status;
            return -1;
        }

        hcpp::i32 next_byte(Decoder &d) noexcept {
            if (d.remain == 0) return fail(d, CodecStatus::BadInput);
            const hcpp::u8 b = d.pos[0];
            if (b != 0xFF) {
                ++d.pos;
                --d.remain;
                return b;
            }
            if (d.remain < 2) return fail(d, CodecStatus::BadInput);
            if (d.pos[1] == 0x00) {
                d.pos += 2;
                d.remain -= 2;
                return 0xFF;
            }
            if (d.pos[1] == 0xFF) {
                ++d.pos;
                --d.remain;
                return next_byte(d);
            }
            return fail(d, CodecStatus::BadInput);
        }

        hcpp::i32 get_bits(Decoder &d, hcpp::i32 count) noexcept {
            if (count == 0) return 0;
            while (d.bit_num < count) {
                const hcpp::i32 b = next_byte(d);
                if (b < 0) return -1;
                d.bit_buf = (d.bit_buf << 8) | static_cast<hcpp::u32>(b);
                d.bit_num += 8;
            }
            d.bit_num -= count;
            return static_cast<hcpp::i32>((d.bit_buf >> d.bit_num) &
                                          ((1u << count) - 1u));
        }

        hcpp::i32 huff_decode(Decoder &d, const HuffTable &table) noexcept {
            hcpp::i32 code = 0;
            for (hcpp::i32 length = 1; length <= 16; ++length) {
                const hcpp::i32 bit = get_bits(d, 1);
                if (bit < 0) return fail(d, CodecStatus::BadInput);
                code = (code << 1) | bit;
                if (table.counts[length] != 0 && code >= table.min_code[length] &&
                    code <= table.max_code[length]) {
                    const hcpp::i32 index =
                        table.val_ptr[length] + code - table.min_code[length];
                    return table.symbols[index];
                }
            }
            return fail(d, CodecStatus::BadInput);
        }

        hcpp::i32 extend(hcpp::i32 bits, hcpp::i32 count) noexcept {
            if (count == 0) return 0;
            if (bits < (1 << (count - 1))) bits -= (1 << count) - 1;
            return bits;
        }

        hcpp::u8 clamp8(hcpp::i32 value) noexcept {
            if (value < 0) return 0;
            if (value > 255) return 255;
            return static_cast<hcpp::u8>(value);
        }

        void idct_store(const hcpp::i32 *coef, hcpp::u8 *dst, hcpp::i32 stride) noexcept {
            hcpp::i64 tmp[64];
            for (hcpp::i32 v = 0; v < 8; ++v) {
                for (hcpp::i32 x = 0; x < 8; ++x) {
                    hcpp::i64 sum = 0;
                    for (hcpp::i32 u = 0; u < 8; ++u)
                        sum += static_cast<hcpp::i64>(coef[v * 8 + u]) * icos[u * 8 + x];
                    tmp[v * 8 + x] = (sum + (1 << (cos_scale_bits - 1))) >> cos_scale_bits;
                }
            }
            for (hcpp::i32 y = 0; y < 8; ++y) {
                for (hcpp::i32 x = 0; x < 8; ++x) {
                    hcpp::i64 sum = 0;
                    for (hcpp::i32 v = 0; v < 8; ++v)
                        sum += tmp[v * 8 + x] * icos[v * 8 + y];
                    const hcpp::i32 value =
                        static_cast<hcpp::i32>((sum + (1 << (cos_scale_bits + 1))) >>
                                               (cos_scale_bits + 2));
                    dst[y * stride + x] = clamp8(value + 128);
                }
            }
        }

        bool decode_block(Decoder &d, Component &comp, hcpp::i32 block_x,
                          hcpp::i32 block_y) noexcept {
            hcpp::i32 coef[64] = {};
            const HuffTable &dc = d.dc[comp.dc_tab];
            const HuffTable &ac = d.ac[comp.ac_tab];
            if (!dc.valid || !ac.valid) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            const hcpp::i32 t = huff_decode(d, dc);
            if (t < 0 || t > 11) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            hcpp::i32 diff = 0;
            if (t != 0) {
                diff = get_bits(d, t);
                if (diff < 0) return false;
                diff = extend(diff, t);
            }
            comp.dc_pred += diff;
            coef[0] = comp.dc_pred * d.qt[comp.qt][0];
            hcpp::i32 k = 1;
            while (k < 64) {
                const hcpp::i32 rs = huff_decode(d, ac);
                if (rs < 0) return false;
                const hcpp::i32 run = rs >> 4;
                const hcpp::i32 size = rs & 0x0F;
                if (size == 0) {
                    if (run != 15) break;
                    k += 16;
                    continue;
                }
                k += run;
                if (k >= 64) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                hcpp::i32 value = get_bits(d, size);
                if (value < 0) return false;
                value = extend(value, size);
                coef[zigzag[k]] = value * d.qt[comp.qt][k];
                ++k;
            }
            idct_store(coef, comp.pixels + block_y * 8 * comp.stride + block_x * 8,
                       comp.stride);
            return true;
        }

        bool skip_restart(Decoder &d, hcpp::i32 &expected) noexcept {
            d.bit_buf = 0;
            d.bit_num = 0;
            while (d.remain >= 2 && d.pos[0] == 0xFF && d.pos[1] == 0xFF) {
                ++d.pos;
                --d.remain;
            }
            if (d.remain < 2 || d.pos[0] != 0xFF || (d.pos[1] & 0xF8) != 0xD0) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            if ((d.pos[1] & 0x07) != (expected & 0x07)) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            expected = (expected + 1) & 0x07;
            d.pos += 2;
            d.remain -= 2;
            for (hcpp::i32 i = 0; i < d.comp_count; ++i) d.comp[i].dc_pred = 0;
            return true;
        }

        bool decode_scan(Decoder &d) noexcept {
            hcpp::i32 mcu_index = 0;
            hcpp::i32 expected_rst = 0;
            for (hcpp::i32 my = 0; my < d.mcuy; ++my) {
                for (hcpp::i32 mx = 0; mx < d.mcux; ++mx, ++mcu_index) {
                    if (d.restart_interval > 0 && mcu_index > 0 &&
                        (mcu_index % d.restart_interval) == 0 &&
                        !skip_restart(d, expected_rst))
                        return false;
                    for (hcpp::i32 c = 0; c < d.comp_count; ++c) {
                        Component &comp = d.comp[c];
                        for (hcpp::i32 by = 0; by < comp.ssy; ++by)
                            for (hcpp::i32 bx = 0; bx < comp.ssx; ++bx)
                                if (!decode_block(d, comp, mx * comp.ssx + bx,
                                                  my * comp.ssy + by))
                                    return false;
                    }
                }
            }
            return true;
        }

        bool read_marker(Decoder &d, hcpp::u8 &marker) noexcept {
            while (d.remain > 0 && d.pos[0] != 0xFF) {
                ++d.pos;
                --d.remain;
            }
            while (d.remain >= 2 && d.pos[0] == 0xFF && d.pos[1] == 0xFF) {
                ++d.pos;
                --d.remain;
            }
            if (d.remain < 2 || d.pos[0] != 0xFF) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            marker = d.pos[1];
            d.pos += 2;
            d.remain -= 2;
            return true;
        }

        hcpp::i32 seg_length(Decoder &d) noexcept {
            if (d.remain < 2) return fail(d, CodecStatus::BadInput);
            const hcpp::i32 length = (d.pos[0] << 8) | d.pos[1];
            d.pos += 2;
            d.remain -= 2;
            if (length < 2 || static_cast<hcpp::size_t>(length - 2) > d.remain) {
                return fail(d, CodecStatus::BadInput);
            }
            return length - 2;
        }

        void seg_consume(Decoder &d, hcpp::size_t amount) noexcept {
            if (amount > d.remain) amount = d.remain;
            d.pos += amount;
            d.remain -= amount;
        }

        bool parse_dqt(Decoder &d, hcpp::i32 length) noexcept {
            while (length > 0 && d.error == CodecStatus::Ok) {
                if (d.remain < 1) return fail(d, CodecStatus::BadInput) != 0;
                const hcpp::u8 pq_tq = d.pos[0];
                seg_consume(d, 1);
                --length;
                const hcpp::i32 pq = pq_tq >> 4;
                const hcpp::i32 tq = pq_tq & 0x0F;
                const hcpp::i32 bytes = pq == 0 ? 64 : 128;
                if (pq > 1 || tq >= static_cast<hcpp::i32>(max_quant_tables) ||
                    bytes > length || static_cast<hcpp::size_t>(bytes) > d.remain) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                for (hcpp::i32 k = 0; k < 64; ++k) {
                    hcpp::u16 value;
                    if (pq == 0) {
                        value = d.pos[k];
                    } else {
                        value = static_cast<hcpp::u16>((d.pos[k * 2] << 8) | d.pos[k * 2 + 1]);
                    }
                    d.qt[tq][k] = value;
                }
                seg_consume(d, static_cast<hcpp::size_t>(bytes));
                length -= bytes;
                d.qt_set[tq] = true;
            }
            return d.error == CodecStatus::Ok;
        }

        bool parse_dht(Decoder &d, hcpp::i32 length) noexcept {
            while (length > 0 && d.error == CodecStatus::Ok) {
                if (d.remain < 17) return fail(d, CodecStatus::BadInput) != 0;
                const hcpp::u8 tc_th = d.pos[0];
                const hcpp::i32 tc = tc_th >> 4;
                const hcpp::i32 th = tc_th & 0x0F;
                if (tc > 1 || th >= static_cast<hcpp::i32>(max_huff_tables)) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                HuffTable &table = tc == 0 ? d.dc[th] : d.ac[th];
                hcpp::i32 total = 0;
                table.counts[0] = 0;
                for (hcpp::i32 i = 1; i <= 16; ++i) {
                    table.counts[i] = d.pos[i];
                    total += d.pos[i];
                }
                if (total > 256 || 17 + total > length ||
                    static_cast<hcpp::size_t>(17 + total) > d.remain) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                for (hcpp::i32 i = 0; i < total; ++i) table.symbols[i] = d.pos[17 + i];
                seg_consume(d, static_cast<hcpp::size_t>(17 + total));
                length -= 17 + total;
                hcpp::i32 code = 0;
                hcpp::i32 k = 0;
                for (hcpp::i32 i = 1; i <= 16; ++i) {
                    if (table.counts[i] != 0) {
                        table.val_ptr[i] = k;
                        table.min_code[i] = code;
                        k += table.counts[i];
                        code += table.counts[i];
                        table.max_code[i] = code - 1;
                    } else {
                        table.max_code[i] = -1;
                        table.min_code[i] = 0;
                        table.val_ptr[i] = 0;
                    }
                    code <<= 1;
                }
                table.valid = true;
            }
            return d.error == CodecStatus::Ok;
        }

        bool parse_sof(Decoder &d, hcpp::i32 length) noexcept {
            if (length < 6 || d.remain < 6) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            if (d.pos[0] != 8) {
                fail(d, CodecStatus::Unsupported);
                return false;
            }
            d.height = (d.pos[1] << 8) | d.pos[2];
            d.width = (d.pos[3] << 8) | d.pos[4];
            d.comp_count = d.pos[5];
            seg_consume(d, 6);
            length -= 6;
            if (d.width <= 0 || d.height <= 0 || d.comp_count != length / 3 ||
                d.comp_count < 1 || d.comp_count > static_cast<hcpp::i32>(max_components) ||
                (d.comp_count != 1 && d.comp_count != 3)) {
                fail(d, d.comp_count == 1 || d.comp_count == 3 ? CodecStatus::BadInput
                                                              : CodecStatus::Unsupported);
                return false;
            }
            hcpp::i32 hmax = 1;
            hcpp::i32 vmax = 1;
            hcpp::i32 mcu_blocks = 0;
            for (hcpp::i32 i = 0; i < d.comp_count; ++i) {
                if (d.remain < 3) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                Component &comp = d.comp[i];
                comp.id = d.pos[0];
                comp.ssx = d.pos[1] >> 4;
                comp.ssy = d.pos[1] & 0x0F;
                comp.qt = d.pos[2];
                seg_consume(d, 3);
                if (comp.ssx < 1 || comp.ssx > 4 || comp.ssy < 1 || comp.ssy > 4 ||
                    comp.qt >= max_quant_tables) {
                    fail(d, CodecStatus::Unsupported);
                    return false;
                }
                if (comp.ssx > hmax) hmax = comp.ssx;
                if (comp.ssy > vmax) vmax = comp.ssy;
                mcu_blocks += comp.ssx * comp.ssy;
            }
            if (mcu_blocks > 10) {
                fail(d, CodecStatus::Unsupported);
                return false;
            }
            d.mcux = (d.width + 8 * hmax - 1) / (8 * hmax);
            d.mcuy = (d.height + 8 * vmax - 1) / (8 * vmax);
            for (hcpp::i32 i = 0; i < d.comp_count; ++i) {
                Component &comp = d.comp[i];
                comp.stride = d.mcux * comp.ssx * 8;
                comp.plane_h = d.mcuy * comp.ssy * 8;
                comp.dc_pred = 0;
                comp.pixels = static_cast<hcpp::u8 *>(
                    arena_alloc(*d.arena,
                                static_cast<hcpp::size_t>(comp.stride) * comp.plane_h, 16));
                if (comp.pixels == nullptr) {
                    fail(d, CodecStatus::NoSpace);
                    return false;
                }
            }
            d.frame_seen = true;
            return true;
        }

        bool parse_sos(Decoder &d, hcpp::i32 length) noexcept {
            if (!d.frame_seen || length < 1 || d.remain < 1) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            const hcpp::i32 ns = d.pos[0];
            seg_consume(d, 1);
            --length;
            if (ns != d.comp_count || length != ns * 2 + 3 ||
                static_cast<hcpp::size_t>(length) > d.remain) {
                fail(d, CodecStatus::BadInput);
                return false;
            }
            for (hcpp::i32 i = 0; i < ns; ++i) {
                const hcpp::u8 id = d.pos[0];
                const hcpp::u8 td_ta = d.pos[1];
                seg_consume(d, 2);
                Component *comp = nullptr;
                for (hcpp::i32 c = 0; c < d.comp_count; ++c)
                    if (d.comp[c].id == id) comp = &d.comp[c];
                if (comp == nullptr || (td_ta >> 4) >= max_huff_tables ||
                    (td_ta & 0x0F) >= max_huff_tables) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
                comp->dc_tab = td_ta >> 4;
                comp->ac_tab = td_ta & 0x0F;
            }
            seg_consume(d, 3);
            for (hcpp::i32 i = 0; i < d.comp_count; ++i)
                if (!d.qt_set[d.comp[i].qt] || !d.dc[d.comp[i].dc_tab].valid ||
                    !d.ac[d.comp[i].ac_tab].valid) {
                    fail(d, CodecStatus::BadInput);
                    return false;
                }
            d.bit_buf = 0;
            d.bit_num = 0;
            if (!decode_scan(d)) return false;
            d.scan_done = true;
            return true;
        }

        hcpp::i32 sample_linear(const Component &comp, hcpp::i32 x, hcpp::i32 y,
                                hcpp::i32 hmax, hcpp::i32 vmax) noexcept {
            if (comp.ssx == hmax && comp.ssy == vmax)
                return comp.pixels[y * comp.stride + x];
            hcpp::i32 cx = ((2 * x + 1) * comp.ssx * 256) / (2 * hmax) - 128;
            hcpp::i32 cy = ((2 * y + 1) * comp.ssy * 256) / (2 * vmax) - 128;
            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            const hcpp::i32 x0 = cx >> 8;
            const hcpp::i32 y0 = cy >> 8;
            const hcpp::i32 fx = cx & 255;
            const hcpp::i32 fy = cy & 255;
            const hcpp::i32 x1 = x0 + 1 < comp.stride ? x0 + 1 : x0;
            const hcpp::i32 y1 = y0 + 1 < comp.plane_h ? y0 + 1 : y0;
            const hcpp::i32 s00 = comp.pixels[y0 * comp.stride + x0];
            const hcpp::i32 s01 = comp.pixels[y0 * comp.stride + x1];
            const hcpp::i32 s10 = comp.pixels[y1 * comp.stride + x0];
            const hcpp::i32 s11 = comp.pixels[y1 * comp.stride + x1];
            const hcpp::i32 top = s00 + (((s01 - s00) * fx) >> 8);
            const hcpp::i32 bot = s10 + (((s11 - s10) * fx) >> 8);
            return top + (((bot - top) * fy) >> 8);
        }

        void convert(const Decoder &d, FrameBuf &fb) noexcept {
            hcpp::i32 hmax = 1;
            hcpp::i32 vmax = 1;
            for (hcpp::i32 i = 0; i < d.comp_count; ++i) {
                if (d.comp[i].ssx > hmax) hmax = d.comp[i].ssx;
                if (d.comp[i].ssy > vmax) vmax = d.comp[i].ssy;
            }
            for (hcpp::i32 y = 0; y < d.height; ++y) {
                hcpp::u8 *row = fb.data + static_cast<hcpp::size_t>(y) * fb.pitch;
                for (hcpp::i32 x = 0; x < d.width; ++x) {
                    if (d.comp_count == 1) {
                        const hcpp::u8 g =
                            clamp8(sample_linear(d.comp[0], x, y, hmax, vmax));
                        row[x * 4 + 0] = g;
                        row[x * 4 + 1] = g;
                        row[x * 4 + 2] = g;
                        row[x * 4 + 3] = 0xFF;
                    } else {
                        const hcpp::i32 ys =
                            sample_linear(d.comp[0], x, y, hmax, vmax);
                        const hcpp::i32 cb =
                            sample_linear(d.comp[1], x, y, hmax, vmax) - 128;
                        const hcpp::i32 cr =
                            sample_linear(d.comp[2], x, y, hmax, vmax) - 128;
                        row[x * 4 + 0] = clamp8(ys + ((359 * cr + 128) >> 8));
                        row[x * 4 + 1] = clamp8(ys - ((88 * cb + 183 * cr + 128) >> 8));
                        row[x * 4 + 2] = clamp8(ys + ((454 * cb + 128) >> 8));
                        row[x * 4 + 3] = 0xFF;
                    }
                }
            }
        }

        CodecStatus run_decoder(const hcpp::u8 *input, hcpp::size_t input_size,
                                FrameBuf &fb, Arena &arena) noexcept {
            Decoder d = {};
            d.pos = input;
            d.remain = input_size;
            d.error = CodecStatus::Ok;
            d.arena = &arena;
            if (input_size < 4 || input[0] != 0xFF || input[1] != 0xD8)
                return CodecStatus::BadInput;
            d.pos += 2;
            d.remain -= 2;
            bool eoi = false;
            while (!eoi && d.error == CodecStatus::Ok) {
                hcpp::u8 marker = 0;
                if (!read_marker(d, marker)) break;
                if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
                    continue;
                if (marker == 0xD9) {
                    eoi = true;
                    break;
                }
                const hcpp::i32 length = seg_length(d);
                if (length < 0) break;
                bool ok = true;
                switch (marker) {
                    case 0xC0:
                    case 0xC1:
                        ok = parse_sof(d, length);
                        break;
                    case 0xC2:
                    case 0xC3:
                    case 0xC5:
                    case 0xC6:
                    case 0xC7:
                    case 0xC9:
                    case 0xCA:
                    case 0xCB:
                    case 0xCD:
                    case 0xCE:
                    case 0xCF:
                    case 0xCC:
                        fail(d, CodecStatus::Unsupported);
                        break;
                    case 0xC4:
                        ok = parse_dht(d, length);
                        break;
                    case 0xDB:
                        ok = parse_dqt(d, length);
                        break;
                    case 0xDD:
                        if (length != 2) {
                            fail(d, CodecStatus::BadInput);
                        } else {
                            d.restart_interval = (d.pos[0] << 8) | d.pos[1];
                            seg_consume(d, 2);
                        }
                        break;
                    case 0xDA:
                        ok = parse_sos(d, length);
                        break;
                    default:
                        seg_consume(d, static_cast<hcpp::size_t>(length));
                        break;
                }
                if (!ok || d.error != CodecStatus::Ok) break;
            }
            if (d.error != CodecStatus::Ok) return d.error;
            if (!d.scan_done) return CodecStatus::BadInput;
            const CodecStatus prepared =
                frame_prepare(&fb, static_cast<hcpp::u32>(d.width),
                              static_cast<hcpp::u32>(d.height), arena);
            if (prepared != CodecStatus::Ok) return prepared;
            convert(d, fb);
            return CodecStatus::Ok;
        }

        hcpp::u32 fnv1a(const hcpp::u8 *data, hcpp::size_t size) noexcept {
            hcpp::u32 hash = 2166136261u;
            for (hcpp::size_t i = 0; i < size; ++i) {
                hash ^= data[i];
                hash *= 16777619u;
            }
            return hash;
        }

    }  // namespace

    CodecStatus jpeg_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                            Arena &arena) noexcept {
        if (input == nullptr || output == nullptr) return CodecStatus::BadInput;
        return run_decoder(input, input_size, *static_cast<FrameBuf *>(output), arena);
    }

    bool jpeg_selftest() noexcept {
        static hcpp::u8 backing[8192];
        static hcpp::u8 frame_storage[JPEG_SELFTEST_WIDTH * JPEG_SELFTEST_HEIGHT * 4];
        Arena arena{backing, sizeof(backing), 0};
        FrameBuf fb = {};
        fb.data = frame_storage;
        fb.capacity = sizeof(frame_storage);
        const CodecStatus status = jpeg_decode(jpeg_selftest_blob,
                                               sizeof(jpeg_selftest_blob), &fb, arena);
        if (status != CodecStatus::Ok) return false;
        if (fb.width != JPEG_SELFTEST_WIDTH || fb.height != JPEG_SELFTEST_HEIGHT)
            return false;
        if (fb.format != video::PixelFormat::RGB32 ||
            fb.size != JPEG_SELFTEST_WIDTH * JPEG_SELFTEST_HEIGHT * 4)
            return false;
        hcpp::u32 distinct = 0;
        for (hcpp::size_t i = 0; i + 1 < fb.size; ++i)
            if (fb.data[i] != fb.data[i + 1]) ++distinct;
        if (distinct < 200) return false;
        return fnv1a(fb.data, fb.size) == 0xBB39240Eu;
    }

}  // namespace hype::codec::image
