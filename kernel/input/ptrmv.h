#pragma once

#include "hcpp/types.hpp"
#include "kernel/input/curstat.h"
#include "kernel/input/msaux.h"

namespace hype::input {

    void ptr_set_bounds(hcpp::i32 width, hcpp::i32 height) noexcept;
    void ptr_home() noexcept;
    void ptr_set(hcpp::i32 x, hcpp::i32 y) noexcept;
    bool ptr_apply(const MsauxPacket &packet) noexcept;
    bool ptr_apply_motion(hcpp::i32 dx, hcpp::i32 dy, hcpp::u8 buttons) noexcept;
    hcpp::i32 ptr_x() noexcept;
    hcpp::i32 ptr_y() noexcept;
    hcpp::u8 ptr_buttons() noexcept;
    bool ptr_take_dirty() noexcept;
    void ptr_mark_dirty() noexcept;
    CurStat ptr_stat() noexcept;

}
