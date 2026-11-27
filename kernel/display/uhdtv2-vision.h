#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    enum class UhdTransfer : hcpp::u8 {
        Bt709 = 1,
        Bt2020 = 14,
        Pq = 16,
        Hlg = 18,
    };

    enum class UhdMatrix : hcpp::u8 {
        Rgb = 0,
        Bt709 = 1,
        Bt2020Ncl = 9,
        Bt2020Cl = 10,
    };

    struct UhdPrimaries {
        hcpp::u16 red_x;
        hcpp::u16 red_y;
        hcpp::u16 green_x;
        hcpp::u16 green_y;
        hcpp::u16 blue_x;
        hcpp::u16 blue_y;
        hcpp::u16 white_x;
        hcpp::u16 white_y;
    };

    constexpr hcpp::u32 uhd_chroma_units = 50000;

    constexpr UhdPrimaries bt2020_primaries{
        35400, 14600,
        8500, 39850,
        6550, 2300,
        15635, 16450
    };

    constexpr UhdPrimaries bt709_primaries{
        32000, 16500,
        15000, 30000,
        7500, 3000,
        15635, 16450
    };

    constexpr bool uhd_primaries_equal(const UhdPrimaries& a, const UhdPrimaries& b) noexcept {
        return a.red_x == b.red_x && a.red_y == b.red_y && a.green_x == b.green_x &&
               a.green_y == b.green_y && a.blue_x == b.blue_x && a.blue_y == b.blue_y &&
               a.white_x == b.white_x && a.white_y == b.white_y;
    }

    struct UhdHdr10Meta {
        UhdPrimaries mastering;
        hcpp::u32 max_luminance_e4;
        hcpp::u32 min_luminance_e4;
        hcpp::u16 max_cll;
        hcpp::u16 max_fall;
    };

    constexpr hcpp::u32 uhd_hdr10_block_size = 28;

    constexpr UhdHdr10Meta uhd_hdr10_default() noexcept {
        return UhdHdr10Meta{bt2020_primaries, 10000000, 500, 1000, 400};
    }

    inline void uhd_put_be16(hcpp::u8* out, hcpp::u16 value) noexcept {
        out[0] = static_cast<hcpp::u8>(value >> 8);
        out[1] = static_cast<hcpp::u8>(value & 0xFF);
    }

    inline void uhd_put_be32(hcpp::u8* out, hcpp::u32 value) noexcept {
        out[0] = static_cast<hcpp::u8>(value >> 24);
        out[1] = static_cast<hcpp::u8>((value >> 16) & 0xFF);
        out[2] = static_cast<hcpp::u8>((value >> 8) & 0xFF);
        out[3] = static_cast<hcpp::u8>(value & 0xFF);
    }

    inline hcpp::u16 uhd_get_be16(const hcpp::u8* data) noexcept {
        return static_cast<hcpp::u16>((static_cast<hcpp::u16>(data[0]) << 8) | data[1]);
    }

    inline hcpp::u32 uhd_get_be32(const hcpp::u8* data) noexcept {
        return (static_cast<hcpp::u32>(data[0]) << 24) |
               (static_cast<hcpp::u32>(data[1]) << 16) |
               (static_cast<hcpp::u32>(data[2]) << 8) | static_cast<hcpp::u32>(data[3]);
    }

    inline bool uhd_primaries_pack(const UhdPrimaries& primaries, hcpp::u8* out,
                                   hcpp::size_t size) noexcept {
        if (out == nullptr || size < 16) {
            return false;
        }
        uhd_put_be16(out + 0, primaries.green_x);
        uhd_put_be16(out + 2, primaries.green_y);
        uhd_put_be16(out + 4, primaries.blue_x);
        uhd_put_be16(out + 6, primaries.blue_y);
        uhd_put_be16(out + 8, primaries.red_x);
        uhd_put_be16(out + 10, primaries.red_y);
        uhd_put_be16(out + 12, primaries.white_x);
        uhd_put_be16(out + 14, primaries.white_y);
        return true;
    }

    inline bool uhd_primaries_parse(const hcpp::u8* data, hcpp::size_t size,
                                    UhdPrimaries& primaries) noexcept {
        if (data == nullptr || size < 16) {
            return false;
        }
        primaries.green_x = uhd_get_be16(data + 0);
        primaries.green_y = uhd_get_be16(data + 2);
        primaries.blue_x = uhd_get_be16(data + 4);
        primaries.blue_y = uhd_get_be16(data + 6);
        primaries.red_x = uhd_get_be16(data + 8);
        primaries.red_y = uhd_get_be16(data + 10);
        primaries.white_x = uhd_get_be16(data + 12);
        primaries.white_y = uhd_get_be16(data + 14);
        return true;
    }

    inline bool uhd_hdr10_pack(const UhdHdr10Meta& meta, hcpp::u8* out,
                               hcpp::size_t size) noexcept {
        if (out == nullptr || size < uhd_hdr10_block_size) {
            return false;
        }
        uhd_primaries_pack(meta.mastering, out, size);
        uhd_put_be32(out + 16, meta.max_luminance_e4);
        uhd_put_be32(out + 20, meta.min_luminance_e4);
        uhd_put_be16(out + 24, meta.max_cll);
        uhd_put_be16(out + 26, meta.max_fall);
        return true;
    }

    inline bool uhd_hdr10_parse(const hcpp::u8* data, hcpp::size_t size,
                                UhdHdr10Meta& meta) noexcept {
        if (data == nullptr || size < uhd_hdr10_block_size) {
            return false;
        }
        uhd_primaries_parse(data, size, meta.mastering);
        meta.max_luminance_e4 = uhd_get_be32(data + 16);
        meta.min_luminance_e4 = uhd_get_be32(data + 20);
        meta.max_cll = uhd_get_be16(data + 24);
        meta.max_fall = uhd_get_be16(data + 26);
        return true;
    }

    constexpr bool uhd_hdr10_equal(const UhdHdr10Meta& a, const UhdHdr10Meta& b) noexcept {
        return uhd_primaries_equal(a.mastering, b.mastering) &&
               a.max_luminance_e4 == b.max_luminance_e4 &&
               a.min_luminance_e4 == b.min_luminance_e4 && a.max_cll == b.max_cll &&
               a.max_fall == b.max_fall;
    }

}  // namespace hype::display
