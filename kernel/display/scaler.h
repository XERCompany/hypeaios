#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    enum class ScaleFilter : hcpp::u8 {
        Nearest,
        Bilinear,
        Auto
    };

    struct ScaleRect {
        hcpp::u32 x;
        hcpp::u32 y;
        hcpp::u32 w;
        hcpp::u32 h;
    };

    bool scale_integer_ratio(hcpp::u32 src_w, hcpp::u32 src_h, hcpp::u32 dst_w,
                             hcpp::u32 dst_h) noexcept;
    void scale_sample_index(hcpp::i64 fixed_q16, hcpp::u32 extent, hcpp::u32& tap0,
                            hcpp::u32& tap1, hcpp::u32& frac) noexcept;
    hcpp::u32 bilinear_sample(hcpp::u32 c00, hcpp::u32 c10, hcpp::u32 c01, hcpp::u32 c11,
                              hcpp::u32 fx_q16, hcpp::u32 fy_q16) noexcept;
    bool scale_nearest(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                       hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                       hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                       const ScaleRect& dst_rect) noexcept;
    bool scale_bilinear(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                        hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                        hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                        const ScaleRect& dst_rect) noexcept;
    bool scale_auto(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                    hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                    hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                    const ScaleRect& dst_rect) noexcept;

}  // namespace hype::display
