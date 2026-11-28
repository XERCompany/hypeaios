#include "kernel/codecapps/image/gif.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {

        bool bounds_ok(hcpp::size_t offset, hcpp::size_t length,
                       hcpp::size_t total) noexcept {
            return offset <= total && length <= total - offset;
        }

        bool bytes_eq(const hcpp::u8 *buffer, hcpp::size_t size, hcpp::size_t offset,
                      const char *sig, hcpp::size_t sig_size) noexcept {
            if (buffer == nullptr || !bounds_ok(offset, sig_size, size)) return false;
            for (hcpp::size_t i = 0; i < sig_size; ++i)
                if (buffer[offset + i] != static_cast<hcpp::u8>(sig[i])) return false;
            return true;
        }

        struct GifBitReader {
            const hcpp::u8 *src;
            hcpp::size_t size;
            hcpp::size_t pos;
            hcpp::size_t block_left;
            hcpp::u32 bit_buffer;
            hcpp::u32 bit_count;

            int read(hcpp::u32 width) noexcept {
                while (bit_count < width) {
                    if (block_left == 0) {
                        if (pos >= size) return -1;
                        const hcpp::u8 len = src[pos++];
                        block_left = len;
                        if (len == 0) return -1;
                    }
                    if (pos >= size) return -1;
                    bit_buffer |= static_cast<hcpp::u32>(src[pos++]) << bit_count;
                    bit_count += 8;
                    --block_left;
                }
                const int code = static_cast<int>(bit_buffer & ((1u << width) - 1u));
                bit_buffer >>= width;
                bit_count -= width;
                return code;
            }
        };

        hcpp::size_t lzw_decompress(GifBitReader &reader, hcpp::u8 min_code_size,
                                    hcpp::u8 *out, hcpp::size_t out_cap) noexcept {
            static hcpp::u16 prefix[4096];
            static hcpp::u8 suffix[4096];
            static hcpp::u8 stack[4096];
            const hcpp::u32 clear = 1u << min_code_size;
            const hcpp::u32 eoi = clear + 1;
            hcpp::u32 width = min_code_size + 1;
            hcpp::u32 next = eoi + 1;
            hcpp::size_t produced = 0;
            hcpp::u8 first = 0;
            int previous = -1;
            for (;;) {
                const int code = reader.read(width);
                if (code < 0) return produced;
                if (static_cast<hcpp::u32>(code) == eoi) return produced;
                if (static_cast<hcpp::u32>(code) == clear) {
                    width = min_code_size + 1;
                    next = eoi + 1;
                    previous = -1;
                    continue;
                }
                if (previous < 0) {
                    if (static_cast<hcpp::u32>(code) >= clear || produced >= out_cap)
                        return produced;
                    first = static_cast<hcpp::u8>(code);
                    out[produced++] = first;
                    previous = code;
                    continue;
                }
                const hcpp::u32 in_code = static_cast<hcpp::u32>(code);
                hcpp::u32 walk;
                hcpp::size_t sp = 0;
                if (in_code == next) {
                    stack[sp++] = first;
                    walk = static_cast<hcpp::u32>(previous);
                } else if (in_code < next) {
                    walk = in_code;
                } else {
                    return produced;
                }
                while (walk >= clear) {
                    if (walk >= 4096 || sp >= sizeof(stack)) return produced;
                    stack[sp++] = suffix[walk];
                    walk = prefix[walk];
                }
                first = static_cast<hcpp::u8>(walk);
                stack[sp++] = first;
                if (next < 4096) {
                    prefix[next] = static_cast<hcpp::u16>(previous);
                    suffix[next] = first;
                    ++next;
                    if (next == (1u << width) && width < 12) ++width;
                }
                while (sp != 0) {
                    if (produced >= out_cap) return produced;
                    out[produced++] = stack[--sp];
                }
                previous = static_cast<int>(in_code);
            }
        }

        bool skip_sub_blocks(const hcpp::u8 *src, hcpp::size_t size,
                             hcpp::size_t &pos) noexcept {
            for (;;) {
                if (pos >= size) return false;
                const hcpp::u8 len = src[pos++];
                if (len == 0) return true;
                if (!bounds_ok(pos, len, size)) return false;
                pos += len;
            }
        }

        void fill_rect(FrameBuf &canvas, hcpp::u32 x, hcpp::u32 y, hcpp::u32 w,
                       hcpp::u32 h, hcpp::u8 r, hcpp::u8 g, hcpp::u8 b) noexcept {
            for (hcpp::u32 yy = 0; yy < h && y + yy < canvas.height; ++yy)
                for (hcpp::u32 xx = 0; xx < w && x + xx < canvas.width; ++xx)
                    put_pixel(canvas, (y + yy) * canvas.width + x + xx, r, g, b, 0xFF);
        }

        void bg_color(const GifAnim &anim, hcpp::u8 &r, hcpp::u8 &g,
                      hcpp::u8 &b) noexcept {
            r = g = b = 0;
            if (anim.gct != nullptr && anim.bg_index < anim.gct_size) {
                r = anim.gct[anim.bg_index * 3 + 0];
                g = anim.gct[anim.bg_index * 3 + 1];
                b = anim.gct[anim.bg_index * 3 + 2];
            }
        }

        // Decompress one image descriptor's LZW payload into `indices`
        // (w*h bytes), applying the interlace row order when flagged.
        // `pos` advances past the trailing sub-block terminator.
        bool read_image_raster(const hcpp::u8 *src, hcpp::size_t size, hcpp::size_t &pos,
                               hcpp::u32 iw, hcpp::u32 ih, bool interlaced, hcpp::u8 *indices,
                               hcpp::size_t total, Arena &arena,
                               const hcpp::u8 *&raster) noexcept {
            if (pos >= size) return false;
            const hcpp::u8 min_code_size = src[pos++];
            if (min_code_size < 2 || min_code_size > 8) return false;
            GifBitReader reader{src, size, pos, 0, 0, 0};
            const hcpp::size_t produced = lzw_decompress(reader, min_code_size, indices, total);
            // Resync past the LZW sub-block chain: the reader may stop mid-block.
            if (reader.block_left > size - reader.pos) return false;
            pos = reader.pos + reader.block_left;
            if (!skip_sub_blocks(src, size, pos)) return false;
            if (produced < total) return false;
            raster = indices;
            if (!interlaced) return true;
            hcpp::u8 *final = static_cast<hcpp::u8 *>(arena_alloc(arena, total, 8));
            if (final == nullptr) return false;
            const hcpp::u8 pass_start[4] = {0, 4, 2, 1};
            const hcpp::u8 pass_step[4] = {8, 8, 4, 2};
            hcpp::size_t cursor = 0;
            for (hcpp::u32 pass = 0; pass < 4; ++pass) {
                for (hcpp::u32 row = pass_start[pass]; row < ih; row += pass_step[pass]) {
                    if (cursor + iw > total) return false;
                    hcpp::u8 *dst = final + static_cast<hcpp::size_t>(row) * iw;
                    const hcpp::u8 *srcrow = indices + cursor;
                    for (hcpp::u32 x = 0; x < iw; ++x) dst[x] = srcrow[x];
                    cursor += iw;
                }
            }
            raster = final;
            return true;
        }

        // Scans the block stream for the next image descriptor, consuming
        // GCE/app/comment extensions on the way. Returns false at the trailer.
        bool next_image(GifAnim &anim) noexcept {
            const hcpp::u8 *src = anim.src;
            const hcpp::size_t size = anim.size;
            for (;;) {
                if (anim.pos >= size) return false;
                const hcpp::u8 sep = src[anim.pos++];
                if (sep == 0x3B) return false;
                if (sep == 0x21) {
                    if (anim.pos >= size) return false;
                    const hcpp::u8 label = src[anim.pos++];
                    if (label == 0xF9) {
                        // Graphic Control Extension: size(4) packed delay lo/hi
                        // transparent-index terminator — carried as sub-blocks.
                        if (anim.pos >= size) return false;
                        const hcpp::u8 blen = src[anim.pos++];
                        if (!bounds_ok(anim.pos, blen, size)) return false;
                        if (blen >= 4) {
                            const hcpp::u8 packed = src[anim.pos];
                            anim.disposal = static_cast<hcpp::u8>((packed >> 2) & 0x07);
                            anim.has_transparent = (packed & 1) != 0;
                            anim.delay_cs = read_le16(src + anim.pos + 1);
                            anim.transparent = src[anim.pos + 3];
                        }
                        anim.pos += blen;
                        if (!skip_sub_blocks(src, size, anim.pos)) return false;
                    } else if (label == 0xFF) {
                        // Application extension: 11-byte id block then data.
                        if (anim.pos >= size) return false;
                        const hcpp::u8 blen = src[anim.pos++];
                        if (!bounds_ok(anim.pos, blen, size)) return false;
                        const bool netscape =
                            blen == 11 &&
                            bytes_eq(src, size, anim.pos, "NETSCAPE2.0", 11);
                        anim.pos += blen;
                        if (netscape && bounds_ok(anim.pos, 4, size) &&
                            src[anim.pos] == 3 && src[anim.pos + 1] == 1)
                            anim.loop_count = read_le16(src + anim.pos + 2);
                        if (!skip_sub_blocks(src, size, anim.pos)) return false;
                    } else {
                        // Comment / plain-text / unknown extensions.
                        if (!skip_sub_blocks(src, size, anim.pos)) return false;
                    }
                    continue;
                }
                if (sep != 0x2C) return false;
                return true;
            }
        }

    }  // namespace

    bool gif_anim_open(GifAnim &anim, const hcpp::u8 *src, hcpp::size_t size) noexcept {
        anim = GifAnim{};
        if (src == nullptr || size < 13 ||
            !(bytes_eq(src, size, 0, "GIF87a", 6) || bytes_eq(src, size, 0, "GIF89a", 6)))
            return false;
        anim.src = src;
        anim.size = size;
        anim.screen_w = read_le16(src + 6);
        anim.screen_h = read_le16(src + 8);
        const hcpp::u8 packed = src[10];
        const bool has_gct = (packed & 0x80) != 0;
        anim.gct_size = 1u << ((packed & 0x07) + 1);
        anim.bg_index = src[11];
        anim.loop_count = 0xFFFF;
        anim.pos = 13;
        if (!dims_valid(anim.screen_w, anim.screen_h)) return false;
        if (has_gct) {
            if (!bounds_ok(anim.pos, 3 * anim.gct_size, size)) return false;
            anim.gct = src + anim.pos;
            anim.pos += 3 * anim.gct_size;
        }
        return true;
    }

    bool gif_anim_next(GifAnim &anim, FrameBuf &canvas, hcpp::u16 &delay_cs,
                       Arena &arena) noexcept {
        if (anim.frames == 0) {
            if (frame_prepare(&canvas, anim.screen_w, anim.screen_h, arena) !=
                CodecStatus::Ok)
                return false;
            hcpp::u8 r, g, b;
            bg_color(anim, r, g, b);
            fill_rect(canvas, 0, 0, anim.screen_w, anim.screen_h, r, g, b);
        } else if (anim.prev_disposal == 2) {
            hcpp::u8 r, g, b;
            bg_color(anim, r, g, b);
            fill_rect(canvas, anim.prev_x, anim.prev_y, anim.prev_w, anim.prev_h, r, g, b);
        } else if (anim.prev_disposal == 3 && anim.restore != nullptr) {
            for (hcpp::u32 yy = 0; yy < anim.prev_h; ++yy) {
                hcpp::u8 *row = canvas.data +
                                static_cast<hcpp::size_t>(anim.prev_y + yy) * canvas.pitch +
                                static_cast<hcpp::size_t>(anim.prev_x) * 4;
                const hcpp::u8 *saved = anim.restore + static_cast<hcpp::size_t>(yy) *
                                                           anim.prev_w * 4;
                for (hcpp::u32 k = 0; k < anim.prev_w * 4; ++k) row[k] = saved[k];
            }
        }
        if (!next_image(anim)) return false;
        if (!bounds_ok(anim.pos, 9, anim.size)) return false;
        const hcpp::u32 ix = read_le16(anim.src + anim.pos + 0);
        const hcpp::u32 iy = read_le16(anim.src + anim.pos + 2);
        const hcpp::u32 iw = read_le16(anim.src + anim.pos + 4);
        const hcpp::u32 ih = read_le16(anim.src + anim.pos + 6);
        const hcpp::u8 ipacked = anim.src[anim.pos + 8];
        anim.pos += 9;
        const bool interlaced = (ipacked & 0x40) != 0;
        const bool has_lct = (ipacked & 0x80) != 0;
        const hcpp::u8 *palette = anim.gct;
        hcpp::u32 palette_size = anim.gct != nullptr ? anim.gct_size : 0;
        if (has_lct) {
            palette_size = 1u << ((ipacked & 0x07) + 1);
            if (!bounds_ok(anim.pos, 3 * palette_size, anim.size)) return false;
            palette = anim.src + anim.pos;
            anim.pos += 3 * palette_size;
        }
        if (palette == nullptr || iw == 0 || ih == 0 ||
            iw > image_max_dimension || ih > image_max_dimension)
            return false;
        const hcpp::size_t total = static_cast<hcpp::size_t>(iw) * ih;
        hcpp::u8 *indices = static_cast<hcpp::u8 *>(arena_alloc(arena, total, 8));
        if (indices == nullptr) return false;
        const hcpp::u8 *raster = nullptr;
        if (!read_image_raster(anim.src, anim.size, anim.pos, iw, ih, interlaced, indices,
                               total, arena, raster))
            return false;
        // Clip the frame rect against the logical screen.
        const hcpp::u32 cw = ix < anim.screen_w
                                 ? (iw < anim.screen_w - ix ? iw : anim.screen_w - ix)
                                 : 0;
        const hcpp::u32 ch = iy < anim.screen_h
                                 ? (ih < anim.screen_h - iy ? ih : anim.screen_h - iy)
                                 : 0;
        if (anim.disposal == 3 && cw != 0 && ch != 0) {
            const hcpp::size_t need = static_cast<hcpp::size_t>(cw) * ch * 4;
            if (anim.restore_cap < need) {
                anim.restore = static_cast<hcpp::u8 *>(arena_alloc(arena, need, 8));
                if (anim.restore == nullptr) return false;
                anim.restore_cap = need;
            }
            for (hcpp::u32 yy = 0; yy < ch; ++yy) {
                const hcpp::u8 *row = canvas.data +
                                      static_cast<hcpp::size_t>(iy + yy) * canvas.pitch +
                                      static_cast<hcpp::size_t>(ix) * 4;
                hcpp::u8 *saved = anim.restore + static_cast<hcpp::size_t>(yy) * cw * 4;
                for (hcpp::u32 k = 0; k < cw * 4; ++k) saved[k] = row[k];
            }
        }
        for (hcpp::u32 yy = 0; yy < ch; ++yy) {
            for (hcpp::u32 xx = 0; xx < cw; ++xx) {
                const hcpp::u8 index = raster[static_cast<hcpp::size_t>(yy) * iw + xx];
                if (anim.has_transparent && index == anim.transparent) continue;
                const hcpp::u32 pi = index < palette_size ? index : 0;
                put_pixel(canvas, (iy + yy) * canvas.width + ix + xx,
                          palette[pi * 3 + 0], palette[pi * 3 + 1], palette[pi * 3 + 2],
                          0xFF);
            }
        }
        anim.prev_disposal = anim.disposal;
        anim.prev_x = ix;
        anim.prev_y = iy;
        anim.prev_w = cw;
        anim.prev_h = ch;
        // The GCE applies to the following image only — reset to defaults.
        anim.disposal = 0;
        anim.has_transparent = false;
        delay_cs = anim.delay_cs;
        ++anim.frames;
        return true;
    }

    hcpp::u32 gif_frame_count(const hcpp::u8 *src, hcpp::size_t size) noexcept {
        GifAnim anim;
        if (!gif_anim_open(anim, src, size)) return 0;
        hcpp::u32 count = 0;
        while (anim.pos < anim.size) {
            const hcpp::u8 sep = anim.src[anim.pos++];
            if (sep == 0x3B) break;
            if (sep == 0x21) {
                if (anim.pos >= anim.size) break;
                ++anim.pos;
                if (!skip_sub_blocks(anim.src, anim.size, anim.pos)) break;
                continue;
            }
            if (sep != 0x2C) break;
            if (!bounds_ok(anim.pos, 9, anim.size)) break;
            const hcpp::u8 ipacked = anim.src[anim.pos + 8];
            anim.pos += 9;
            if ((ipacked & 0x80) != 0) {
                const hcpp::u32 lct = 3u * (1u << ((ipacked & 0x07) + 1));
                if (!bounds_ok(anim.pos, lct, anim.size)) break;
                anim.pos += lct;
            }
            if (anim.pos >= anim.size) break;
            ++anim.pos;  // LZW min code size
            if (!skip_sub_blocks(anim.src, anim.size, anim.pos)) break;
            ++count;
        }
        return count;
    }

    CodecStatus gif_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (src == nullptr || frame == nullptr) return CodecStatus::BadInput;
        GifAnim anim;
        if (!gif_anim_open(anim, src, size)) return CodecStatus::BadInput;
        hcpp::u16 delay_cs = 0;
        if (!gif_anim_next(anim, *frame, delay_cs, arena)) return CodecStatus::BadInput;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
