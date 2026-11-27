#pragma once

#include "hcpp/types.hpp"

namespace hype::input {

    enum class MsauxMode : hcpp::u8 {
        Probing = 0,
        Irq = 1,
        Poll = 2,
    };

    struct MsauxPacket {
        hcpp::u8 buttons;
        hcpp::i32 dx;
        hcpp::i32 dy;
        hcpp::i32 wheel;
    };

    bool msaux_init() noexcept;
    bool msaux_ready() noexcept;
    bool msaux_parse(hcpp::u8 b0, hcpp::u8 b1, hcpp::u8 b2, MsauxPacket &out) noexcept;
    hcpp::u32 msaux_poll(MsauxPacket *out, hcpp::u32 max) noexcept;
    void msaux_irq() noexcept;
    MsauxMode msaux_mode() noexcept;
    hcpp::u8 msaux_mouse_id() noexcept;
    hcpp::u8 msaux_packet_size() noexcept;
    hcpp::u64 msaux_byte_count() noexcept;
    hcpp::u64 msaux_resync_count() noexcept;
    hcpp::u8 msaux_fail_step() noexcept;

}
