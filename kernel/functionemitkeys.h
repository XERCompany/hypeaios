#pragma once
#include "hcpp/funkit.h"
#include "kernel/istatekeys.h"
namespace hype::input {
    struct KeyEvent {
        hcpp::u8 key;
        KeyState state;
        hcpp::u32 modifiers;
    };
    void initialize_key_emitter() noexcept;
    bool subscribe_keys(hcpp::funkit::FunctionSlot slot) noexcept;
    void emit_key_event(const KeyEvent& event) noexcept;
}  // namespace hype::input
