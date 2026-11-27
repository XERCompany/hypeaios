#include "kernel/gdxf/gdxf.h"

#include "kernel/functionemitkeys.h"
#include "kernel/gdxf/gdxfint.h"
#include "kernel/input/hypeptr.h"
#include "kernel/input/scancode.h"
#include "kernel/istatekeys.h"

namespace hype::gdxf {

    namespace {

        void on_input_event(void* context,
                            const input::InputEvent& event) noexcept {
            (void)context;
            if (event.type != input::InputEventType::Key ||
                event.code > input::max_key_code) {
                return;
            }
            const auto state = static_cast<input::KeyState>(event.value);
            if (state == input::KeyState::Released) {
                input::keybit_clear(g_gdxf.keys_down, event.code);
            } else {
                // edge only on the initial press, not on key-repeat
                if (!input::keybit_test(g_gdxf.keys_down, event.code)) {
                    input::keybit_set(g_gdxf.keys_edge_next, event.code);
                }
                input::keybit_set(g_gdxf.keys_down, event.code);
            }
        }

    }  // namespace

    void gdxf_input_hook() noexcept {
        if (!g_gdxf.input_hooked) {
            g_gdxf.input_hooked =
                input::subscribe_keys(&on_input_event, nullptr);
        }
    }

    void gdxf_input_pump() noexcept {
        g_gdxf.keys_edge_frame = g_gdxf.keys_edge_next;
        input::keybit_clear_all(g_gdxf.keys_edge_next);
        const input::CurStat cur = input::cursor_stat();
        g_gdxf.mouse.x = cur.x;
        g_gdxf.mouse.y = cur.y;
        g_gdxf.mouse.buttons = cur.buttons;
        g_gdxf.mouse.wheel = 0;  // 3-byte PS/2 packets carry no wheel data
        g_gdxf.mouse_ready = cur.ready;
    }

    bool gdxf_key_down(hcpp::u16 key_code) noexcept {
        return input::keybit_test(g_gdxf.keys_down, key_code) ||
               input::scancode_key_pressed(key_code);
    }

    bool gdxf_key_pressed(hcpp::u16 key_code) noexcept {
        return input::keybit_test(g_gdxf.keys_edge_frame, key_code);
    }

    bool gdxf_mouse(MouseState& out) noexcept {
        out = g_gdxf.mouse;
        return g_gdxf.mouse_ready;
    }

    hcpp::u8 gdxf_gamepad_count() noexcept {
        return 0;  // no gamepad backend until virtio-input lands
    }

    bool gdxf_gamepad_state(hcpp::u8 index, GamepadState& out) noexcept {
        (void)index;
        out = GamepadState{};
        return false;
    }

}  // namespace hype::gdxf
