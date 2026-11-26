#include "kernel/codecapps/image/bmp.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {
        constexpr hcpp::size_t bmp_file_header_size = 14;
        constexpr hcpp::size_t bmp_info_header_size = 40;
        constexpr hcpp::u32 bmp_palette_max = 256;
    }  // namespace

    CodecStatus bmp_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (input == nullptr || frame == nullptr || input_size < 26 || input[0] != 'B' ||
            input[1] != 'M')
            return CodecStatus::BadInput;
        const hcpp::u32 data_offset = read_le32(input + 10);
        const hcpp::u32 dib_size = read_le32(input + 14);
        hcpp::i32 width = 0;
        hcpp::i32 height = 0;
        hcpp::u16 bpp = 0;
        hcpp::u32 compression = 0;
        hcpp::u32 colors_used = 0;
        hcpp::size_t palette_offset = 0;
        hcpp::u32 palette_entry = 4;
        if (dib_size == 12) {
            width = read_le16(input + 18);
            height = read_le16(input + 20);
            if (read_le16(input + 22) != 1) return CodecStatus::BadInput;
            bpp = read_le16(input + 24);
            palette_offset = bmp_file_header_size + dib_size;
            palette_entry = 3;
        } else {
            if (dib_size < bmp_info_header_size || dib_size > 1024 ||
                bmp_file_header_size + dib_size > input_size)
                return CodecStatus::BadInput;
            width = static_cast<hcpp::i32>(read_le32(input + 18));
            height = static_cast<hcpp::i32>(read_le32(input + 22));
            if (read_le16(input + 26) != 1) return CodecStatus::BadInput;
            bpp = read_le16(input + 28);
            compression = read_le32(input + 30);
            colors_used = read_le32(input + 46);
            palette_offset = bmp_file_header_size + dib_size;
        }
        if (compression != 0) return CodecStatus::Unsupported;
        if (width <= 0 || height == 0 || height == static_cast<hcpp::i32>(0x80000000))
            return CodecStatus::BadInput;
        const hcpp::u32 w = static_cast<hcpp::u32>(width);
        const hcpp::u32 h = height < 0 ? static_cast<hcpp::u32>(-height)
                                     : static_cast<hcpp::u32>(height);
        const bool top_down = height < 0;
        if (!dims_valid(w, h)) return CodecStatus::BadInput;
        if (bpp != 8 && bpp != 24 && bpp != 32) return CodecStatus::Unsupported;
        hcpp::u32 palette_colors = 0;
        if (bpp == 8) {
            palette_colors = colors_used != 0 ? colors_used : bmp_palette_max;
            if (palette_colors > bmp_palette_max) palette_colors = bmp_palette_max;
            if (palette_offset > input_size ||
                static_cast<hcpp::size_t>(palette_colors) * palette_entry >
                    input_size - palette_offset)
                return CodecStatus::BadInput;
        }
        const hcpp::size_t stride =
            (static_cast<hcpp::size_t>(w) * bpp + 31) / 32 * 4;
        if (data_offset > input_size) return CodecStatus::BadInput;
        const hcpp::size_t avail = input_size - data_offset;
        if (stride == 0 || static_cast<hcpp::size_t>(h) > avail / stride)
            return CodecStatus::BadInput;
        const CodecStatus prepared = frame_prepare(frame, w, h, arena);
        if (prepared != CodecStatus::Ok) return prepared;
        for (hcpp::u32 y = 0; y < h; ++y) {
            const hcpp::u32 src_y = top_down ? y : h - 1 - y;
            const hcpp::u8 *row =
                input + data_offset + static_cast<hcpp::size_t>(src_y) * stride;
            for (hcpp::u32 x = 0; x < w; ++x) {
                hcpp::u8 r = 0, g = 0, b = 0, a = 0xFF;
                if (bpp == 24) {
                    const hcpp::u8 *px = row + static_cast<hcpp::size_t>(x) * 3;
                    b = px[0];
                    g = px[1];
                    r = px[2];
                } else if (bpp == 32) {
                    const hcpp::u8 *px = row + static_cast<hcpp::size_t>(x) * 4;
                    b = px[0];
                    g = px[1];
                    r = px[2];
                    a = px[3];
                } else {
                    const hcpp::u8 index = row[x];
                    if (index >= palette_colors) return CodecStatus::BadInput;
                    const hcpp::u8 *entry =
                        input + palette_offset +
                        static_cast<hcpp::size_t>(index) * palette_entry;
                    b = entry[0];
                    g = entry[1];
                    r = entry[2];
                }
                put_pixel(*frame, y * w + x, r, g, b, a);
            }
        }
        return CodecStatus::Ok;
    }

    CodecStatus bmp_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &) noexcept {
        written = 0;
        const FrameBuf *frame = static_cast<const FrameBuf *>(input);
        if (!frame_source_valid(frame) || output == nullptr) return CodecStatus::BadInput;
        const hcpp::u32 w = frame->width;
        const hcpp::u32 h = frame->height;
        const hcpp::size_t row_size = (static_cast<hcpp::size_t>(w) * 3 + 3) & ~3ULL;
        const hcpp::size_t pixel_bytes = row_size * h;
        const hcpp::size_t total = bmp_file_header_size + bmp_info_header_size + pixel_bytes;
        if (output_cap < total) return CodecStatus::NoSpace;
        for (hcpp::size_t i = 0; i < bmp_file_header_size + bmp_info_header_size; ++i)
            output[i] = 0;
        output[0] = 'B';
        output[1] = 'M';
        write_le32(output + 2, static_cast<hcpp::u32>(total));
        write_le32(output + 10, 54);
        write_le32(output + 14, 40);
        write_le32(output + 18, w);
        write_le32(output + 22, h);
        write_le16(output + 26, 1);
        write_le16(output + 28, 24);
        write_le32(output + 34, static_cast<hcpp::u32>(pixel_bytes));
        write_le32(output + 38, 2835);
        write_le32(output + 42, 2835);
        for (hcpp::u32 y = 0; y < h; ++y) {
            hcpp::u8 *row = output + 54 + static_cast<hcpp::size_t>(h - 1 - y) * row_size;
            for (hcpp::u32 x = 0; x < w; ++x) {
                hcpp::u8 *px = row + static_cast<hcpp::size_t>(x) * 3;
                px[0] = source_component(*frame, x, y, 2);
                px[1] = source_component(*frame, x, y, 1);
                px[2] = source_component(*frame, x, y, 0);
            }
            for (hcpp::size_t i = static_cast<hcpp::size_t>(w) * 3; i < row_size; ++i)
                row[i] = 0;
        }
        written = total;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
