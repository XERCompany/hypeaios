#pragma once

#include "hcpp/types.hpp"

namespace hype::input {

    struct MsauxPacket {
        hcpp::u8 buttons;
        hcpp::i32 dx;
        hcpp::i32 dy;
    };

    bool msaux_init() noexcept;
    bool msaux_ready() noexcept;
    bool msaux_parse(hcpp::u8 b0, hcpp::u8 b1, hcpp::u8 b2, MsauxPacket &out) noexcept;
    hcpp::u32 msaux_poll(MsauxPacket *out, hcpp::u32 max) noexcept;
    hcpp::u64 msaux_byte_count() noexcept;
    hcpp::u64 msaux_resync_count() noexcept;
    hcpp::u8 msaux_fail_step() noexcept;

}
