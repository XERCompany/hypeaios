#include "kernel/ui/flowlogin.h"

#include "kernel/flowlogin.h"
#include "kernel/font/hypetext.h"
#include "kernel/functionemitkeys.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/huitokens.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/uiscale.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/inputotp.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/userservice.h"

namespace hype::ui::flowlogin {

    namespace {

        State screen_state = State::Hidden;
        hello::HelloResult last_submit = hello::HelloResult::NotEnrolled;
        hcpp::u32 user_uid = 0;
        hcpp::u32 session_uid = 0;
        char session_name[32] = {};
        char user_label_text[64] = "user: -";
        hello::HelloMethod method = hello::HelloMethod::PinCode;
        hcpp::graphics::Framebuffer* screen_fb = nullptr;
        bool keys_hooked = false;

        Panel window;
        Flex column{FlexDirection::Column};
        Label user_label;
        Label method_label;
        InputOTP secret{4};
        Button sign_in;
        OverlayHost overlays;
        WidgetTree tree;
        bool built = false;

        void build() noexcept {
            if (built) {
                return;
            }
            const Theme& theme = theme_active();
            window.set_title("Sign in to HypeAI OS");

            column.set_padding(Insets::uniform(
                static_cast<hcpp::i32>(theme.spacing_lg) + 4));
            column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
            column.set_cross_align(CrossAlign::Stretch);

            user_label.set_text(user_label_text);
            method_label.set_text("method: PIN \xE2\x80\x94 enter code, "
                                  "press Enter");
            method_label.set_color(LabelColor::Muted);

            secret.set_focused(true);

            sign_in.set_label("Sign In");
            sign_in.set_style(ButtonStyle::Accent);

            column.add_child(&user_label);
            column.add_child(&method_label);
            column.add_child(&secret);
            column.add_child(&sign_in);
            window.add_child(&column);
            tree.set_root(&window);
            built = true;
        }

        void on_key(void*, const input::InputEvent& event) noexcept {
            if (event.type != input::InputEventType::Key ||
                event.value != static_cast<hcpp::u32>(input::KeyState::Pressed)) {
                return;
            }
            handle_char(input::scancode_last_ascii());
        }

    }  // namespace

    bool set_user(hcpp::u32 uid, hcpp::StringView name) noexcept {
        if (name.empty()) {
            return false;
        }
        user_uid = uid;
        hcpp::size_t pos = 0;
        const char prefix[] = "user: ";
        for (hcpp::size_t i = 0; prefix[i] != '\0' && pos + 1 < sizeof(user_label_text);
             ++i) {
            user_label_text[pos++] = prefix[i];
        }
        for (hcpp::size_t i = 0; i < name.size() && pos + 1 < sizeof(user_label_text);
             ++i) {
            user_label_text[pos++] = name[i];
        }
        user_label_text[pos] = '\0';
        user_label.set_text(user_label_text);
        bool same = name.size() + 1 < sizeof(session_name);
        if (same) {
            for (hcpp::size_t i = 0; i < name.size(); ++i) {
                if (session_name[i] != name[i]) {
                    same = false;
                    break;
                }
            }
            same = same && session_name[name.size()] == '\0';
        }
        if (session_uid == 0 || !same) {
            session_uid = users::users_create(
                name,
                security::capability_read | security::capability_write |
                    security::capability_execute);
            hcpp::size_t j = 0;
            for (; j < name.size() && j + 1 < sizeof(session_name); ++j) {
                session_name[j] = name[j];
            }
            session_name[j] = '\0';
        }
        return true;
    }

    void repaint(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        Canvas canvas(framebuffer);
        const Theme& theme = theme_active();
        const Rect screen{0, 0, static_cast<hcpp::i32>(framebuffer.width()),
                          static_cast<hcpp::i32>(framebuffer.height())};
        canvas.fill_rect(screen, canvas.color(theme.bg));
        // Reskin v2 greeter veil: a radial falloff into the scrim tone
        // so the sign-in card reads as the only lit surface (blur-ish
        // dim without a real blur pass).
        gfx::gfx_gradient_radial(
            canvas, screen, rui_lift(theme.bg, 1),
            rui_shade(theme.bg, 8));
        canvas.fill_rect_alpha(screen, canvas.color(theme.scrim),
                               static_cast<hcpp::u8>(theme.scrim_alpha / 3));

        const Rect window_rect = centered_window(screen, 440, 300);
        tree.layout_root(window_rect);
        tree.paint_tree(canvas);

        // Avatar badge straddling the card's top edge: accent circle +
        // user monogram on a soft shadow puck.
        const hcpp::i32 av_r = ui_px(22);
        const hcpp::i32 av_cx = window_rect.x + window_rect.w / 2;
        const hcpp::i32 av_cy = window_rect.y;
        hui::draw_shadow(canvas,
                         Rect{av_cx - av_r, av_cy - av_r, av_r * 2,
                              av_r * 2},
                         av_r, hui::HuiShadowLevel::Md);
        canvas.fill_circle(av_cx, av_cy, av_r, canvas.color(theme.accent));
        canvas.stroke_circle(av_cx, av_cy, av_r,
                             canvas.color(theme.accent_hover));
        char mono[2] = {'U', '\0'};
        for (hcpp::size_t i = 6; i < sizeof(user_label_text) &&
                                 user_label_text[i] != '\0'; ++i) {
            char c = user_label_text[i];
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
            if (c >= 'A' && c <= 'Z') {
                mono[0] = c;
                break;
            }
        }
        const hcpp::u32 mono_px = static_cast<hcpp::u32>(av_r);
        if (hype::font::hypefont_ready()) {
            const hcpp::StringView mv{mono, 1};
            const hcpp::i32 tw = hype::font::text_measure(mv, mono_px);
            const hcpp::i32 line = static_cast<hcpp::i32>(
                hype::font::text_line_height(mono_px));
            draw_text_sdf_styled(canvas, av_cx - tw / 2, av_cy - line / 2,
                                 mv, canvas.color(theme.accent_fg),
                                 mono_px, 96, false);
        }
        overlays.paint_overlays(canvas, screen);
    }

    hcpp::u32 show(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        if (!framebuffer.valid()) {
            return 0;
        }
        build();
        if (!keys_hooked) {
            keys_hooked = input::subscribe_keys(&on_key, nullptr);
        }
        screen_fb = &framebuffer;
        screen_state = State::Shown;
        secret.clear();
        secret.set_focused(true);
        overlays.close();
        overlays.toasts().clear();
        users::login_reset();
        if (session_uid != 0) {
            users::login_begin(session_uid);
        }
        repaint(framebuffer);
        serial::write_line("ui login=shown");
        return widget_count(window);
    }

    bool handle_char(char ascii) noexcept {
        if (screen_state != State::Shown) {
            return false;
        }
        if (overlays.handle_key(ascii)) {
            if (screen_fb != nullptr && screen_state == State::Shown) {
                repaint(*screen_fb);
            }
            return true;
        }
        bool consumed = true;
        if (ascii == '\n' || ascii == '\r') {
            const hello::HelloResult result = submit();
            if (result == hello::HelloResult::Ok) {
                return true;
            }
        } else if (ascii == '\b') {
            consumed = secret.backspace();
        } else {
            consumed = secret.push_char(ascii);
        }
        if (screen_fb != nullptr && screen_state == State::Shown) {
            repaint(*screen_fb);
        }
        return consumed;
    }

    hello::HelloResult submit() noexcept {
        if (screen_state != State::Shown) {
            return last_submit;
        }
        last_submit = hello::hello_auth(user_uid, method, secret.data(),
                                        secret.length());
        serial::write("hello auth=");
        serial::write(hello::hello_method_name(method));
        serial::write_char(' ');
        serial::write_line(last_submit == hello::HelloResult::Ok ? "ok"
                                                               : "fail");
        switch (last_submit) {
            case hello::HelloResult::Ok:
                screen_state = State::Authenticated;
                users::login_authenticated();
                serial::write_line("ui login=ok");
                break;
            case hello::HelloResult::LockedOut:
                secret.clear();
                overlays.toasts().show("locked out", ToastKind::Error, "sign in");
                users::login_denied();
                serial::write_line("ui login fail=locked");
                break;
            case hello::HelloResult::BadSecret:
                secret.clear();
                overlays.toasts().show("wrong pin \xE2\x80\x94 try again",
                            ToastKind::Error, "sign in");
                users::login_denied();
                serial::write_line("ui login fail=toast");
                break;
            case hello::HelloResult::NotEnrolled:
                secret.clear();
                overlays.toasts().show("credential not enrolled", ToastKind::Warning,
                            "sign in");
                users::login_denied();
                serial::write_line("ui login fail=toast");
                break;
            case hello::HelloResult::NoCamera:
                secret.clear();
                overlays.toasts().show("no camera present", ToastKind::Warning,
                            "sign in");
                users::login_denied();
                serial::write_line("ui login fail=toast");
                break;
        }
        if (screen_state == State::Shown && session_uid != 0) {
            users::login_begin(session_uid);
        }
        return last_submit;
    }

    State state() noexcept { return screen_state; }

    bool authenticated() noexcept {
        return screen_state == State::Authenticated;
    }

    hello::HelloResult last_result() noexcept { return last_submit; }

}  // namespace hype::ui::flowlogin
