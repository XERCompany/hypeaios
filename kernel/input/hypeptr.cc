#include "kernel/input/hypeptr.h"

#include "kernel/console/fbcon.h"
#include "kernel/functionemitkeys.h"
#include "kernel/input/cursprite.h"
#include "kernel/input/msaux.h"
#include "kernel/input/ptrmv.h"
#include "kernel/sched/watchdog.h"
#include "kernel/serial.hpp"
#include "kernel/usb/xhci.h"
#include "kernel/session/session.h"
#include "kernel/win/winman.h"

namespace hype::input {

    namespace {

        bool initialized;
        MsauxMode last_mode = MsauxMode::Probing;

        void route_wheel(hcpp::i32 delta) noexcept {
            serial::write("ptr wheel=");
            if (delta < 0) {
                serial::write_char('-');
                serial::write_dec32(static_cast<hcpp::u32>(-delta));
            } else {
                serial::write_dec32(static_cast<hcpp::u32>(delta));
            }
            serial::write("\r\n");
            emit_input_event({InputEventType::Relative, rel_wheel_code,
                              static_cast<hcpp::u32>(delta), 0});
        }

        void route_mode() noexcept {
            const MsauxMode mode = msaux_mode();
            if (mode == last_mode) {
                return;
            }
            last_mode = mode;
            if (mode == MsauxMode::Irq) {
                serial::write_line("ptr irq=ok");
            } else if (mode == MsauxMode::Poll) {
                serial::write_line("ptr irq=poll");
            }
        }

        void route_buttons(hcpp::u8 before, hcpp::u8 after, hcpp::i32 x,
                           hcpp::i32 y) noexcept {
            const hcpp::u8 rising = static_cast<hcpp::u8>(after & ~before);
            const hcpp::u8 falling = static_cast<hcpp::u8>(before & ~after);
            // Locked session: a press unlocks, every other button edge is
            // swallowed before it can reach the WM or apps.
            if (hype::session::session_locked()) {
                if ((rising &
                     (ptr_button_left | ptr_button_right | ptr_button_middle)) != 0) {
                    serial::write_line("ptr click=unlock");
                    hype::session::session_unlock();
                }
                return;
            }
            // fbcon debug view owns the screen (issue #139): swallow
            // pointer edges so clicks cannot reach the hidden WM/apps.
            if (hype::console::fbcon_active()) {
                return;
            }
            if ((rising & ptr_button_left) != 0) {
                serial::write("ptr click=");
                serial::write_dec32(static_cast<hcpp::u32>(x));
                serial::write_char(',');
                serial::write_dec32(static_cast<hcpp::u32>(y));
                serial::write(" hit=");
                if (!hype::session::session_logged_in()) {
                    serial::write_line("locked");
                    return;
                }
                if (hype::win::wm_handle_mouse(x, y, true)) {
                    serial::write_line("wm");
                    ptr_mark_dirty();
                } else {
                    serial::write_line("none");
                }
            }
            if ((falling & ptr_button_left) != 0) {
                if (hype::session::session_logged_in() &&
                    hype::win::wm_handle_mouse(x, y, false)) {
                    ptr_mark_dirty();
                }
            }
            if ((rising & ptr_button_right) != 0) {
                serial::write("ptr rclick=");
                serial::write_dec32(static_cast<hcpp::u32>(x));
                serial::write_char(',');
                serial::write_dec32(static_cast<hcpp::u32>(y));
                serial::write(" hit=");
                if (!hype::session::session_logged_in()) {
                    serial::write_line("locked");
                    return;
                }
                if (hype::win::wm_handle_mouse_r(x, y)) {
                    serial::write_line("wm");
                    ptr_mark_dirty();
                } else {
                    serial::write_line("none");
                }
            }
        }

    }

    bool cursor_init(hcpp::u32 screen_w, hcpp::u32 screen_h) noexcept {
        ptr_set_bounds(static_cast<hcpp::i32>(screen_w),
                       static_cast<hcpp::i32>(screen_h));
        ptr_home();
        const bool png_ok = cursprite_init();
        serial::write("cursor src=");
        serial::write_line(png_ok ? "png" : "arrow");
        serial::write("cursor sprite=");
        serial::write_line(png_ok ? "png" : "arrow");
        const bool ok = msaux_init();
        initialized = ok;
        serial::write("ptr id=");
        serial::write_dec32(msaux_mouse_id());
        serial::write(" pkt=");
        serial::write_dec32(msaux_packet_size());
        serial::write("\r\n");
        serial::write_line("rbtn wired=ok");
        return ok;
    }

    bool cursor_ready() noexcept { return initialized && msaux_ready(); }

    bool cursor_poll() noexcept {
        // Watchdog heartbeat (issue #140): the shell idle loop calls
        // this unconditionally at the top of every pass (logged-in,
        // locked and pre-login alike), so its freshness tracks the
        // whole BSP main loop, not just the pointer path.
        wdt::wdt_pet_name("input");
        MsauxPacket packets[16];
        const hcpp::u8 before = ptr_buttons();
        const hcpp::u32 n = msaux_poll(packets, 16);
        bool moved = false;
        for (hcpp::u32 i = 0; i < n; ++i) {
            ptr_apply(packets[i]);
            if (packets[i].dx != 0 || packets[i].dy != 0) {
                moved = true;
            }
            if (packets[i].wheel != 0) {
                route_wheel(packets[i].wheel);
            }
        }
        // USB HID mouse reports land in the same MsauxPacket/ptr_apply
        // pipeline as PS/2 above: usb_poll() pumps the xHCI event ring
        // (and feeds USB keyboard events through the scancode path), then
        // usb_mouse_poll() hands over the queued packets.
        usb::usb_poll();
        const hcpp::u32 usb_n = usb::usb_mouse_poll(packets, 16);
        for (hcpp::u32 i = 0; i < usb_n; ++i) {
            ptr_apply(packets[i]);
            if (packets[i].dx != 0 || packets[i].dy != 0) {
                moved = true;
            }
            if (packets[i].wheel != 0) {
                route_wheel(packets[i].wheel);
            }
        }
        route_mode();
        const hcpp::u8 after = ptr_buttons();
        if (before != after) {
            route_buttons(before, after, ptr_x(), ptr_y());
        }
        // Motion events keep the WM drag engine and caption-button hot
        // tracking alive; the press/release edges still route through
        // route_buttons above.
        if (moved && !hype::console::fbcon_active() &&
            hype::session::session_logged_in() &&
            hype::win::wm_handle_move(ptr_x(), ptr_y())) {
            ptr_mark_dirty();
        }
        return ptr_take_dirty();
    }

    hcpp::u32 cursor_draw(hcpp::graphics::Framebuffer &fb) noexcept {
        if (!fb.valid()) {
            return 0;
        }
        return cursprite_draw(fb, ptr_x(), ptr_y());
    }

    hcpp::u32 cursor_erase(hcpp::graphics::Framebuffer &fb) noexcept {
        if (!fb.valid()) {
            return 0;
        }
        return cursprite_erase(fb);
    }

    CurStat cursor_stat() noexcept { return ptr_stat(); }

    bool input_selftest(hcpp::graphics::Framebuffer &fb) noexcept {
        bool ok = true;
        MsauxPacket packet{};
        const bool pkt_ok = msaux_parse(0x29, 0x05, 0xFD, packet) &&
                            packet.buttons == 1 && packet.dx == 5 && packet.dy == 3 &&
                            msaux_parse(0x19, 0xFB, 0x03, packet) && packet.dx == -5 &&
                            packet.dy == -3 && !msaux_parse(0x01, 0x05, 0xFD, packet);
        serial::write("ptr pkt=");
        serial::write_line(pkt_ok ? "ok" : "fail");
        ok = ok && pkt_ok;

        const CurStat saved = ptr_stat();
        ptr_set(saved.x, saved.y);
        const hcpp::i32 x0 = ptr_x();
        const hcpp::i32 y0 = ptr_y();
        ptr_apply_motion(10, 6, 1);
        const bool move_ok = ptr_x() == x0 + 10 && ptr_y() == y0 + 6 && ptr_buttons() == 1;
        ptr_apply_motion(1 << 20, 1 << 20, 0);
        const CurStat clamped = ptr_stat();
        const bool clamp_ok = ptr_x() >= 0 && ptr_y() >= 0 && clamped.x >= 0 && clamped.y >= 0;
        ptr_apply_motion(-(1 << 20), -(1 << 20), 0);
        const bool clamp_low_ok = ptr_x() == 0 && ptr_y() == 0;
        ptr_set(saved.x, saved.y);
        ptr_apply_motion(0, 0, saved.buttons);
        serial::write("ptr pos=");
        serial::write_dec32(static_cast<hcpp::u32>(ptr_x()));
        serial::write_char(',');
        serial::write_dec32(static_cast<hcpp::u32>(ptr_y()));
        serial::write("\r\n");
        ok = ok && move_ok && clamp_ok && clamp_low_ok;

        hcpp::u32 painted = cursprite_opaque_count();
        if (fb.valid()) {
            painted = cursor_draw(fb);
        }
        const bool draw_ok = painted != 0;
        serial::write("cursor draw=");
        serial::write_line(draw_ok ? "ok" : "fail");
        ok = ok && draw_ok;
        return ok;
    }

}
