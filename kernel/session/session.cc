#include "kernel/session/session.h"

#include "kernel/flowlogin.h"
#include "kernel/functionemitkeys.h"
#include "kernel/hello/hello.h"
#include "kernel/input/scancode.h"
#include "kernel/power.h"
#include "kernel/sched/watchdog.h"
#include "kernel/serial.hpp"
#include "kernel/session/idlelock.h"
#include "kernel/session/lockscr.h"
#include "kernel/svc/revsvc.h"
#include "kernel/time/rtc.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/flowlogin.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/inputotp.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/ui/widgets/textinput.h"
#include "kernel/userservice.h"
#include "kernel/ui/deskbar.h"
#include "kernel/win/winman.h"

namespace hype::session {

    namespace {

        constexpr char greeter_user_name[] = "demo";
        constexpr char scripted_password[] = "hype-demo-pass";
        constexpr char scripted_pin[] = "2468";
        constexpr char scripted_wrong_pin[] = "9999";

        SessionState current_state = SessionState::Booting;
        hcpp::u32 active_uid = 0;
        hcpp::u32 login_user_uid = 0;
        char active_name[32] = {};
        hcpp::graphics::Framebuffer* shell_fb = nullptr;
        bool power_dialog = false;

        void copy_active_name(hcpp::StringView name) noexcept {
            hcpp::size_t i = 0;
            for (; i + 1 < sizeof(active_name) && i < name.size(); ++i) {
                active_name[i] = name[i];
            }
            active_name[i] = '\0';
        }

        constexpr hcpp::u32 power_id_shutdown = 1;
        constexpr hcpp::u32 power_id_restart = 2;

        // Rendered through the shared winman context-menu machinery
        // (rui_panel + scrim + click-outside dismiss). Row heights mirror
        // winman's ctx_item_h/ctx_sep_h/menu_pad so the anchor math below
        // lands the menu centered.
        const win::WmCtxItem power_items[] = {
            {"Power", 0, win::wm_ctx_disabled},
            {nullptr, 0, win::wm_ctx_separator},
            {"Shut Down", power_id_shutdown, 0},
            {"Restart", power_id_restart, 0},
            {"Cancel", 3, 0},
        };
        constexpr hcpp::i32 power_menu_w = 180;
        constexpr hcpp::i32 power_menu_h = 4 * 2 + 28 + 9 + 3 * 28;

        void power_pick(hcpp::u32 id, void*) noexcept {
            power_dialog = false;
            serial::write_line("power dialog=closed");
            if (id == power_id_shutdown) {
                power::power_shutdown();
            } else if (id == power_id_restart) {
                power::power_restart();
            }
        }

        void log_login_timestamp() noexcept {
            char text[20];
            if (hype::time::rtc_format(text)) {
                serial::write("session login_at=");
                serial::write_line(text);
            }
        }

        ui::Panel reg_window;
        ui::Flex reg_column{ui::FlexDirection::Column};
        ui::Label reg_title;
        ui::Label reg_user_label;
        ui::TextInput reg_username{"pick a username"};
        ui::Label reg_pass_label;
        ui::TextInput reg_password{"choose a password"};
        ui::Label reg_pin_label;
        ui::InputOTP reg_pin{4};
        ui::Button reg_button;
        ui::OverlayHost reg_overlays;
        ui::WidgetTree reg_tree;
        bool reg_built = false;
        bool reg_keys_hooked = false;
        hcpp::u8 reg_focus = 0;
        hcpp::graphics::Framebuffer* reg_fb = nullptr;

        void write_users_marker() noexcept {
            serial::write("session users=");
            serial::write_dec32(hello::hello_user_count());
            serial::write("\r\n");
        }

        void reg_build() noexcept {
            if (reg_built) {
                return;
            }
            const ui::Theme& theme = ui::theme_active();
            reg_window.set_title("Create your HypeAI account");
            reg_column.set_padding(ui::Insets::uniform(
                static_cast<hcpp::i32>(theme.spacing_lg) + 4));
            reg_column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
            reg_column.set_cross_align(ui::CrossAlign::Stretch);

            reg_title.set_text("registration \xE2\x80\x94 new account");
            reg_user_label.set_text("username");
            reg_user_label.set_color(ui::LabelColor::Muted);
            reg_pass_label.set_text("password");
            reg_pass_label.set_color(ui::LabelColor::Muted);
            reg_password.set_masked(true);
            reg_pin_label.set_text(
                "pin \xE2\x80\x94 4 digits, Enter to finish");
            reg_pin_label.set_color(ui::LabelColor::Muted);
            reg_button.set_label("Create Account");
            reg_button.set_style(ui::ButtonStyle::Accent);

            reg_column.add_child(&reg_title);
            reg_column.add_child(&reg_user_label);
            reg_column.add_child(&reg_username);
            reg_column.add_child(&reg_pass_label);
            reg_column.add_child(&reg_password);
            reg_column.add_child(&reg_pin_label);
            reg_column.add_child(&reg_pin);
            reg_column.add_child(&reg_button);
            reg_window.add_child(&reg_column);
            reg_tree.set_root(&reg_window);
            reg_built = true;
        }

        void reg_repaint(hcpp::graphics::Framebuffer& fb) noexcept {
            ui::Canvas canvas(fb);
            const ui::Theme& theme = ui::theme_active();
            const ui::Rect screen{0, 0, static_cast<hcpp::i32>(fb.width()),
                                  static_cast<hcpp::i32>(fb.height())};
            canvas.fill_rect(screen, canvas.color(theme.bg));
            const ui::Rect window_rect = ui::centered_window(screen, 460, 400);
            reg_tree.layout_root(window_rect);
            reg_tree.paint_tree(canvas);
            reg_overlays.paint_overlays(canvas, screen);
        }

        void reg_focus_set(hcpp::u8 index) noexcept {
            reg_focus = index;
            reg_username.set_focused(index == 0);
            reg_password.set_focused(index == 1);
            reg_pin.set_focused(index == 2);
        }

        bool finish_auth(hcpp::u32 uid, const void* pin,
                         hcpp::size_t pin_size) noexcept {
            current_state = SessionState::Authenticating;
            const hello::HelloResult result =
                hello::hello_auth(uid, hello::HelloMethod::PinCode, pin, pin_size);
            serial::write("hello auth=pincode ");
            serial::write_line(result == hello::HelloResult::Ok ? "ok" : "fail");
            if (result != hello::HelloResult::Ok) {
                current_state = SessionState::Greeter;
                serial::write_line("session auth=fail");
                return false;
            }
            if (login_user_uid != 0) {
                users::login_reset();
                users::login_begin(login_user_uid);
                users::login_authenticated();
            }
            active_uid = uid;
            current_state = SessionState::LoggedIn;
            // Reset the idle clock (issue #134): the login itself is
            // activity, so a long greeter dwell cannot re-lock instantly.
            idle_lock_reset();
            serial::write_line("session auth=ok");
            log_login_timestamp();
            return true;
        }

        bool reg_submit() noexcept {
            if (reg_username.length() == 0) {
                reg_overlays.toasts().show("username required",
                                           ui::ToastKind::Error, "register");
                return false;
            }
            if (reg_password.length() == 0) {
                reg_overlays.toasts().show("password required",
                                           ui::ToastKind::Error, "register");
                return false;
            }
            if (!reg_pin.complete()) {
                reg_overlays.toasts().show("pin must be 4 digits",
                                           ui::ToastKind::Error, "register");
                return false;
            }
            const hcpp::u32 uid = session_register(
                hcpp::StringView(reg_username.data(), reg_username.length()),
                reg_password.data(), reg_password.length(), reg_pin.data(),
                reg_pin.length());
            if (uid == 0) {
                reg_overlays.toasts().show("registration failed",
                                           ui::ToastKind::Error, "register");
                return false;
            }
            return finish_auth(uid, reg_pin.data(), reg_pin.length());
        }

        bool reg_handle_char(char ascii) noexcept {
            if (current_state != SessionState::Registering) {
                return false;
            }
            if (reg_overlays.handle_key(ascii)) {
                if (reg_fb != nullptr &&
                    current_state == SessionState::Registering) {
                    reg_repaint(*reg_fb);
                }
                return true;
            }
            bool consumed = true;
            if (ascii == '\t') {
                reg_focus_set(static_cast<hcpp::u8>((reg_focus + 1) % 3));
            } else if (ascii == '\n' || ascii == '\r') {
                if (reg_focus < 2) {
                    reg_focus_set(static_cast<hcpp::u8>(reg_focus + 1));
                } else if (reg_submit()) {
                    return true;
                }
            } else if (ascii == '\b') {
                consumed = reg_focus == 0   ? reg_username.backspace()
                           : reg_focus == 1 ? reg_password.backspace()
                                            : reg_pin.backspace();
            } else {
                consumed = reg_focus == 0   ? reg_username.push_char(ascii)
                           : reg_focus == 1 ? reg_password.push_char(ascii)
                                            : reg_pin.push_char(ascii);
            }
            if (reg_fb != nullptr &&
                current_state == SessionState::Registering) {
                reg_repaint(*reg_fb);
            }
            return consumed;
        }

        void on_key(void*, const input::InputEvent& event) noexcept {
            if (event.type != input::InputEventType::Key ||
                event.value !=
                    static_cast<hcpp::u32>(input::KeyState::Pressed)) {
                return;
            }
            session_handle_char(input::scancode_last_ascii());
        }

        hcpp::u32 reg_show(hcpp::graphics::Framebuffer& fb) noexcept {
            if (!fb.valid()) {
                return 0;
            }
            reg_build();
            if (!reg_keys_hooked) {
                reg_keys_hooked = input::subscribe_keys(&on_key, nullptr);
            }
            reg_fb = &fb;
            reg_username.clear();
            reg_password.clear();
            reg_pin.clear();
            reg_focus_set(0);
            reg_overlays.close();
            reg_overlays.toasts().clear();
            reg_repaint(fb);
            serial::write_line("session reg=shown");
            return ui::widget_count(reg_window);
        }

    }  // namespace

    SessionState session_state() noexcept { return current_state; }

    const char* session_state_name(SessionState state) noexcept {
        switch (state) {
            case SessionState::Booting: return "booting";
            case SessionState::Greeter: return "greeter";
            case SessionState::Registering: return "registering";
            case SessionState::Authenticating: return "authenticating";
            case SessionState::LoggedIn: return "logged-in";
            case SessionState::Locked: return "locked";
        }
        return "unknown";
    }

    hcpp::u32 session_user() noexcept { return active_uid; }

    const char* session_user_name() noexcept { return active_name; }

    bool session_logged_in() noexcept {
        return current_state == SessionState::LoggedIn;
    }

    bool session_locked() noexcept {
        return current_state == SessionState::Locked;
    }

    void session_lock() noexcept {
        if (current_state != SessionState::LoggedIn) {
            return;
        }
        // Tear down transient shell UI so nothing overlays the veil.
        session_power_dialog_close();
        if (ui::deskbar_start_open()) {
            ui::deskbar_close_start();
        }
        if (ui::deskbar_qs_open()) {
            ui::deskbar_close_qs();
        }
        if (ui::deskbar_nc_open()) {
            ui::deskbar_close_nc();
        }
        current_state = SessionState::Locked;
        lockscr_enter();
        serial::write_line("session locked=1");
    }

    void session_unlock() noexcept {
        if (current_state != SessionState::Locked) {
            return;
        }
        current_state = SessionState::LoggedIn;
        // Reset the idle clock (issue #134): however long the veil was
        // up, the unlock gesture itself counts as activity.
        idle_lock_reset();
        serial::write_line("session locked=0");
        // Force a full desktop repaint under the lifted veil.
        win::wm_invalidate_all();
    }

    void session_power_dialog() noexcept {
        if (current_state != SessionState::LoggedIn) {
            return;
        }
        if (ui::deskbar_start_open()) {
            ui::deskbar_close_start();
        }
        hcpp::i32 sw = 1024;
        hcpp::i32 sh = 768;
        if (shell_fb != nullptr && shell_fb->valid()) {
            sw = static_cast<hcpp::i32>(shell_fb->width());
            sh = static_cast<hcpp::i32>(shell_fb->height());
        }
        const hcpp::i32 x = (sw - power_menu_w) / 2;
        const hcpp::i32 y = (sh - power_menu_h) / 2;
        if (!win::wm_ctx_open(x, y, power_items, 5, &power_pick,
                              nullptr)) {
            serial::write_line("power dialog=fail");
            return;
        }
        power_dialog = true;
        serial::write_line("power dialog=open");
    }

    bool session_power_dialog_active() noexcept {
        // The menu can also close itself (click-outside pick path), so
        // reconcile the flag against the real overlay state.
        power_dialog = power_dialog && win::wm_ctx_visible();
        return power_dialog;
    }

    void session_power_dialog_close() noexcept {
        if (!power_dialog) {
            return;
        }
        win::wm_ctx_close();
        power_dialog = false;
        serial::write_line("power dialog=closed");
    }

    bool session_frame(hcpp::graphics::Framebuffer& fb) noexcept {
        if (current_state != SessionState::Locked) {
            return false;
        }
        // Watchdog heartbeat (issue #140): the locked branch of the
        // shell idle loop calls this every pass — the "ui" source must
        // stay fed while the lock veil owns the screen instead of
        // deskbar_idle().
        wdt::wdt_pet_name("ui");
        shell_fb = &fb;
        return lockscr_frame(fb);
    }

    hcpp::u32 session_register(hcpp::StringView name, const void* password,
                               hcpp::size_t password_size, const void* pin,
                               hcpp::size_t pin_size) noexcept {
        if (name.empty() || password == nullptr || password_size == 0 ||
            pin == nullptr || pin_size == 0) {
            serial::write_line("session reg=fail");
            return 0;
        }
        hcpp::u32 uid = hello::hello_find_user(name);
        if (uid == 0) {
            uid = hello::hello_register_user(name);
        }
        if (uid == 0) {
            serial::write_line("session reg=fail");
            return 0;
        }
        const hcpp::u32 password_bit =
            1U << static_cast<hcpp::u8>(hello::HelloMethod::Password);
        const hcpp::u32 pin_bit =
            1U << static_cast<hcpp::u8>(hello::HelloMethod::PinCode);
        const hcpp::u32 methods = hello::hello_methods(uid);
        bool ok = true;
        if ((methods & password_bit) == 0) {
            ok = hello::hello_enroll(uid, hello::HelloMethod::Password,
                                     password, password_size);
        }
        if (ok && (methods & pin_bit) == 0) {
            ok = hello::hello_enroll(uid, hello::HelloMethod::PinCode, pin,
                                     pin_size);
        }
        if (!ok) {
            serial::write_line("session reg=fail");
            return 0;
        }
        hello::hello_reset_failures(uid, hello::HelloMethod::Password);
        hello::hello_reset_failures(uid, hello::HelloMethod::PinCode);
        if (login_user_uid == 0) {
            login_user_uid = users::users_create(
                name, security::capability_read | security::capability_write |
                          security::capability_execute);
        }
        active_uid = uid;
        copy_active_name(name);
        serial::write_line("session reg=ok");
        write_users_marker();
        return uid;
    }

    hcpp::u32 session_begin(hcpp::graphics::Framebuffer& fb) noexcept {
        current_state = SessionState::Greeter;
        shell_fb = &fb;
        lockscr_init();
        serial::write_line("session state=greeter");
        hcpp::u32 running = 0;
        const svc::RevService* services = svc::svc_list();
        const hcpp::size_t total = svc::svc_count();
        for (hcpp::size_t i = 0; i < total; ++i) {
            if (services[i].state == svc::SvcState::Running) {
                ++running;
            }
        }
        serial::write("session services=");
        serial::write_dec32(running);
        serial::write("\r\n");
        if (hello::hello_user_count() == 0) {
            current_state = SessionState::Registering;
            serial::write_line("session state=registering");
            return reg_show(fb);
        }
        const hcpp::u32 uid = hello::hello_find_user(greeter_user_name);
        copy_active_name(greeter_user_name);
        ui::flowlogin::set_user(uid, greeter_user_name);
        const hcpp::u32 shown = ui::flowlogin::show(fb);
        if (shown == 0) {
            serial::write_line("session greeter=unavailable");
        }
        return shown;
    }

    hcpp::u32 session_scripted_auth(hcpp::graphics::Framebuffer& fb) noexcept {
        hcpp::u32 uid = hello::hello_find_user(greeter_user_name);
        if (uid == 0) {
            uid = session_register(greeter_user_name, scripted_password,
                                   sizeof(scripted_password) - 1, scripted_pin,
                                   sizeof(scripted_pin) - 1);
        } else {
            if (login_user_uid == 0) {
                login_user_uid = users::users_create(
                    greeter_user_name,
                    security::capability_read | security::capability_write |
                        security::capability_execute);
            }
            const hcpp::u32 pin_bit =
                1U << static_cast<hcpp::u8>(hello::HelloMethod::PinCode);
            if ((hello::hello_methods(uid) & pin_bit) == 0 &&
                !hello::hello_enroll(uid, hello::HelloMethod::PinCode,
                                     scripted_pin, sizeof(scripted_pin) - 1)) {
                serial::write_line("session auth=fail");
                return 0;
            }
            hello::hello_reset_failures(uid, hello::HelloMethod::PinCode);
            active_uid = uid;
            copy_active_name(greeter_user_name);
            write_users_marker();
        }
        if (uid == 0) {
            serial::write_line("session auth=fail");
            return 0;
        }
        ui::flowlogin::set_user(uid, greeter_user_name);
        if (current_state == SessionState::Registering ||
            ui::flowlogin::state() != ui::flowlogin::State::Shown) {
            if (ui::flowlogin::show(fb) == 0) {
                serial::write_line("session auth=fail");
                return 0;
            }
        }
        current_state = SessionState::Authenticating;
        for (hcpp::size_t i = 0; scripted_wrong_pin[i] != '\0'; ++i) {
            ui::flowlogin::handle_char(scripted_wrong_pin[i]);
        }
        ui::flowlogin::handle_char('\n');
        for (hcpp::size_t i = 0; scripted_pin[i] != '\0'; ++i) {
            ui::flowlogin::handle_char(scripted_pin[i]);
        }
        ui::flowlogin::handle_char('\n');
        if (!ui::flowlogin::authenticated()) {
            current_state = SessionState::Greeter;
            serial::write_line("session auth=fail");
            return 0;
        }
        if (login_user_uid != 0) {
            users::login_authenticated();
        }
        active_uid = uid;
        current_state = SessionState::LoggedIn;
        idle_lock_reset();
        serial::write_line("session auth=ok");
        log_login_timestamp();
        return 1;
    }

    bool session_handle_char(char ascii) noexcept {
        if (current_state == SessionState::Locked) {
            return true;
        }
        if (current_state == SessionState::Registering) {
            return reg_handle_char(ascii);
        }
        return ui::flowlogin::handle_char(ascii);
    }

}  // namespace hype::session
