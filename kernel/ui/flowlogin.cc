#include "kernel/ui/flowlogin.h"

#include "kernel/flowlogin.h"
#include "kernel/functionemitkeys.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/overlays/toast.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/ui/widgets/textinput.h"
#include "kernel/userservice.h"

namespace hype::ui::flowlogin {

    namespace {

        constexpr char demo_user_name[] = "demo";
        constexpr char demo_pin[] = "2468";
        constexpr char wrong_pin[] = "9999";

        State screen_state = State::Hidden;
        hello::HelloResult last_submit = hello::HelloResult::NotEnrolled;
        hcpp::u32 user_uid = 0;
        hcpp::u32 session_uid = 0;
        hello::HelloMethod method = hello::HelloMethod::PinCode;
        hcpp::graphics::Framebuffer* screen_fb = nullptr;
        bool keys_hooked = false;

        Panel window;
        Flex column{FlexDirection::Column};
        Label user_label;
        Label method_label;
        TextInput secret;
        Button sign_in;
        ToastHost toasts;
        WidgetTree tree;
        bool built = false;

        void build() noexcept {
            if (built) {
                return;
            }
            const Theme& theme = Theme::dark();
            window.set_title("Sign in to HypeAI OS");

            column.set_padding(Insets::uniform(
                static_cast<hcpp::i32>(theme.spacing_lg) + 4));
            column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
            column.set_cross_align(CrossAlign::Stretch);

            user_label.set_text("user: demo");
            method_label.set_text("method: PIN \xE2\x80\x94 enter code, "
                                  "press Enter");
            method_label.set_color(LabelColor::Muted);

            secret.set_masked(true);
            secret.set_focused(true);
            secret.set_placeholder("pin");

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

    bool ensure_demo_user() noexcept {
        if (session_uid == 0) {
            session_uid = users::users_create(
                demo_user_name,
                security::capability_read | security::capability_write |
                    security::capability_execute);
        }
        user_uid = hello::hello_find_user(demo_user_name);
        if (user_uid == 0) {
            user_uid = hello::hello_register_user(demo_user_name);
        }
        if (user_uid == 0) {
            return false;
        }
        const hcpp::u32 pin_bit =
            1U << static_cast<hcpp::u8>(hello::HelloMethod::PinCode);
        if ((hello::hello_methods(user_uid) & pin_bit) != 0) {
            hello::hello_reset_failures(user_uid, hello::HelloMethod::PinCode);
            return true;
        }
        return hello::hello_enroll(user_uid, hello::HelloMethod::PinCode,
                                   demo_pin, sizeof(demo_pin) - 1);
    }

    void repaint(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        Canvas canvas(framebuffer);
        const Theme& theme = Theme::dark();
        const Rect screen{0, 0, static_cast<hcpp::i32>(framebuffer.width()),
                          static_cast<hcpp::i32>(framebuffer.height())};
        canvas.fill_rect(screen, canvas.color(theme.background));

        const Rect window_rect = centered_window(screen, 440, 300);
        tree.layout_root(window_rect);
        tree.paint_tree(canvas);
        toasts.paint(canvas, screen);
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
        toasts.clear();
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
                toasts.show("locked out", ToastKind::Error, "sign in");
                users::login_denied();
                serial::write_line("ui login fail=locked");
                break;
            case hello::HelloResult::BadSecret:
                secret.clear();
                toasts.show("wrong pin \xE2\x80\x94 try again",
                            ToastKind::Error, "sign in");
                users::login_denied();
                serial::write_line("ui login fail=toast");
                break;
            case hello::HelloResult::NotEnrolled:
                secret.clear();
                toasts.show("credential not enrolled", ToastKind::Warning,
                            "sign in");
                users::login_denied();
                serial::write_line("ui login fail=toast");
                break;
            case hello::HelloResult::NoCamera:
                secret.clear();
                toasts.show("no camera present", ToastKind::Warning,
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

    hcpp::u32 drive(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        if (!ensure_demo_user()) {
            screen_state = State::Unavailable;
            serial::write_line("ui login=unavailable");
            return 0;
        }
        if (show(framebuffer) == 0) {
            screen_state = State::Unavailable;
            serial::write_line("ui login=failed");
            return 0;
        }
        for (hcpp::size_t i = 0; wrong_pin[i] != '\0'; ++i) {
            handle_char(wrong_pin[i]);
        }
        handle_char('\n');
        if (screen_fb != nullptr && screen_state == State::Shown) {
            repaint(*screen_fb);
        }
        for (hcpp::size_t i = 0; demo_pin[i] != '\0'; ++i) {
            handle_char(demo_pin[i]);
        }
        handle_char('\n');
        return authenticated() ? 1 : 0;
    }

}  // namespace hype::ui::flowlogin
