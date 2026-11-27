#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    enum class DpiScale : hcpp::u32 {
        X100 = 100,
        X125 = 125,
        X150 = 150,
        X200 = 200,
    };

    constexpr hcpp::u32 dpi_table[] = {
        96, 120, 144, 168, 192, 216, 240, 264, 288, 312, 336, 360, 384, 408, 432, 456, 480,
    };

    constexpr hcpp::u32 dpi_table_count() noexcept {
        return static_cast<hcpp::u32>(sizeof(dpi_table) / sizeof(dpi_table[0]));
    }

    constexpr hcpp::u32 dpi_base = 96;

    constexpr hcpp::u32 isqrt(hcpp::u64 value) noexcept {
        hcpp::u64 root = 0;
        hcpp::u64 bit = 1ull << 62;
        while (bit > value) {
            bit >>= 2;
        }
        while (bit != 0) {
            if (value >= root + bit) {
                value -= root + bit;
                root = (root >> 1) + bit;
            } else {
                root >>= 1;
            }
            bit >>= 2;
        }
        return static_cast<hcpp::u32>(root);
    }

    constexpr hcpp::u32 dpi_exact(hcpp::u32 width_px, hcpp::u32 height_px,
                                  hcpp::u32 phys_mm) noexcept {
        if (phys_mm == 0 || width_px == 0 || height_px == 0) {
            return dpi_base;
        }
        const hcpp::u64 diag_sq = static_cast<hcpp::u64>(width_px) * width_px +
                                  static_cast<hcpp::u64>(height_px) * height_px;
        const hcpp::u64 diag_px = isqrt(diag_sq);
        return static_cast<hcpp::u32>((diag_px * 254 + phys_mm * 5) / (phys_mm * 10));
    }

    constexpr hcpp::u32 dpi_quantize(hcpp::u32 dpi) noexcept {
        hcpp::u32 best = dpi_table[0];
        hcpp::u32 best_dist = dpi > best ? dpi - best : best - dpi;
        for (hcpp::u32 i = 1; i < dpi_table_count(); ++i) {
            const hcpp::u32 entry = dpi_table[i];
            const hcpp::u32 dist = dpi > entry ? dpi - entry : entry - dpi;
            if (dist < best_dist) {
                best = entry;
                best_dist = dist;
            }
        }
        return best;
    }

    constexpr hcpp::u32 dpi_for(hcpp::u32 width_px, hcpp::u32 height_px,
                                hcpp::u32 phys_mm) noexcept {
        return dpi_quantize(dpi_exact(width_px, height_px, phys_mm));
    }

    constexpr DpiScale scale_factor_for(hcpp::u32 dpi) noexcept {
        if (dpi < 120) {
            return DpiScale::X100;
        }
        if (dpi < 144) {
            return DpiScale::X125;
        }
        if (dpi < 192) {
            return DpiScale::X150;
        }
        return DpiScale::X200;
    }

    constexpr hcpp::u32 scale_percent_for(hcpp::u32 dpi) noexcept {
        return static_cast<hcpp::u32>(scale_factor_for(dpi));
    }

    constexpr hcpp::u32 scale_q16_for(hcpp::u32 dpi) noexcept {
        return scale_percent_for(dpi) * 65536 / 100;
    }

}  // namespace hype::display
