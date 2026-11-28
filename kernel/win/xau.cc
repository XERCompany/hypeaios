// XAU — HypeAI OS windowing API (Windows-style layer over winman).
//
// XAU handles pack (generation << 8) | wm_window_id. Each of the
// wm_max_windows wm slots has a parallel XauRec carrying the generation
// counter, bound WndProc and lifecycle flags. The wm calls into this
// file through two hooks installed by xau_ensure():
//   - paint hook: per-window content paint during wm_paint -> PAINT msg
//   - event hook: close/geometry/mouse/min-max/focus -> XAU_MSG_*
// Keyboard delivery uses the shared input::subscribe_keys bus, so no
// extra wm plumbing is needed for KEYDOWN/KEYUP/CHAR.

#include "kernel/win/xau.h"

#include "kernel/functionemitkeys.h"
#include "kernel/input/scancode.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"
#include "kernel/win/winman.h"

namespace hype::win {

    namespace {

        using hype::ui::Point;
        using hype::ui::Rect;

        struct XauClass {
            bool used = false;
            char name[24]{};
            XauWndProc proc = nullptr;
            hcpp::u32 style = 0;
            hcpp::u32 bg = 0;
        };

        struct XauRec {
            hcpp::u32 gen = 0;        // bumped on every bind; stale handles fail
            bool bound = false;
            bool hidden = false;
            bool close_sent = false;  // XAU_MSG_CLOSE already delivered
            bool in_wm_close = false; // wm_close is unwinding this window
            bool paint_seen = false;  // "xau msg=PAINT" marker once per bind
            XauWndProc proc = nullptr;
            hcpp::u32 class_idx = 0xFFFFFFFFu;
            char title[24]{};
        };

        struct XauPost {
            XAU xau_win = XAU_INVALID;
            hcpp::u32 msg = 0;
            hcpp::u64 wparam = 0;
            hcpp::u64 lparam = 0;
        };

        struct XauTimer {
            bool used = false;
            XAU xau_win = XAU_INVALID;
            hcpp::u32 timer_id = 0;
            hcpp::u64 period = 0;
            hcpp::u64 next = 0;
        };

        XauClass classes[xau_max_classes];
        hcpp::u32 class_count = 0;
        XauRec recs[wm_max_windows];
        XauPost post_q[xau_post_capacity];
        hcpp::u32 post_head = 0;
        hcpp::u32 post_tail = 0;
        XauTimer timers[xau_max_timers];
        hcpp::u32 synth_counter = 0;
        bool api_ready = false;
        bool api_marked = false;

        void copy_text(char* dst, const char* src, hcpp::u32 cap) noexcept {
            hcpp::u32 i = 0;
            if (src == nullptr) {
                dst[0] = '\0';
                return;
            }
            while (i + 1 < cap && src[i] != '\0') {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = '\0';
        }

        bool text_equals(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        hcpp::u32 find_class(const char* name) noexcept {
            if (name == nullptr || name[0] == '\0') {
                return 0xFFFFFFFFu;
            }
            for (hcpp::u32 i = 0; i < xau_max_classes; ++i) {
                if (classes[i].used && text_equals(classes[i].name, name)) {
                    return i;
                }
            }
            return 0xFFFFFFFFu;
        }

        XAU make_handle(hcpp::u32 wm_id) noexcept {
            if (wm_id == 0 || wm_id > wm_max_windows) {
                return XAU_INVALID;
            }
            const XauRec& r = recs[wm_id - 1];
            if (!r.bound || r.gen == 0) {
                return XAU_INVALID;
            }
            return (r.gen << 8) | wm_id;
        }

        hcpp::u32 handle_wm_id(XAU xau_win) noexcept {
            const hcpp::u32 id = xau_win & 0xFFu;
            if (id == 0 || id > wm_max_windows) {
                return 0;
            }
            const XauRec& r = recs[id - 1];
            if (!r.bound || r.gen == 0 || (xau_win >> 8) != r.gen) {
                return 0;
            }
            return id;
        }

        hcpp::i32 dispatch(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                           hcpp::u64 lparam) noexcept {
            const hcpp::u32 id = handle_wm_id(xau_win);
            if (id == 0) {
                return 0;
            }
            const XauRec& r = recs[id - 1];
            if (r.proc != nullptr) {
                return r.proc(xau_win, msg, wparam, lparam);
            }
            return xau_defproc(xau_win, msg, wparam, lparam);
        }

        // Binds a wm slot to a WndProc and delivers XAU_MSG_CREATE.
        XAU bind(hcpp::u32 wm_id, XauWndProc proc, hcpp::u32 class_idx,
                 const char* title) noexcept {
            if (wm_id == 0 || wm_id > wm_max_windows || proc == nullptr) {
                return XAU_INVALID;
            }
            XauRec& r = recs[wm_id - 1];
            if (r.bound) {
                return make_handle(wm_id);
            }
            r.gen += 1;
            if (r.gen == 0) {
                r.gen = 1;
            }
            r.bound = true;
            r.hidden = false;
            r.close_sent = false;
            r.in_wm_close = false;
            r.paint_seen = false;
            r.proc = proc;
            r.class_idx = class_idx;
            copy_text(r.title, title, 24);
            const XAU xau_win = make_handle(wm_id);
            serial::write("xau create xau_win=");
            serial::write_dec32(xau_win);
            serial::write("\r\n");
            serial::write("xau app=");
            serial::write(title != nullptr ? title : "?");
            serial::write_line(" ok");
            dispatch(xau_win, XAU_MSG_CREATE, 0, 0);
            const Win* w = wm_window(wm_id);
            if (w != nullptr) {
                wm_invalidate(w->frame);
            }
            return xau_win;
        }

        void unbind(hcpp::u32 wm_id, XAU xau_win) noexcept {
            XauRec& r = recs[wm_id - 1];
            if (!r.bound) {
                return;
            }
            r.bound = false;
            r.proc = nullptr;
            r.close_sent = false;
            r.in_wm_close = false;
            r.paint_seen = false;
            serial::write("xau destroy xau_win=");
            serial::write_dec32(xau_win);
            serial::write("\r\n");
        }

        // Adopt a plain wm window whose app name matches a registered
        // XAU class — this is what makes .hxer apps port to WndProc
        // without changing their launch path.
        XAU adopt(hcpp::u32 wm_id, const char* app_name) noexcept {
            const hcpp::u32 ci = find_class(app_name);
            if (ci == 0xFFFFFFFFu) {
                return XAU_INVALID;
            }
            return bind(wm_id, classes[ci].proc, ci, classes[ci].name);
        }

        // Resolves a wm window id to an XAU handle, adopting lazily.
        XAU resolve(hcpp::u32 wm_id) noexcept {
            XAU xau_win = make_handle(wm_id);
            if (xau_win != XAU_INVALID) {
                return xau_win;
            }
            const Win* w = wm_window(wm_id);
            if (w == nullptr) {
                return XAU_INVALID;
            }
            return adopt(wm_id, w->app_name);
        }

        hcpp::u64 pack_xy(hcpp::i32 x, hcpp::i32 y) noexcept {
            return (static_cast<hcpp::u64>(static_cast<hcpp::u16>(y)) << 16) |
                   static_cast<hcpp::u16>(x);
        }

        // lparam for pointer messages: client-space (y<<16)|x like Win32.
        hcpp::u64 pack_client(hcpp::u32 wm_id, hcpp::i32 x,
                              hcpp::i32 y) noexcept {
            const Win* w = wm_window(wm_id);
            if (w == nullptr) {
                return pack_xy(x, y);
            }
            return pack_xy(x - w->content_rect.x, y - w->content_rect.y);
        }

        // ---- wm -> xau hooks ---------------------------------------------

        bool xau_wm_paint(hcpp::u32 wm_id, hype::hxrt::AppCtx& ctx) noexcept {
            const XAU xau_win = resolve(wm_id);
            if (xau_win == XAU_INVALID) {
                return false;
            }
            XauRec& r = recs[wm_id - 1];
            if (r.hidden) {
                return true;
            }
            if (!r.paint_seen) {
                r.paint_seen = true;
                serial::write_line("xau msg=PAINT");
            }
            const XauDC dc{ctx.fb, ctx.region, ctx.paint_clip()};
            dispatch(xau_win, XAU_MSG_PAINT, 0,
                     reinterpret_cast<hcpp::u64>(&dc));
            return true;
        }

        bool xau_wm_event(hcpp::u32 wm_id, hcpp::u32 ev, hcpp::i32 a,
                          hcpp::i32 b) noexcept {
            if (ev == wm_xau_ev_close) {
                // Fires inside wm_close: deliver CLOSE once, then DESTROY
                // and drop the binding — the wm slot is going away either
                // way, so a WndProc that vetoes close only loses its hook.
                const XAU xau_win = make_handle(wm_id);
                if (xau_win == XAU_INVALID) {
                    return false;
                }
                XauRec& r = recs[wm_id - 1];
                r.in_wm_close = true;
                if (!r.close_sent) {
                    r.close_sent = true;
                    dispatch(xau_win, XAU_MSG_CLOSE, 0, 0);
                }
                if (r.bound) {
                    dispatch(xau_win, XAU_MSG_DESTROY, 0, 0);
                    unbind(wm_id, xau_win);
                }
                r.in_wm_close = false;
                return true;
            }
            const XAU xau_win = resolve(wm_id);
            if (xau_win == XAU_INVALID) {
                return false;
            }
            switch (ev) {
                case wm_xau_ev_size:
                    dispatch(xau_win, XAU_MSG_SIZE, 0, pack_xy(a, b));
                    return true;
                case wm_xau_ev_move:
                    dispatch(xau_win, XAU_MSG_MOVE, 0, pack_xy(a, b));
                    return true;
                case wm_xau_ev_lbuttondown:
                    dispatch(xau_win, XAU_MSG_LBUTTONDOWN, 0,
                             pack_client(wm_id, a, b));
                    // The WndProc may have destroyed its own window.
                    if (const Win* w = wm_window(wm_id)) {
                        wm_invalidate(w->frame);
                    }
                    return true;
                case wm_xau_ev_lbuttonup:
                    dispatch(xau_win, XAU_MSG_LBUTTONUP, 0,
                             pack_client(wm_id, a, b));
                    return true;
                case wm_xau_ev_rbuttondown:
                    dispatch(xau_win, XAU_MSG_RBUTTONDOWN, 0,
                             pack_client(wm_id, a, b));
                    return true;
                case wm_xau_ev_mousemove:
                    dispatch(xau_win, XAU_MSG_MOUSEMOVE, 0,
                             pack_client(wm_id, a, b));
                    return false;
                case wm_xau_ev_minimize:
                    recs[wm_id - 1].hidden = a != 0;
                    dispatch(xau_win, XAU_MSG_MINIMIZE,
                             static_cast<hcpp::u64>(a), 0);
                    return true;
                case wm_xau_ev_maximize:
                    dispatch(xau_win, XAU_MSG_MAXIMIZE,
                             static_cast<hcpp::u64>(a), 0);
                    return true;
                case wm_xau_ev_focus:
                    dispatch(xau_win, XAU_MSG_FOCUS,
                             static_cast<hcpp::u64>(a), 0);
                    return true;
                default:
                    return false;
            }
        }

        void xau_key_event(void*, const input::InputEvent& e) noexcept {
            if (e.type != input::InputEventType::Key) {
                return;
            }
            if (hype::session::session_locked()) {
                return;
            }
            const Win* focused = wm_focused();
            if (focused == nullptr) {
                return;
            }
            const XAU xau_win = resolve(focused->id);
            if (xau_win == XAU_INVALID) {
                return;
            }
            const bool pressed =
                e.value !=
                static_cast<hcpp::u32>(input::KeyState::Released);
            const char ascii = pressed ? input::scancode_last_ascii() : '\0';
            dispatch(xau_win,
                     pressed ? XAU_MSG_KEYDOWN : XAU_MSG_KEYUP,
                     static_cast<hcpp::u64>(e.code),
                     static_cast<hcpp::u64>(static_cast<hcpp::u8>(ascii)));
            // Ctrl/Alt/Win chords are commands, not text: suppress the
            // CHAR so Ctrl+C reaches the app as COPY without also
            // inserting a literal 'c'. (ctrl=29, alt=56, gui=91/92.)
            const bool chord = input::scancode_key_pressed(29) ||
                               input::scancode_key_pressed(56) ||
                               input::scancode_key_pressed(91) ||
                               input::scancode_key_pressed(92);
            if (pressed && !chord && ascii >= 32 && ascii < 127) {
                dispatch(xau_win, XAU_MSG_CHAR,
                         static_cast<hcpp::u64>(
                             static_cast<hcpp::u8>(ascii)),
                         static_cast<hcpp::u64>(e.code));
            }
        }

        void api_mark() noexcept {
            if (api_marked) {
                return;
            }
            api_marked = true;
            serial::write("xau api=ok classes=");
            serial::write_dec32(class_count);
            serial::write("\r\n");
        }

        void xau_ensure() noexcept {
            if (api_ready) {
                return;
            }
            api_ready = true;
            wm_set_xau_hooks(&xau_wm_paint, &xau_wm_event);
            input::subscribe_keys(&xau_key_event, nullptr);
        }

    }

    bool xau_register_class(const char* name, XauWndProc proc,
                            hcpp::u32 style, hcpp::u32 bg) noexcept {
        xau_ensure();
        if (name == nullptr || name[0] == '\0' || proc == nullptr) {
            return false;
        }
        const hcpp::u32 dup = find_class(name);
        if (dup != 0xFFFFFFFFu) {
            classes[dup].proc = proc;
            classes[dup].style = style;
            classes[dup].bg = bg;
            api_mark();
            return true;
        }
        hcpp::u32 slot = xau_max_classes;
        for (hcpp::u32 i = 0; i < xau_max_classes; ++i) {
            if (!classes[i].used) {
                slot = i;
                break;
            }
        }
        if (slot >= xau_max_classes) {
            return false;
        }
        XauClass& c = classes[slot];
        c.used = true;
        copy_text(c.name, name, 24);
        c.proc = proc;
        c.style = style;
        c.bg = bg;
        ++class_count;
        api_mark();
        return true;
    }

    XAU xau_create(const char* class_name, const char* title, hcpp::i32 x,
                   hcpp::i32 y, hcpp::i32 w, hcpp::i32 h, hcpp::u32 flags,
                   XauWndProc proc) noexcept {
        xau_ensure();
        api_mark();
        XauWndProc use = proc;
        hcpp::u32 ci = 0xFFFFFFFFu;
        if (class_name != nullptr && class_name[0] != '\0') {
            ci = find_class(class_name);
            if (use == nullptr && ci != 0xFFFFFFFFu) {
                use = classes[ci].proc;
            }
        }
        if (use == nullptr) {
            return XAU_INVALID;
        }
        // Every xau_create gets a unique synthetic wm app name so wm_open
        // cannot dedup two XAU windows into one; the real title goes to
        // the wm caption field instead.
        char wm_name[24] = "xauw";
        hcpp::u32 n = ++synth_counter;
        hcpp::u32 pos = 4;
        char digits[10];
        hcpp::u32 dn = 0;
        if (n == 0) {
            digits[dn++] = '0';
        }
        while (n != 0 && dn < 10) {
            digits[dn++] = static_cast<char>('0' + (n % 10));
            n /= 10;
        }
        while (dn != 0 && pos < 23) {
            wm_name[pos++] = digits[--dn];
        }
        wm_name[pos] = '\0';
        const Rect frame =
            (w > 0 && h > 0) ? Rect{x, y, w, h} : Rect{};
        const hcpp::u32 wm_id = wm_open(wm_name, frame);
        if (wm_id == 0) {
            return XAU_INVALID;
        }
        const char* shown =
            (title != nullptr && title[0] != '\0') ? title : wm_name;
        wm_set_caption(wm_id, shown);
        const XAU xau_win = bind(wm_id, use, ci, shown);
        if (xau_win == XAU_INVALID) {
            wm_close(wm_id);
            return XAU_INVALID;
        }
        if ((flags & XAU_WS_MINIMIZED) != 0) {
            wm_set_minimized(wm_id, true);
        } else if ((flags & XAU_WS_MAXIMIZED) != 0) {
            wm_set_maximized(wm_id, true);
        }
        return xau_win;
    }

    bool xau_destroy(XAU xau_win) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        XauRec& r = recs[wm_id - 1];
        if (r.in_wm_close) {
            // Already unwinding inside wm_close (XAU_MSG_CLOSE default
            // path): the close handler finishes the DESTROY + unbind.
            return true;
        }
        r.close_sent = true; // DestroyWindow semantics: no CLOSE msg.
        if (wm_window(wm_id) != nullptr) {
            return wm_close(wm_id);
        }
        unbind(wm_id, xau_win);
        return true;
    }

    bool xau_show(XAU xau_win) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        recs[wm_id - 1].hidden = false;
        bool ok = wm_set_minimized(wm_id, false);
        ok = wm_focus(wm_id) && ok;
        const Win* w = wm_window(wm_id);
        if (w != nullptr) {
            wm_invalidate(w->frame);
        }
        return ok;
    }

    bool xau_hide(XAU xau_win) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        recs[wm_id - 1].hidden = true;
        return wm_set_minimized(wm_id, true);
    }

    bool xau_move(XAU xau_win, hcpp::i32 x, hcpp::i32 y) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        const Win* w = wm_window(wm_id);
        if (w == nullptr) {
            return false;
        }
        return wm_set_frame(wm_id, Rect{x, y, w->frame.w, w->frame.h});
    }

    bool xau_get_rect(XAU xau_win, Rect& out) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        const Win* w = wm_window(wm_id);
        if (w == nullptr) {
            return false;
        }
        out = w->frame;
        return true;
    }

    bool xau_set_rect(XAU xau_win, const Rect& frame) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0 || frame.empty()) {
            return false;
        }
        return wm_set_frame(wm_id, frame);
    }

    bool xau_invalidate(XAU xau_win, const Rect* rect) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        const Win* w = wm_window(wm_id);
        if (w == nullptr) {
            return false;
        }
        if (rect == nullptr || rect->empty()) {
            wm_invalidate(w->frame);
            return true;
        }
        // Client-space rect -> screen space.
        wm_invalidate(Rect{w->content_rect.x + rect->x,
                           w->content_rect.y + rect->y, rect->w, rect->h});
        return true;
    }

    bool xau_set_title(XAU xau_win, const char* title) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0 || title == nullptr) {
            return false;
        }
        wm_set_caption(wm_id, title);
        copy_text(recs[wm_id - 1].title, title, 24);
        return true;
    }

    bool xau_focus(XAU xau_win) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        return wm_focus(wm_id);
    }

    bool xau_client_rect(XAU xau_win, Rect& out) noexcept {
        const hcpp::u32 wm_id = handle_wm_id(xau_win);
        if (wm_id == 0) {
            return false;
        }
        const Win* w = wm_window(wm_id);
        if (w == nullptr) {
            return false;
        }
        out = w->content_rect;
        return true;
    }

    bool xau_valid(XAU xau_win) noexcept { return handle_wm_id(xau_win) != 0; }

    XAU xau_focused() noexcept {
        const Win* w = wm_focused();
        if (w == nullptr) {
            return XAU_INVALID;
        }
        return make_handle(w->id);
    }

    hcpp::i32 xau_send(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                       hcpp::u64 lparam) noexcept {
        return dispatch(xau_win, msg, wparam, lparam);
    }

    bool xau_post(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                  hcpp::u64 lparam) noexcept {
        const hcpp::u32 next = (post_tail + 1) % xau_post_capacity;
        if (next == post_head) {
            return false;
        }
        post_q[post_tail] = XauPost{xau_win, msg, wparam, lparam};
        post_tail = next;
        return true;
    }

    bool xau_set_timer(XAU xau_win, hcpp::u32 timer_id,
                       hcpp::u64 ticks) noexcept {
        if (handle_wm_id(xau_win) == 0 || ticks == 0) {
            return false;
        }
        for (hcpp::u32 i = 0; i < xau_max_timers; ++i) {
            if (timers[i].used && timers[i].xau_win == xau_win &&
                timers[i].timer_id == timer_id) {
                timers[i].period = ticks;
                timers[i].next = hype::interrupts::pit_ticks() + ticks;
                return true;
            }
        }
        for (hcpp::u32 i = 0; i < xau_max_timers; ++i) {
            if (!timers[i].used) {
                timers[i] = XauTimer{true, xau_win, timer_id, ticks,
                                     hype::interrupts::pit_ticks() + ticks};
                return true;
            }
        }
        return false;
    }

    bool xau_kill_timer(XAU xau_win, hcpp::u32 timer_id) noexcept {
        for (hcpp::u32 i = 0; i < xau_max_timers; ++i) {
            if (timers[i].used && timers[i].xau_win == xau_win &&
                timers[i].timer_id == timer_id) {
                timers[i] = XauTimer{};
                return true;
            }
        }
        return false;
    }

    void xau_pump() noexcept {
        const hcpp::u64 now = hype::interrupts::pit_ticks();
        for (hcpp::u32 i = 0; i < xau_max_timers; ++i) {
            if (!timers[i].used || now < timers[i].next) {
                continue;
            }
            timers[i].next = now + timers[i].period;
            if (handle_wm_id(timers[i].xau_win) == 0) {
                timers[i] = XauTimer{};
                continue;
            }
            dispatch(timers[i].xau_win, XAU_MSG_TIMER, timers[i].timer_id,
                     0);
        }
        while (post_head != post_tail) {
            const XauPost m = post_q[post_head];
            post_head = (post_head + 1) % xau_post_capacity;
            dispatch(m.xau_win, m.msg, m.wparam, m.lparam);
        }
    }

    hcpp::i32 xau_defproc(XAU xau_win, hcpp::u32 msg, hcpp::u64 wparam,
                          hcpp::u64 lparam) noexcept {
        (void)wparam;
        switch (msg) {
            case XAU_MSG_CLOSE:
                xau_destroy(xau_win);
                return 0;
            case XAU_MSG_PAINT: {
                const XauDC* dc =
                    reinterpret_cast<const XauDC*>(lparam);
                if (dc == nullptr || dc->fb == nullptr ||
                    !dc->fb->valid()) {
                    return 0;
                }
                const hcpp::u32 wm_id = handle_wm_id(xau_win);
                hcpp::u32 bg = hype::ui::theme_active().card;
                if (wm_id != 0 &&
                    recs[wm_id - 1].class_idx != 0xFFFFFFFFu) {
                    const hcpp::u32 cbg =
                        classes[recs[wm_id - 1].class_idx].bg;
                    if (cbg != 0) {
                        bg = cbg;
                    }
                }
                hype::ui::Canvas canvas(*dc->fb);
                canvas.set_clip(dc->clip);
                canvas.fill_rect(dc->client, canvas.color(bg));
                return 0;
            }
            default:
                return 0;
        }
    }

}
