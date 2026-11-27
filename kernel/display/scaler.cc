#include "kernel/display/scaler.h"

namespace hype::display {

    namespace {

        hcpp::u32 clip_lo(hcpp::u32 value, hcpp::u32 limit) noexcept {
            return value < limit ? value : limit;
        }

        hcpp::u32 clip_hi(hcpp::u32 begin, hcpp::u32 extent, hcpp::u32 limit) noexcept {
            const hcpp::u32 room = limit - begin;
            return begin + (extent < room ? extent : room);
        }

        bool clip_rect(const ScaleRect& rect, hcpp::u32 limit_w, hcpp::u32 limit_h,
                       ScaleRect& out) noexcept {
            out.x = clip_lo(rect.x, limit_w);
            out.y = clip_lo(rect.y, limit_h);
            const hcpp::u32 end_x = clip_hi(out.x, rect.w, limit_w);
            const hcpp::u32 end_y = clip_hi(out.y, rect.h, limit_h);
            out.w = end_x - out.x;
            out.h = end_y - out.y;
            return out.w != 0 && out.h != 0;
        }

    }  // namespace

    void scale_sample_index(hcpp::i64 fixed_q16, hcpp::u32 extent, hcpp::u32& tap0,
                            hcpp::u32& tap1, hcpp::u32& frac) noexcept {
        hcpp::i64 position = fixed_q16;
        if (position < 0) {
            position = 0;
        }
        hcpp::u32 index = static_cast<hcpp::u32>(position >> 16);
        hcpp::u32 fraction = static_cast<hcpp::u32>(position & 0xFFFF);
        if (extent == 0) {
            tap0 = 0;
            tap1 = 0;
            frac = 0;
            return;
        }
        if (index >= extent - 1) {
            index = extent - 1;
            fraction = 0;
        }
        tap0 = index;
        tap1 = index + 1 < extent ? index + 1 : index;
        frac = fraction;
    }

    bool scale_integer_ratio(hcpp::u32 src_w, hcpp::u32 src_h, hcpp::u32 dst_w,
                             hcpp::u32 dst_h) noexcept {
        if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
            return false;
        }
        const bool x_ratio = (dst_w % src_w == 0) || (src_w % dst_w == 0);
        const bool y_ratio = (dst_h % src_h == 0) || (src_h % dst_h == 0);
        return x_ratio && y_ratio;
    }

    hcpp::u32 bilinear_sample(hcpp::u32 c00, hcpp::u32 c10, hcpp::u32 c01, hcpp::u32 c11,
                              hcpp::u32 fx_q16, hcpp::u32 fy_q16) noexcept {
        hcpp::u32 result = 0;
        for (hcpp::u32 shift = 0; shift < 32; shift += 8) {
            const hcpp::i32 v00 = static_cast<hcpp::i32>((c00 >> shift) & 0xFF);
            const hcpp::i32 v10 = static_cast<hcpp::i32>((c10 >> shift) & 0xFF);
            const hcpp::i32 v01 = static_cast<hcpp::i32>((c01 >> shift) & 0xFF);
            const hcpp::i32 v11 = static_cast<hcpp::i32>((c11 >> shift) & 0xFF);
            const hcpp::i32 top =
                v00 + static_cast<hcpp::i32>(((v10 - v00) * static_cast<hcpp::i32>(fx_q16)) >> 16);
            const hcpp::i32 bottom =
                v01 + static_cast<hcpp::i32>(((v11 - v01) * static_cast<hcpp::i32>(fx_q16)) >> 16);
            hcpp::i32 value =
                top + static_cast<hcpp::i32>(((bottom - top) * static_cast<hcpp::i32>(fy_q16)) >> 16);
            if (value < 0) {
                value = 0;
            }
            if (value > 255) {
                value = 255;
            }
            result |= static_cast<hcpp::u32>(value) << shift;
        }
        return result;
    }

    bool scale_nearest(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                       hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                       hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                       const ScaleRect& dst_rect) noexcept {
        if (src == nullptr || dst == nullptr || src_stride < src_w || dst_stride < dst_w) {
            return false;
        }
        ScaleRect sclip{};
        ScaleRect dclip{};
        if (!clip_rect(src_rect, src_w, src_h, sclip)) {
            return false;
        }
        if (!clip_rect(dst_rect, dst_w, dst_h, dclip)) {
            return true;
        }
        for (hcpp::u32 y = 0; y < dclip.h; ++y) {
            const hcpp::u32 sy = sclip.y + (y * sclip.h) / dclip.h;
            const hcpp::u32* src_row = src + static_cast<hcpp::size_t>(sy) * src_stride;
            hcpp::u32* dst_row = dst + static_cast<hcpp::size_t>(dclip.y + y) * dst_stride;
            for (hcpp::u32 x = 0; x < dclip.w; ++x) {
                const hcpp::u32 sx = sclip.x + (x * sclip.w) / dclip.w;
                dst_row[dclip.x + x] = src_row[sx];
            }
        }
        return true;
    }

    bool scale_bilinear(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                        hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                        hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                        const ScaleRect& dst_rect) noexcept {
        if (src == nullptr || dst == nullptr || src_stride < src_w || dst_stride < dst_w) {
            return false;
        }
        ScaleRect sclip{};
        ScaleRect dclip{};
        if (!clip_rect(src_rect, src_w, src_h, sclip)) {
            return false;
        }
        if (!clip_rect(dst_rect, dst_w, dst_h, dclip)) {
            return true;
        }
        const hcpp::i64 step_x = (static_cast<hcpp::i64>(sclip.w) << 16) / dclip.w;
        const hcpp::i64 step_y = (static_cast<hcpp::i64>(sclip.h) << 16) / dclip.h;
        for (hcpp::u32 y = 0; y < dclip.h; ++y) {
            const hcpp::i64 fy = (step_y >> 1) - 0x8000 + static_cast<hcpp::i64>(y) * step_y;
            hcpp::u32 tap_y0 = 0;
            hcpp::u32 tap_y1 = 0;
            hcpp::u32 frac_y = 0;
            scale_sample_index(fy, sclip.h, tap_y0, tap_y1, frac_y);
            const hcpp::u32* row0 =
                src + static_cast<hcpp::size_t>(sclip.y + tap_y0) * src_stride + sclip.x;
            const hcpp::u32* row1 =
                src + static_cast<hcpp::size_t>(sclip.y + tap_y1) * src_stride + sclip.x;
            hcpp::u32* dst_row = dst + static_cast<hcpp::size_t>(dclip.y + y) * dst_stride;
            for (hcpp::u32 x = 0; x < dclip.w; ++x) {
                const hcpp::i64 fx = (step_x >> 1) - 0x8000 + static_cast<hcpp::i64>(x) * step_x;
                hcpp::u32 tap_x0 = 0;
                hcpp::u32 tap_x1 = 0;
                hcpp::u32 frac_x = 0;
                scale_sample_index(fx, sclip.w, tap_x0, tap_x1, frac_x);
                dst_row[dclip.x + x] =
                    bilinear_sample(row0[tap_x0], row0[tap_x1], row1[tap_x0], row1[tap_x1],
                                    frac_x, frac_y);
            }
        }
        return true;
    }

    bool scale_auto(const hcpp::u32* src, hcpp::u32 src_w, hcpp::u32 src_h,
                    hcpp::u32 src_stride, const ScaleRect& src_rect, hcpp::u32* dst,
                    hcpp::u32 dst_w, hcpp::u32 dst_h, hcpp::u32 dst_stride,
                    const ScaleRect& dst_rect) noexcept {
        if (scale_integer_ratio(src_rect.w, src_rect.h, dst_rect.w, dst_rect.h)) {
            return scale_nearest(src, src_w, src_h, src_stride, src_rect, dst, dst_w, dst_h,
                                 dst_stride, dst_rect);
        }
        return scale_bilinear(src, src_w, src_h, src_stride, src_rect, dst, dst_w, dst_h,
                              dst_stride, dst_rect);
    }

}  // namespace hype::display
