#pragma once

#include "hcpp/types.hpp"

namespace hcpp::graphics {

class Framebuffer {
public:
    Framebuffer() noexcept;
    Framebuffer(
        volatile u8* base,
        u32 width,
        u32 height,
        u32 pitch,
        u8 bits_per_pixel,
        u8 red_mask_size,
        u8 red_position,
        u8 green_mask_size,
        u8 green_position,
        u8 blue_mask_size,
        u8 blue_position) noexcept;

    bool valid() const noexcept;
    u32 width() const noexcept;
    u32 height() const noexcept;
    u32 pitch() const noexcept;

    u32 encode_rgb(u8 red, u8 green, u8 blue) const noexcept;
    void put_pixel(u32 x, u32 y, u32 color) const noexcept;
    void fill_rect(u32 x, u32 y, u32 width, u32 height, u32 color) const noexcept;
    void clear(u32 color) const noexcept;
    void draw_test_pattern() const noexcept;

private:
    static u32 scale_channel(u8 value, u8 bits) noexcept;
    u32 bytes_per_pixel() const noexcept;

    volatile u8* base_ = nullptr;
    u32 width_ = 0;
    u32 height_ = 0;
    u32 pitch_ = 0;
    u8 bits_per_pixel_ = 0;
    u8 red_mask_size_ = 0;
    u8 red_position_ = 0;
    u8 green_mask_size_ = 0;
    u8 green_position_ = 0;
    u8 blue_mask_size_ = 0;
    u8 blue_position_ = 0;
};

} // namespace hcpp::graphics
