#pragma once

#include "kernel/istatekeys.h"

namespace hype::input {

    enum class InputEventType : hcpp::u16 { Sync = 0, Key = 1, Relative = 2, Absolute = 3 };

    struct InputEvent {
        InputEventType type;
        hcpp::u16 code;
        hcpp::u32 value;
        hcpp::u64 timestamp;
    };

    using InputEventHandler = void (*)(void *context, const InputEvent &event) noexcept;
    using KeyEvent = InputEvent;

    void initialize_key_emitter() noexcept;
    bool subscribe_keys(InputEventHandler handler, void *context) noexcept;
    void emit_input_event(const InputEvent &event) noexcept;
    void emit_key_event(hcpp::u16 key, KeyState state, hcpp::u64 timestamp = 0) noexcept;
    hcpp::size_t key_subscriber_count() noexcept;
    bool key_event_valid(const KeyEvent &event) noexcept;

}  // namespace hype::input
