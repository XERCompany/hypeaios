#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"
#include "kernel/display/scaler.h"

namespace hype::display {

    constexpr hcpp::u32 uhd4k_width = 3840;
    constexpr hcpp::u32 uhd4k_height = 2160;
    constexpr hcpp::u32 uhd8k_width = 7680;
    constexpr hcpp::u32 uhd8k_height = 4320;

    enum class UhdTier : hcpp::u8 {
        Downscale,
        IntegerFast,
        EdgePreserve,
        Native
    };

    constexpr bool uhd_integer_fast_path(hcpp::u32 src_w, hcpp::u32 src_h, hcpp::u32 dst_w,
                                         hcpp::u32 dst_h) noexcept {
        return src_w != 0 && src_h != 0 && dst_w % src_w == 0 && dst_h % src_h == 0 &&
               (dst_w / src_w) == (dst_h / src_h);
    }

    constexpr UhdTier uhd_tier_to(hcpp::u32 width, hcpp::u32 height, hcpp::u32 target_w,
                                  hcpp::u32 target_h) noexcept {
        if (width == target_w && height == target_h) {
            return UhdTier::Native;
        }
        if (width > target_w || height > target_h) {
            return UhdTier::Downscale;
        }
        if (uhd_integer_fast_path(width, height, target_w, target_h)) {
            return UhdTier::IntegerFast;
        }
        return UhdTier::EdgePreserve;
    }

    constexpr UhdTier uhd_tier(hcpp::u32 width, hcpp::u32 height) noexcept {
        return uhd_tier_to(width, height, uhd4k_width, uhd4k_height);
    }

    constexpr bool uhd_edge_preserve(hcpp::u32 width, hcpp::u32 height) noexcept {
        return uhd_tier_to(width, height, uhd4k_width, uhd4k_height) == UhdTier::EdgePreserve;
    }

    constexpr ScaleFilter uhd_filter(hcpp::u32 width, hcpp::u32 height) noexcept {
        return uhd_tier_to(width, height, uhd4k_width, uhd4k_height) == UhdTier::IntegerFast
                   ? ScaleFilter::Nearest
                   : ScaleFilter::Bilinear;
    }

    constexpr bool uhd_mode(hcpp::u32 width, hcpp::u32 height) noexcept {
        const DisplayMode* mode = find_mode(width, height);
        return mode != nullptr &&
               (mode->width == uhd4k_width || mode->width == uhd8k_width);
    }

}  // namespace hype::display
