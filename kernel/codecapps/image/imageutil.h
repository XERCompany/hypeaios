#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/codec.h"

namespace hype::codec::image {

    constexpr hcpp::u32 image_max_dimension = 16384;

    inline hcpp::u16 read_le16(const hcpp::u8 *p) noexcept {
        return static_cast<hcpp::u16>(p[0] | (static_cast<hcpp::u16>(p[1]) << 8));
    }

    inline hcpp::u32 read_le32(const hcpp::u8 *p) noexcept {
        return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
               (static_cast<hcpp::u32>(p[2]) << 16) | (static_cast<hcpp::u32>(p[3]) << 24);
    }

    inline hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
        return static_cast<hcpp::u32>(p[3]) | (static_cast<hcpp::u32>(p[2]) << 8) |
               (static_cast<hcpp::u32>(p[1]) << 16) | (static_cast<hcpp::u32>(p[0]) << 24);
    }

    inline void write_le16(hcpp::u8 *p, hcpp::u16 v) noexcept {
        p[0] = static_cast<hcpp::u8>(v & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
    }

    inline void write_le32(hcpp::u8 *p, hcpp::u32 v) noexcept {
        p[0] = static_cast<hcpp::u8>(v & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
        p[2] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
        p[3] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
    }

    inline void write_be32(hcpp::u8 *p, hcpp::u32 v) noexcept {
        p[0] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
        p[2] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
        p[3] = static_cast<hcpp::u8>(v & 0xFF);
    }

    inline bool dims_valid(hcpp::u32 width, hcpp::u32 height) noexcept {
        return width != 0 && height != 0 && width <= image_max_dimension &&
               height <= image_max_dimension;
    }

    inline CodecStatus frame_prepare(FrameBuf *frame, hcpp::u32 width, hcpp::u32 height,
                                     Arena &arena) noexcept {
        if (frame == nullptr || !dims_valid(width, height)) return CodecStatus::BadInput;
        const hcpp::u32 pitch = width * 4;
        const hcpp::size_t needed = static_cast<hcpp::size_t>(pitch) * height;
        if (frame->data == nullptr) {
            void *block = arena_alloc(arena, needed, 16);
            if (block == nullptr) return CodecStatus::NoSpace;
            frame->data = static_cast<hcpp::u8 *>(block);
            frame->capacity = needed;
        } else if (frame->capacity < needed) {
            return CodecStatus::NoSpace;
        }
        frame->width = width;
        frame->height = height;
        frame->pitch = pitch;
        frame->format = video::PixelFormat::RGB32;
        frame->size = needed;
        return CodecStatus::Ok;
    }

    inline void put_pixel(FrameBuf &frame, hcpp::u32 index, hcpp::u8 r, hcpp::u8 g, hcpp::u8 b,
                          hcpp::u8 a) noexcept {
        hcpp::u8 *p = frame.data + static_cast<hcpp::size_t>(index) * 4;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }

    inline hcpp::u32 source_pitch(const FrameBuf &frame) noexcept {
        const hcpp::u32 bpp = frame.format == video::PixelFormat::RGB32 ? 4 : 3;
        const hcpp::u32 packed = frame.width * bpp;
        return frame.pitch >= packed ? frame.pitch : packed;
    }

    inline bool frame_source_valid(const FrameBuf *frame) noexcept {
        if (frame == nullptr || frame->data == nullptr || !dims_valid(frame->width, frame->height))
            return false;
        if (frame->format != video::PixelFormat::RGB32 &&
            frame->format != video::PixelFormat::RGB24)
            return false;
        const hcpp::u32 pitch = source_pitch(*frame);
        const hcpp::u32 bpp = frame->format == video::PixelFormat::RGB32 ? 4 : 3;
        const hcpp::size_t last =
            static_cast<hcpp::size_t>(pitch) * (frame->height - 1) +
            static_cast<hcpp::size_t>(frame->width) * bpp;
        return frame->size >= last;
    }

    inline hcpp::u8 source_component(const FrameBuf &frame, hcpp::u32 x, hcpp::u32 y,
                                     hcpp::u32 component) noexcept {
        const hcpp::u32 bpp = frame.format == video::PixelFormat::RGB32 ? 4 : 3;
        const hcpp::u8 *p = frame.data + static_cast<hcpp::size_t>(y) * source_pitch(frame) +
                          static_cast<hcpp::size_t>(x) * bpp;
        return component == 3 ? (bpp == 4 ? p[3] : 0xFF) : p[component];
    }

}  // namespace hype::codec::image
