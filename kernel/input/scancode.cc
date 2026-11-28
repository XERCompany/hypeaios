#include "kernel/input/scancode.h"

#include "kernel/session/idlelock.h"

namespace hype::input {
    namespace {
        constexpr hcpp::u16 left_shift = 42;
        constexpr hcpp::u16 right_shift = 54;
        constexpr hcpp::u16 caps_lock_code = 58;
        KeyBitset pressed_keys;
        bool extended_prefix;
        bool caps_lock;
        hcpp::u64 event_count;
        char last_ascii;
        const char lowercase_table[128] = {
            0,    27,  '1', '2',  '3', '4', '5', '6', '7',  '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
            't',  'y', 'u', 'i',  'o', 'p', '[', ']', '\n', 0,   'a', 's', 'd', 'f', 'g',  'h',  'j', 'k', 'l', ';',
            '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', 'b',  'n', 'm', ',', '.', '/', 0,    '*',  0,   ' ', 0,
        };
        const char uppercase_table[128] = {
            0,   27,  '!', '@', '#', '$', '%', '^', '&',  '*', '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',
            'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A', 'S', 'D', 'F', 'G',  'H',  'J', 'K', 'L', ':',
            '"', '~', 0,   '|', 'Z', 'X', 'C', 'V', 'B',  'N', 'M', '<', '>', '?', 0,    '*',  0,   ' ', 0,
        };
    }  // namespace

    void scancode_initialize() noexcept {
        keybit_clear_all(pressed_keys);
        extended_prefix = false;
        caps_lock = false;
        event_count = 0;
    }

    DecodedKey scancode_decode(hcpp::u8 scancode) noexcept {
        // Idle-lock pet (issue #134): every byte here is a real
        // hardware scancode — the PS/2 IRQ1 handler and the xHCI HID
        // report path both decode through this choke point, while the
        // synthetic emitters (gdxf selftest, input_device_report_key)
        // bypass it by calling emit_key_event directly.
        hype::session::idle_lock_pet();
        if (scancode == 0xE0 || scancode == 0xE1) {
            extended_prefix = true;
            return {0, 0, false, true, false};
        }
        const bool extended = extended_prefix;
        extended_prefix = false;
        const bool pressed = (scancode & 0x80) == 0;
        hcpp::u16 code = scancode & 0x7F;
        ++event_count;
        // PrintScreen (issue #131): set-1 PrtSc make is "E0 2A E0 37" and
        // the break is "E0 B7 E0 AA". The extended 0x2A is a fake-shift
        // pad byte that must neither light the real left-shift bit nor
        // emit a spurious shift key event — swallow it outright. The E0
        // prefix on 0x37 only ever comes from PrtSc (plain 0x37 is the
        // keypad '*' key; Alt+PrtSc sends E0 37 without the pad), so it
        // remaps to the synthetic key_prtsc code.
        if (extended && code == 0x2A) {
            return {0, 0, false, true, false};
        }
        if (extended && code == 0x37) {
            code = key_prtsc;
        }
        if (pressed)
            keybit_set(pressed_keys, code);
        else
            keybit_clear(pressed_keys, code);
        if (pressed && code == caps_lock_code) caps_lock = !caps_lock;

        char ascii = 0;
        last_ascii = 0;
        if (pressed && code < 128) {
            const bool letter = lowercase_table[code] >= 'a' && lowercase_table[code] <= 'z';
            const bool shifted = scancode_shift_active() != (letter && caps_lock);
            ascii = shifted ? uppercase_table[code] : lowercase_table[code];
        }
        if (ascii != 0) last_ascii = ascii;
        return {code, ascii, pressed, extended, true};
    }

    bool scancode_key_pressed(hcpp::u16 key_code) noexcept { return keybit_test(pressed_keys, key_code); }
    bool scancode_shift_active() noexcept {
        return keybit_test(pressed_keys, left_shift) || keybit_test(pressed_keys, right_shift);
    }
    bool scancode_caps_lock_active() noexcept { return caps_lock; }
    hcpp::u64 scancode_event_count() noexcept { return event_count; }
    char scancode_last_ascii() noexcept { return last_ascii; }
    const char* scancode_key_name(hcpp::u16 key_code) noexcept {
        switch (key_code) {
            case 1:
                return "escape";
            case 14:
                return "backspace";
            case 15:
                return "tab";
            case 28:
                return "enter";
            case 29:
                return "control";
            case 42:
                return "left shift";
            case 54:
                return "right shift";
            case 56:
                return "alt";
            case 57:
                return "space";
            case 58:
                return "caps lock";
            case 72:
                return "up";
            case 75:
                return "left";
            case 77:
                return "right";
            case 80:
                return "down";
            case key_prtsc:
                return "print screen";
            default:
                return "key";
        }
    }

}  // namespace hype::input
