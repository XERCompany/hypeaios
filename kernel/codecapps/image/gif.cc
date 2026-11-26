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

    }  // namespace

    CodecStatus gif_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (src == nullptr || frame == nullptr || size < 13 ||
            !(bytes_eq(src, size, 0, "GIF87a", 6) || bytes_eq(src, size, 0, "GIF89a", 6)))
            return CodecStatus::BadInput;
        const hcpp::u8 packed = src[10];
        const bool has_gct = (packed & 0x80) != 0;
        const hcpp::u32 gct_size = 1u << ((packed & 0x07) + 1);
        hcpp::size_t pos = 13;
        const hcpp::u8 *gct = nullptr;
        if (has_gct) {
            if (!bounds_ok(pos, 3 * gct_size, size)) return CodecStatus::BadInput;
            gct = src + pos;
            pos += 3 * gct_size;
        }
        for (;;) {
            if (pos >= size) return CodecStatus::BadInput;
            const hcpp::u8 sep = src[pos++];
            if (sep == 0x3B) return CodecStatus::BadInput;
            if (sep == 0x21) {
                if (pos >= size) return CodecStatus::BadInput;
                ++pos;
                if (!skip_sub_blocks(src, size, pos)) return CodecStatus::BadInput;
                continue;
            }
            if (sep != 0x2C) return CodecStatus::BadInput;
            break;
        }
        if (!bounds_ok(pos, 9, size)) return CodecStatus::BadInput;
        const hcpp::u32 iw = read_le16(src + pos + 4);
        const hcpp::u32 ih = read_le16(src + pos + 6);
        const hcpp::u8 ipacked = src[pos + 8];
        pos += 9;
        const bool interlaced = (ipacked & 0x40) != 0;
        const bool has_lct = (ipacked & 0x80) != 0;
        const hcpp::u8 *palette = gct;
        hcpp::u32 palette_size = has_gct ? gct_size : 0;
        if (has_lct) {
            palette_size = 1u << ((ipacked & 0x07) + 1);
            if (!bounds_ok(pos, 3 * palette_size, size)) return CodecStatus::BadInput;
            palette = src + pos;
            pos += 3 * palette_size;
        }
        if (palette == nullptr || !dims_valid(iw, ih)) return CodecStatus::BadInput;
        if (pos >= size) return CodecStatus::BadInput;
        const hcpp::u8 min_code_size = src[pos++];
        if (min_code_size < 2 || min_code_size > 8) return CodecStatus::BadInput;
        const hcpp::size_t total = static_cast<hcpp::size_t>(iw) * ih;
        hcpp::u8 *indices = static_cast<hcpp::u8 *>(arena_alloc(arena, total, 8));
        if (indices == nullptr) return CodecStatus::NoSpace;
        GifBitReader reader{src, size, pos, 0, 0, 0};
        const hcpp::size_t produced = lzw_decompress(reader, min_code_size, indices, total);
        if (produced < total) return CodecStatus::BadInput;
        const hcpp::u8 *raster = indices;
        if (interlaced) {
            hcpp::u8 *final = static_cast<hcpp::u8 *>(arena_alloc(arena, total, 8));
            if (final == nullptr) return CodecStatus::NoSpace;
            const hcpp::u8 pass_start[4] = {0, 4, 2, 1};
            const hcpp::u8 pass_step[4] = {8, 8, 4, 2};
            hcpp::size_t cursor = 0;
            for (hcpp::u32 pass = 0; pass < 4; ++pass) {
                for (hcpp::u32 row = pass_start[pass]; row < ih; row += pass_step[pass]) {
                    if (cursor + iw > total) return CodecStatus::BadInput;
                    hcpp::u8 *dst = final + static_cast<hcpp::size_t>(row) * iw;
                    const hcpp::u8 *srcrow = indices + cursor;
                    for (hcpp::u32 x = 0; x < iw; ++x) dst[x] = srcrow[x];
                    cursor += iw;
                }
            }
            raster = final;
        }
        const CodecStatus status = frame_prepare(frame, iw, ih, arena);
        if (status != CodecStatus::Ok) return status;
        for (hcpp::u32 y = 0; y < ih; ++y) {
            for (hcpp::u32 x = 0; x < iw; ++x) {
                const hcpp::u8 index = raster[static_cast<hcpp::size_t>(y) * iw + x];
                const hcpp::u32 pi = index < palette_size ? index : 0;
                put_pixel(*frame, y * iw + x, palette[pi * 3 + 0], palette[pi * 3 + 1],
                          palette[pi * 3 + 2], 0xFF);
            }
        }
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
