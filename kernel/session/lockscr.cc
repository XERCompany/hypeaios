#include "kernel/session/lockscr.h"

#include "hcpp/graphics/text.hpp"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/functionemitkeys.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/input/hypeptr.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/time/rtc.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"

namespace hype::session {

    namespace {

        // Set-1 scancodes for the lock/power chords and the unlock key.
        constexpr hcpp::u16 key_escape = 1;
        constexpr hcpp::u16 key_enter = 28;
        constexpr hcpp::u16 key_ctrl = 29;
        constexpr hcpp::u16 key_l = 38;
        constexpr hcpp::u16 key_alt = 56;
        constexpr hcpp::u16 key_end = 79;
        constexpr hcpp::u16 key_del = 83;
        constexpr hcpp::u16 key_lwin = 91;
        constexpr hcpp::u16 key_rwin = 92;

        bool keys_hooked = false;
        bool veil_dirty = true;
        hcpp::i32 last_minute = -1;

        void put2(char* dst, hcpp::u32 value) noexcept {
            dst[0] = static_cast<char>('0' + (value / 10) % 10);
            dst[1] = static_cast<char>('0' + value % 10);
        }

        void put4(char* dst, hcpp::u32 value) noexcept {
            dst[0] = static_cast<char>('0' + (value / 1000) % 10);
            dst[1] = static_cast<char>('0' + (value / 100) % 10);
            dst[2] = static_cast<char>('0' + (value / 10) % 10);
            dst[3] = static_cast<char>('0' + value % 10);
        }

        hcpp::u32 fallback_scale(hcpp::u32 px) noexcept {
            hcpp::u32 scale = px / 16;
            return scale == 0 ? 1 : scale;
        }

        hcpp::i32 text_width(hcpp::StringView text, hcpp::u32 px) noexcept {
            if (font::hypefont_ready()) {
                return font::text_measure(text, px);
            }
            return static_cast<hcpp::i32>(text.size() * 8 *
                                          fallback_scale(px));
        }

        void draw_centered(ui::Canvas& canvas, hcpp::i32 cx, hcpp::i32 y,
                           hcpp::StringView text, hcpp::u32 rgb,
                           hcpp::u32 px) noexcept {
            hcpp::i32 x = cx - text_width(text, px) / 2;
            if (x < 0) {
                x = 0;
            }
            if (font::hypefont_ready()) {
                ui::draw_text_sdf(canvas, x, y, text, canvas.color(rgb), px);
                return;
            }
            hcpp::graphics::draw_text(canvas.target(),
                                      static_cast<hcpp::u32>(x),
                                      static_cast<hcpp::u32>(y), text,
                                      canvas.color(rgb), fallback_scale(px));
        }

        void paint_lock(hcpp::graphics::Framebuffer& fb) noexcept {
            ui::Canvas canvas(fb);
            const ui::Theme& theme = ui::theme_active();
            const ui::Rect screen{0, 0, static_cast<hcpp::i32>(fb.width()),
                                  static_cast<hcpp::i32>(fb.height())};
            canvas.set_clip(screen);
            // Dark veil: theme bg at center fading into a slight accent
            // vignette at the edges, then a bg alpha pass to mute it.
            gfx::gfx_gradient_radial(canvas, screen, ui::rui_lift(theme.bg, 1),
                                     ui::rui_shade(theme.accent, 60));
            canvas.fill_rect_alpha(screen,
                                   canvas.color(ui::rui_shade(theme.bg, 10)),
                                   110);

            char clock_text[6] = "--:--";
            char date_text[11] = "----------";
            time::RtcTime now{};
            if (time::rtc_now(now)) {
                put2(clock_text + 0, now.hour);
                clock_text[2] = ':';
                put2(clock_text + 3, now.minute);
                clock_text[5] = '\0';
                put4(date_text + 0, now.year);
                date_text[4] = '-';
                put2(date_text + 5, now.month);
                date_text[7] = '-';
                put2(date_text + 8, now.day);
                date_text[10] = '\0';
            }

            const hcpp::i32 cx = screen.x + screen.w / 2;
            const hcpp::i32 clock_y = screen.y + screen.h / 3;
            draw_centered(canvas, cx, clock_y, clock_text, theme.fg, 96);
            draw_centered(canvas, cx, clock_y + 112, date_text,
                          theme.muted_fg, 20);
            const char* name = session_user_name();
            draw_centered(canvas, cx, clock_y + 176,
                          name != nullptr && name[0] != '\0' ? name : "user",
                          theme.card_fg, 24);
            // Small accent underline under the user name.
            canvas.fill_rect(ui::Rect{cx - 40, clock_y + 208, 80, 2},
                             canvas.color(theme.accent));
            draw_centered(canvas, cx,
                          screen.bottom() - static_cast<hcpp::i32>(screen.h) / 6,
                          "Press Enter or click to unlock", theme.muted_fg,
                          15);
        }

        void lock_key_event(void*, const input::InputEvent& e) noexcept {
            if (e.type != input::InputEventType::Key ||
                e.value ==
                    static_cast<hcpp::u32>(input::KeyState::Released)) {
                return;
            }
            if (session_locked()) {
                // Only Enter unlocks; every other key is swallowed.
                if (e.code == key_enter) {
                    session_unlock();
                }
                return;
            }
            if (session_state() != SessionState::LoggedIn) {
                return;
            }
            const bool ctrl = input::scancode_key_pressed(key_ctrl);
            const bool alt = input::scancode_key_pressed(key_alt);
            const bool win = input::scancode_key_pressed(key_lwin) ||
                             input::scancode_key_pressed(key_rwin);
            if (e.code == key_l && (win || (ctrl && alt))) {
                session_lock();
                return;
            }
            if (ctrl && alt && (e.code == key_end || e.code == key_del)) {
                session_power_dialog();
                return;
            }
            if (e.code == key_escape) {
                session_power_dialog_close();
            }
        }

    }  // namespace

    void lockscr_init() noexcept {
        if (keys_hooked) {
            return;
        }
        keys_hooked = input::subscribe_keys(&lock_key_event, nullptr);
    }

    void lockscr_enter() noexcept {
        veil_dirty = true;
        last_minute = -1;
    }

    bool lockscr_frame(hcpp::graphics::Framebuffer& fb) noexcept {
        if (!fb.valid()) {
            return false;
        }
        time::RtcTime now{};
        if (time::rtc_now(now) &&
            static_cast<hcpp::i32>(now.minute) != last_minute) {
            last_minute = now.minute;
            veil_dirty = true;
        }
        if (!veil_dirty) {
            return false;
        }
        input::cursor_erase(fb);
        paint_lock(fb);
        veil_dirty = false;
        return true;
    }

}  // namespace hype::session
