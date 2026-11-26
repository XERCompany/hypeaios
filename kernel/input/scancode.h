#pragma once

#include "hcpp/types.hpp"
#include "kernel/istatekeys.h"

namespace hype::input {

    struct DecodedKey {
        hcpp::u16 key_code;
        char ascii;
        bool pressed;
        bool extended;
        bool valid;
    };

    void scancode_initialize() noexcept;
    DecodedKey scancode_decode(hcpp::u8 scancode) noexcept;
    bool scancode_key_pressed(hcpp::u16 key_code) noexcept;
    bool scancode_shift_active() noexcept;
    bool scancode_caps_lock_active() noexcept;
    hcpp::u64 scancode_event_count() noexcept;
    char scancode_last_ascii() noexcept;
    const char* scancode_key_name(hcpp::u16 key_code) noexcept;

}  // namespace hype::input
