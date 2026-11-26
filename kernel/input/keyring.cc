#include "kernel/input/keyring.h"

#include "kernel/functionemitkeys.h"
#include "kernel/input/scancode.h"
namespace hype::input {
    namespace {
        constexpr hcpp::size_t capacity = 64;
        char keys[capacity];
        hcpp::size_t count;
        void handler(void*, const InputEvent& e) noexcept {
            if (e.type == InputEventType::Key && e.value == static_cast<hcpp::u32>(KeyState::Pressed)) {
                char c = scancode_last_ascii();
                if (c && count < capacity) keys[count++] = c;
            }
        }
    }  // namespace
    void keyring_initialize() noexcept {
        count = 0;
        subscribe_keys(handler, nullptr);
    }
    hcpp::size_t keyring_count() noexcept { return count; }
    char keyring_peek(hcpp::size_t i) noexcept { return i < count ? keys[i] : 0; }
}  // namespace hype::input
