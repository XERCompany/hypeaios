#pragma once

#include "hcpp/types.hpp"
#include "kernel/istatekeys.h"

namespace hype::input {

    // Synthetic key code for PrintScreen (issue #131). Set-1 PrtSc is the
    // multi-byte sequence "E0 2A E0 37" (make) / "E0 B7 E0 AA" (break);
    // the decoder folds it into this single code so key-event consumers
    // see a normal make/break pair. Value is outside the 0x00-0x7F
    // scancode space and under max_key_code so key_event_valid() passes.
    constexpr hcpp::u16 key_prtsc = 0xF0;

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
