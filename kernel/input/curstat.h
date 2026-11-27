#pragma once

#include "hcpp/types.hpp"

namespace hype::input {

    constexpr hcpp::u8 ptr_button_left = 1;
    constexpr hcpp::u8 ptr_button_right = 2;
    constexpr hcpp::u8 ptr_button_middle = 4;

    struct CurStat {
        hcpp::i32 x = 0;
        hcpp::i32 y = 0;
        hcpp::u8 buttons = 0;
        bool ready = false;
        hcpp::u64 packets = 0;
        hcpp::u64 resyncs = 0;
        hcpp::i32 wheel = 0;
    };

}
