#include "kernel/codecapps/image/tga.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {
        constexpr char tga_signature[] = "TRUEVISION-XFILE.";
        constexpr hcpp::size_t tga_header_size = 18;
        constexpr hcpp::size_t tga_footer_size = 26;

        bool tga_type_known(hcpp::u8 type) noexcept {
            return type == 1 || type == 2 || type == 3 || type == 9 || type == 10 ||
                   type == 11;
        }

        bool tga_footer_match(const hcpp::u8 *input, hcpp::size_t size) noexcept {
            if (size < tga_header_size) return false;
            const hcpp::size_t base = size - tga_header_size;
            for (hcpp::size_t i = 0; i < 17; ++i)
                if (input[base + i] != static_cast<hcpp::u8>(tga_signature[i])) return false;
            return input[size - 1] == 0;
        }

        bool tga_header_plausible(const hcpp::u8 *input, hcpp::size_t size) noexcept {
            if (size < tga_header_size) return false;
            const hcpp::u8 id_length = input[0];
            const hcpp::u8 cmap_type = input[1];
            const hcpp::u8 type = input[2];
            const hcpp::u16 cmap_first = read_le16(input + 3);
            const hcpp::u16 cmap_length = read_le16(input + 5);
            const hcpp::u8 cmap_depth = input[7];
            const hcpp::u16 w = read_le16(input + 12);
            const hcpp::u16 h = read_le16(input + 14);
            const hcpp::u8 bpp = input[16];
            const hcpp::u8 descriptor = input[17];
            if (cmap_type > 1 || !tga_type_known(type)) return false;
            if (cmap_type == 0 && (cmap_first != 0 || cmap_length != 0)) return false;
            if (cmap_type == 1 &&
                (cmap_length == 0 ||
                 (cmap_depth != 15 && cmap_depth != 16 && cmap_depth != 24 &&
                  cmap_depth != 32)))
                return false;
            if (w == 0 || h == 0) return false;
            if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;
            if ((descriptor & 0xC0) != 0) return false;
            hcpp::size_t need = tga_header_size + id_length;
            if (cmap_type == 1)
                need += static_cast<hcpp::size_t>(cmap_length) * ((cmap_depth + 7) / 8);
            if (need > size) return false;
            if (type <= 3 &&
                need + static_cast<hcpp::size_t>(w) * h * ((bpp + 7) / 8) > size)
                return false;
            return true;
        }
    }  // namespace

    bool tga_probe(const hcpp::u8 *input, hcpp::size_t input_size) noexcept {
        if (input == nullptr || input_size < tga_header_size) return false;
        return tga_footer_match(input, input_size) ||
               tga_header_plausible(input, input_size);
    }

    CodecStatus tga_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (input == nullptr || frame == nullptr || input_size < tga_header_size)
            return CodecStatus::BadInput;
        const hcpp::u8 id_length = input[0];
        const hcpp::u8 cmap_type = input[1];
        const hcpp::u8 type = input[2];
        const hcpp::u32 w = read_le16(input + 12);
        const hcpp::u32 h = read_le16(input + 14);
        const hcpp::u8 bpp = input[16];
        const hcpp::u8 descriptor = input[17];
        if (cmap_type != 0 || (type != 2 && type != 10)) return CodecStatus::Unsupported;
        if (bpp != 24 && bpp != 32) return CodecStatus::Unsupported;
        if (!dims_valid(w, h)) return CodecStatus::BadInput;
        const hcpp::u32 pixel_bytes = bpp / 8;
        hcpp::size_t pos = tga_header_size + id_length;
        if (pos > input_size) return CodecStatus::BadInput;
        const CodecStatus prepared = frame_prepare(frame, w, h, arena);
        if (prepared != CodecStatus::Ok) return prepared;
        const bool flip_v = (descriptor & 0x20) == 0;
        const bool flip_h = (descriptor & 0x10) != 0;
        const hcpp::u32 total = w * h;
        hcpp::u32 produced = 0;
        while (produced < total) {
            hcpp::u32 count = 1;
            bool rle = false;
            if (type == 10) {
                if (pos >= input_size) return CodecStatus::BadInput;
                const hcpp::u8 packet = input[pos++];
                count = static_cast<hcpp::u32>(packet & 0x7F) + 1;
                rle = (packet & 0x80) != 0;
                if (count > total - produced) return CodecStatus::BadInput;
            } else {
                count = total - produced;
            }
            const hcpp::u32 literals = rle ? 1 : count;
            if (static_cast<hcpp::size_t>(literals) * pixel_bytes > input_size - pos)
                return CodecStatus::BadInput;
            for (hcpp::u32 i = 0; i < count; ++i) {
                const hcpp::u8 *px = input + pos + (rle ? 0 : i * pixel_bytes);
                const hcpp::u8 b = px[0];
                const hcpp::u8 g = px[1];
                const hcpp::u8 r = px[2];
                const hcpp::u8 a = pixel_bytes == 4 ? px[3] : 0xFF;
                const hcpp::u32 index = produced + i;
                hcpp::u32 x = index % w;
                hcpp::u32 y = index / w;
                if (flip_h) x = w - 1 - x;
                if (flip_v) y = h - 1 - y;
                put_pixel(*frame, y * w + x, r, g, b, a);
            }
            pos += static_cast<hcpp::size_t>(literals) * pixel_bytes;
            produced += count;
        }
        return CodecStatus::Ok;
    }

    CodecStatus tga_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &) noexcept {
        written = 0;
        const FrameBuf *frame = static_cast<const FrameBuf *>(input);
        if (!frame_source_valid(frame) || output == nullptr) return CodecStatus::BadInput;
        const hcpp::u32 w = frame->width;
        const hcpp::u32 h = frame->height;
        const hcpp::size_t pixel_bytes = static_cast<hcpp::size_t>(w) * h * 3;
        const hcpp::size_t total = tga_header_size + pixel_bytes + tga_footer_size;
        if (output_cap < total) return CodecStatus::NoSpace;
        for (hcpp::size_t i = 0; i < tga_header_size; ++i) output[i] = 0;
        output[2] = 2;
        write_le16(output + 12, static_cast<hcpp::u16>(w));
        write_le16(output + 14, static_cast<hcpp::u16>(h));
        output[16] = 24;
        output[17] = 0x20;
        hcpp::size_t pos = tga_header_size;
        const hcpp::size_t pixels = static_cast<hcpp::size_t>(w) * h;
        for (hcpp::size_t i = 0; i < pixels; ++i) {
            const hcpp::u32 x = static_cast<hcpp::u32>(i % w);
            const hcpp::u32 y = static_cast<hcpp::u32>(i / w);
            output[pos++] = source_component(*frame, x, y, 2);
            output[pos++] = source_component(*frame, x, y, 1);
            output[pos++] = source_component(*frame, x, y, 0);
        }
        for (hcpp::size_t i = 0; i < 8; ++i) output[pos++] = 0;
        for (hcpp::size_t i = 0; i < 17; ++i)
            output[pos++] = static_cast<hcpp::u8>(tga_signature[i]);
        output[pos++] = 0;
        written = pos;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
