#include "kernel/display/fmtcvt.h"

namespace hype::display {

    hcpp::u32 fmtcvt_canonical(const hcpp::graphics::Framebuffer& src, hcpp::u32 x,
                               hcpp::u32 y) noexcept {
        hcpp::u8 red = 0;
        hcpp::u8 green = 0;
        hcpp::u8 blue = 0;
        src.decode_rgb(src.get_pixel(x, y), red, green, blue);
        return 0xFF000000u | (static_cast<hcpp::u32>(red) << 16) |
               (static_cast<hcpp::u32>(green) << 8) | blue;
    }

    void fmtcvt_row(const hcpp::graphics::Framebuffer& src, hcpp::u32 y, hcpp::u32* row,
                    hcpp::u32 count) noexcept {
        for (hcpp::u32 x = 0; x < count; ++x) {
            row[x] = fmtcvt_canonical(src, x, y);
        }
    }

    codec::CodecStatus fmtcvt_frame(const hcpp::graphics::Framebuffer& src,
                                    codec::FrameBuf& frame, codec::Arena& arena,
                                    video::PixelFormat format) noexcept {
        if (!src.valid() ||
            (format != video::PixelFormat::RGB24 && format != video::PixelFormat::RGB32)) {
            return codec::CodecStatus::BadInput;
        }
        const hcpp::u32 bpp = format == video::PixelFormat::RGB32 ? 4 : 3;
        const hcpp::u32 pitch = src.width() * bpp;
        const hcpp::size_t needed = static_cast<hcpp::size_t>(pitch) * src.height();
        if (frame.data == nullptr) {
            void* block = codec::arena_alloc(arena, needed, 16);
            if (block == nullptr) {
                return codec::CodecStatus::NoSpace;
            }
            frame.data = static_cast<hcpp::u8*>(block);
            frame.capacity = needed;
        } else if (frame.capacity < needed) {
            return codec::CodecStatus::NoSpace;
        }
        frame.width = src.width();
        frame.height = src.height();
        frame.pitch = pitch;
        frame.format = format;
        frame.size = needed;
        for (hcpp::u32 y = 0; y < src.height(); ++y) {
            hcpp::u8* row = frame.data + static_cast<hcpp::size_t>(y) * pitch;
            for (hcpp::u32 x = 0; x < src.width(); ++x) {
                hcpp::u8 red = 0;
                hcpp::u8 green = 0;
                hcpp::u8 blue = 0;
                src.decode_rgb(src.get_pixel(x, y), red, green, blue);
                hcpp::u8* px = row + static_cast<hcpp::size_t>(x) * bpp;
                px[0] = red;
                px[1] = green;
                px[2] = blue;
                if (bpp == 4) {
                    px[3] = 0xFF;
                }
            }
        }
        return codec::CodecStatus::Ok;
    }

}  // namespace hype::display
