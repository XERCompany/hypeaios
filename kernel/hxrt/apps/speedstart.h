#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::hxrt {

    hcpp::u32 ss_count() noexcept;
    hype::ui::Rect ss_tile_rect(hcpp::u32 index) noexcept;
    hcpp::i32 ss_hit(hcpp::i32 x, hcpp::i32 y) noexcept;
    hcpp::u32 ss_launch(hcpp::u32 index) noexcept;
    bool ss_selftest() noexcept;

}
