#pragma once

#include "hcpp/types.hpp"

namespace hype::audio::hda {

    constexpr hcpp::u16 reg_gcap = 0x00;
    constexpr hcpp::u16 reg_vmin = 0x02;
    constexpr hcpp::u16 reg_vmaj = 0x03;
    constexpr hcpp::u16 reg_gctl = 0x08;
    constexpr hcpp::u16 reg_wakeen = 0x0C;
    constexpr hcpp::u16 reg_statests = 0x0E;
    constexpr hcpp::u16 reg_corblbase = 0x40;
    constexpr hcpp::u16 reg_corbubase = 0x44;
    constexpr hcpp::u16 reg_corbwp = 0x48;
    constexpr hcpp::u16 reg_corbrp = 0x4A;
    constexpr hcpp::u16 reg_corbctl = 0x4C;
    constexpr hcpp::u16 reg_rirblbase = 0x50;
    constexpr hcpp::u16 reg_rirbubase = 0x54;
    constexpr hcpp::u16 reg_rirbwp = 0x58;
    constexpr hcpp::u16 reg_rintcnt = 0x5A;
    constexpr hcpp::u16 reg_rirbctl = 0x5C;
    constexpr hcpp::u16 reg_ics = 0x60;

    constexpr hcpp::u32 gctl_reset = 0x1;
    constexpr hcpp::u32 corbctl_run = 0x2;
    constexpr hcpp::u32 rirbctl_run = 0x2;
    constexpr hcpp::u32 verb_get_parameter = 0xF00;
    constexpr hcpp::u32 param_vendor_id = 0x00;
    constexpr hcpp::u8 function_group_audio = 0x01;

    struct IzauController {
        hcpp::uintptr_t mmio;
        hcpp::u8 card_index;
        hcpp::u8 codec_mask;
        bool reset_done;
    };

    bool izau_bind(hcpp::size_t card_index, IzauController &controller) noexcept;
    bool izau_reset(IzauController &controller) noexcept;
    bool izau_codec_verb(IzauController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept;

}  // namespace hype::audio::hda
