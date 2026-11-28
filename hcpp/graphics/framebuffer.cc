#include "hcpp/graphics/framebuffer.hpp"

namespace hcpp::graphics {

    Framebuffer::Framebuffer() noexcept = default;

    Framebuffer::Framebuffer(volatile u8* base, u32 width, u32 height, u32 pitch, u8 bits_per_pixel, u8 red_mask_size,
                             u8 red_position, u8 green_mask_size, u8 green_position, u8 blue_mask_size,
                             u8 blue_position) noexcept
        : base_(base),
          width_(width),
          height_(height),
          pitch_(pitch),
          bits_per_pixel_(bits_per_pixel),
          red_mask_size_(red_mask_size),
          red_position_(red_position),
          green_mask_size_(green_mask_size),
          green_position_(green_position),
          blue_mask_size_(blue_mask_size),
          blue_position_(blue_position) {}

    bool Framebuffer::valid() const noexcept {
        return base_ != nullptr && width_ != 0 && height_ != 0 && pitch_ != 0 &&
               (bits_per_pixel_ == 24 || bits_per_pixel_ == 32);
    }

    volatile u8* Framebuffer::base() const noexcept { return base_; }

    u32 Framebuffer::width() const noexcept { return width_; }

    u32 Framebuffer::height() const noexcept { return height_; }

    u32 Framebuffer::pitch() const noexcept { return pitch_; }

    u8 Framebuffer::bits_per_pixel() const noexcept { return bits_per_pixel_; }

    Framebuffer Framebuffer::with_base(volatile u8* base) const noexcept {
        return Framebuffer(base, width_, height_,
                           width_ * bytes_per_pixel(), bits_per_pixel_,
                           red_mask_size_, red_position_, green_mask_size_,
                           green_position_, blue_mask_size_, blue_position_);
    }

    u32 Framebuffer::encode_rgb(u8 red, u8 green, u8 blue) const noexcept {
        return (scale_channel(red, red_mask_size_) << red_position_) |
               (scale_channel(green, green_mask_size_) << green_position_) |
               (scale_channel(blue, blue_mask_size_) << blue_position_);
    }

    void Framebuffer::decode_rgb(u32 color, u8& red, u8& green, u8& blue) const noexcept {
        red = unscale_channel(color, red_position_, red_mask_size_);
        green = unscale_channel(color, green_position_, green_mask_size_);
        blue = unscale_channel(color, blue_position_, blue_mask_size_);
    }

    u32 Framebuffer::get_pixel(u32 x, u32 y) const noexcept {
        if (!valid() || x >= width_ || y >= height_) {
            return 0;
        }

        volatile u8* row = base_ + static_cast<size_t>(y) * pitch_;
        volatile u8* pixel = row + static_cast<size_t>(x) * bytes_per_pixel();
        if (bits_per_pixel_ == 32) {
            return *reinterpret_cast<volatile const u32*>(pixel);
        }
        return static_cast<u32>(pixel[0]) | (static_cast<u32>(pixel[1]) << 8) |
               (static_cast<u32>(pixel[2]) << 16);
    }

    void Framebuffer::put_pixel(u32 x, u32 y, u32 color) const noexcept {
        if (!valid() || x >= width_ || y >= height_) {
            return;
        }

        volatile u8* row = base_ + static_cast<size_t>(y) * pitch_;
        volatile u8* pixel = row + static_cast<size_t>(x) * bytes_per_pixel();
        if (bits_per_pixel_ == 32) {
            *reinterpret_cast<volatile u32*>(pixel) = color;
        } else {
            pixel[0] = static_cast<u8>(color);
            pixel[1] = static_cast<u8>(color >> 8);
            pixel[2] = static_cast<u8>(color >> 16);
        }
    }

    void Framebuffer::fill_rect(u32 x, u32 y, u32 width, u32 height, u32 color) const noexcept {
        if (!valid() || x >= width_ || y >= height_) {
            return;
        }
        if (x + width > width_) {
            width = width_ - x;
        }
        if (y + height > height_) {
            height = height_ - y;
        }

        for (u32 row = 0; row < height; ++row) {
            for (u32 column = 0; column < width; ++column) {
                put_pixel(x + column, y + row, color);
            }
        }
    }

    void Framebuffer::clear(u32 color) const noexcept { fill_rect(0, 0, width_, height_, color); }

    void Framebuffer::draw_test_pattern() const noexcept {
        if (!valid()) {
            return;
        }

        for (u32 y = 0; y < height_; ++y) {
            for (u32 x = 0; x < width_; ++x) {
                const u8 red = static_cast<u8>((x * 255) / width_);
                const u8 green = static_cast<u8>((y * 255) / height_);
                const u8 blue = static_cast<u8>(((x + y) * 255) / (width_ + height_));
                put_pixel(x, y, encode_rgb(red, green, blue));
            }
        }
    }

    u32 Framebuffer::scale_channel(u8 value, u8 bits) noexcept {
        if (bits == 0) {
            return 0;
        }
        if (bits == 8) {
            return value;
        }
        const u32 maximum = (1U << bits) - 1U;
        return (static_cast<u32>(value) * maximum) / 255U;
    }

    u8 Framebuffer::unscale_channel(u32 color, u8 position, u8 bits) noexcept {
        if (bits == 0) {
            return 0;
        }
        const u32 maximum = (1U << bits) - 1U;
        const u32 raw = (color >> position) & maximum;
        if (bits == 8) {
            return static_cast<u8>(raw);
        }
        return static_cast<u8>((raw * 255U) / maximum);
    }

    u32 Framebuffer::bytes_per_pixel() const noexcept { return bits_per_pixel_ / 8; }

}  // namespace hcpp::graphics
