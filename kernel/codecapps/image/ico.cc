#include "kernel/codecapps/image/ico.h"
#include "kernel/codecapps/image/imageutil.h"
#include "kernel/codecapps/registry.h"

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

        CodecStatus decode_dib(const hcpp::u8 *dib, hcpp::size_t dlen, hcpp::u32 dir_width,
                               hcpp::u32 dir_height, FrameBuf &frame,
                               Arena &arena) noexcept {
            if (dlen < 40) return CodecStatus::BadInput;
            const hcpp::u32 header_size = read_le32(dib);
            if (header_size < 40 || !bounds_ok(0, header_size, dlen))
                return CodecStatus::BadInput;
            const hcpp::u32 dib_width = read_le32(dib + 4);
            const hcpp::u32 dib_height = read_le32(dib + 8) >> 1;
            const hcpp::u16 planes = read_le16(dib + 12);
            const hcpp::u16 bpp = read_le16(dib + 14);
            const hcpp::u32 compression = read_le32(dib + 16);
            if (planes != 1 || compression != 0) return CodecStatus::Unsupported;
            if (bpp != 8 && bpp != 24 && bpp != 32) return CodecStatus::Unsupported;
            hcpp::u32 width = dir_width != 0 ? dir_width : dib_width;
            hcpp::u32 height = dir_height != 0 ? dir_height : dib_height;
            if (dib_width != 0 && dib_width < width) width = dib_width;
            if (dib_height != 0 && dib_height < height) height = dib_height;
            if (!dims_valid(width, height)) return CodecStatus::BadInput;
            hcpp::u32 palette_count = 0;
            if (bpp <= 8) {
                palette_count = read_le32(dib + 32);
                if (palette_count == 0) palette_count = 1u << bpp;
            }
            const hcpp::size_t palette_off = header_size;
            const hcpp::size_t pixel_off =
                palette_off + static_cast<hcpp::size_t>(palette_count) * 4;
            const hcpp::size_t row_size =
                ((static_cast<hcpp::size_t>(width) * bpp + 31) / 32) * 4;
            if (!bounds_ok(pixel_off, row_size * height, dlen))
                return CodecStatus::BadInput;
            const CodecStatus status = frame_prepare(&frame, width, height, arena);
            if (status != CodecStatus::Ok) return status;
            for (hcpp::u32 y = 0; y < height; ++y) {
                const hcpp::u8 *src_row = dib + pixel_off +
                                          static_cast<hcpp::size_t>(height - 1 - y) *
                                              row_size;
                for (hcpp::u32 x = 0; x < width; ++x) {
                    hcpp::u8 r = 0, g = 0, b = 0, a = 0xFF;
                    if (bpp == 8) {
                        const hcpp::u8 index =
                            src_row[x] < palette_count ? src_row[x] : 0;
                        const hcpp::u8 *pal =
                            dib + palette_off + static_cast<hcpp::size_t>(index) * 4;
                        b = pal[0];
                        g = pal[1];
                        r = pal[2];
                    } else if (bpp == 24) {
                        b = src_row[x * 3 + 0];
                        g = src_row[x * 3 + 1];
                        r = src_row[x * 3 + 2];
                    } else {
                        b = src_row[x * 4 + 0];
                        g = src_row[x * 4 + 1];
                        r = src_row[x * 4 + 2];
                        a = src_row[x * 4 + 3];
                    }
                    put_pixel(frame, y * width + x, r, g, b, a);
                }
            }
            return CodecStatus::Ok;
        }

    }  // namespace

    CodecStatus ico_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (src == nullptr || frame == nullptr || size < 6) return CodecStatus::BadInput;
        if (src[0] != 0 || src[1] != 0 || src[3] != 0 || (src[2] != 1 && src[2] != 2))
            return CodecStatus::BadInput;
        const hcpp::u16 count = read_le16(src + 4);
        if (count == 0 || !bounds_ok(6, static_cast<hcpp::size_t>(count) * 16, size))
            return CodecStatus::BadInput;
        int best = -1;
        hcpp::u32 best_pixels = 0;
        for (hcpp::u16 i = 0; i < count; ++i) {
            const hcpp::u8 *entry = src + 6 + static_cast<hcpp::size_t>(i) * 16;
            const hcpp::u32 w = entry[0] == 0 ? 256 : entry[0];
            const hcpp::u32 h = entry[1] == 0 ? 256 : entry[1];
            const hcpp::u32 bytes = read_le32(entry + 8);
            const hcpp::u32 offset = read_le32(entry + 12);
            if (!bounds_ok(offset, bytes, size)) continue;
            if (w * h >= best_pixels) {
                best = i;
                best_pixels = w * h;
            }
        }
        if (best < 0) return CodecStatus::BadInput;
        const hcpp::u8 *entry = src + 6 + static_cast<hcpp::size_t>(best) * 16;
        const hcpp::u32 dir_width = entry[0] == 0 ? 256 : entry[0];
        const hcpp::u32 dir_height = entry[1] == 0 ? 256 : entry[1];
        const hcpp::u32 bytes = read_le32(entry + 8);
        const hcpp::u32 offset = read_le32(entry + 12);
        const hcpp::u8 *data = src + offset;
        if (bytes >= 8 &&
            bytes_eq(data, bytes, 0, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8)) {
            if (codec_info(CodecId::Png) == nullptr) return CodecStatus::Unsupported;
            return codec_decode(CodecId::Png, data, bytes, output, arena);
        }
        return decode_dib(data, bytes, dir_width, dir_height, *frame, arena);
    }

}  // namespace hype::codec::image
