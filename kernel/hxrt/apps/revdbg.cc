#include "kernel/hxrt/apps/builtins.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/hxrt/hxrt.h"
#include "hcpp/text_writer.hpp"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/svc/revsvc.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::hxrt {

    namespace {

        using hype::svc::SvcState;
        using hype::ui::Point;
        using hype::ui::Rect;

        constexpr hcpp::size_t dbg_none = static_cast<hcpp::size_t>(-1);
        constexpr hcpp::u32 dbg_log_max = 10;
        constexpr hcpp::i32 dbg_pad = 12;
        constexpr hcpp::i32 dbg_list_w = 176;
        constexpr hcpp::i32 dbg_row_h = 22;
        constexpr hcpp::i32 dbg_log_line_h = 15;
        constexpr hcpp::i32 dbg_btn_w = 76;
        constexpr hcpp::i32 dbg_btn_h = 24;
        constexpr hcpp::i32 dbg_btn_gap = 8;

        hcpp::size_t dbg_sel = dbg_none;
        hcpp::u32 dbg_rows = 0;
        hcpp::u32 dbg_logs = 0;
        Rect dbg_row_rects[hype::svc::svc_max_services];
        Rect dbg_btn_rects[3];
        bool dbg_layout = false;

        bool name_equals(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        hcpp::u32 state_rgb(SvcState state, const hype::ui::Theme& theme) noexcept {
            switch (state) {
                case SvcState::Running:
                    return theme.success;
                case SvcState::Failed:
                    return theme.destructive;
                case SvcState::Starting:
                case SvcState::Stopping:
                    return theme.warning;
                case SvcState::Stopped:
                    return theme.muted_fg;
            }
            return theme.muted_fg;
        }

        bool can_restart(SvcState state) noexcept {
            return state == SvcState::Running;
        }

        bool can_stop(SvcState state) noexcept {
            return state == SvcState::Running || state == SvcState::Starting;
        }

        bool can_start(SvcState state) noexcept {
            return state == SvcState::Stopped || state == SvcState::Failed;
        }

        void paint_button(hype::ui::Canvas& canvas, const Rect& rect,
                          const char* label, bool enabled,
                          const hype::ui::Theme& theme) noexcept {
            const hcpp::u32 fill =
                enabled ? theme.secondary : hype::ui::rui_shade(theme.muted, 30);
            const hcpp::u32 edge = enabled ? theme.border : theme.muted;
            hype::ui::rui_control_bg(canvas, rect, fill, edge);
            canvas.draw_text(rect.x + 10, rect.y + 16, label,
                             canvas.color(enabled ? theme.fg : theme.muted_fg));
        }

        void paint_list(hype::ui::Canvas& canvas, const Rect& box,
                        const hype::ui::Theme& theme) noexcept {
            const hype::svc::RevService* list = hype::svc::svc_list();
            const hcpp::size_t count = hype::svc::svc_count();
            const Rect pane{box.x + dbg_pad, box.y + dbg_pad, dbg_list_w,
                            box.h - 2 * dbg_pad};
            canvas.fill_rect(pane, canvas.color(hype::ui::rui_shade(theme.card, 10)));
            canvas.stroke_rect(pane, canvas.color(theme.border));
            const hcpp::i32 room = (pane.h - 8) / dbg_row_h;
            hcpp::u32 shown = 0;
            for (hcpp::size_t i = 0; i < count && i < hype::svc::svc_max_services;
                 ++i) {
                if (static_cast<hcpp::i32>(shown) >= room) {
                    break;
                }
                const Rect row{pane.x + 1, pane.y + 4 + static_cast<hcpp::i32>(i) * dbg_row_h,
                               pane.w - 2, dbg_row_h};
                dbg_row_rects[i] = row;
                if (i == dbg_sel) {
                    canvas.fill_rect(row, canvas.color(hype::ui::rui_lift(theme.accent, 1)));
                }
                const hcpp::u32 badge = state_rgb(list[i].state, theme);
                canvas.fill_circle(row.x + 10, row.y + row.h / 2, 4,
                                   canvas.color(badge));
                canvas.draw_text(row.x + 22, row.y + 15, list[i].name,
                                 canvas.color(theme.card_fg));
                canvas.draw_text(row.right() - 58, row.y + 15,
                                 hype::svc::svc_state_name(list[i].state),
                                 canvas.color(badge));
                ++shown;
            }
            dbg_rows = shown;
        }

        void paint_detail(hype::ui::Canvas& canvas, const Rect& box,
                          const hype::ui::Theme& theme) noexcept {
            const Rect pane{box.x + dbg_pad + dbg_list_w + dbg_pad, box.y + dbg_pad,
                            box.w - dbg_list_w - 3 * dbg_pad, box.h - 2 * dbg_pad};
            canvas.draw_line(pane.x - dbg_pad / 2, pane.y, pane.x - dbg_pad / 2,
                             pane.bottom(), canvas.color(theme.border));
            dbg_btn_rects[0] = Rect{};
            dbg_btn_rects[1] = Rect{};
            dbg_btn_rects[2] = Rect{};
            dbg_logs = 0;
            if (dbg_sel == dbg_none || dbg_sel >= hype::svc::svc_count()) {
                canvas.draw_text(pane.x + 8, pane.y + 24,
                                 "select a service", canvas.color(theme.muted_fg));
                return;
            }
            const hype::svc::RevService& svc = hype::svc::svc_list()[dbg_sel];
            const hcpp::u32 badge = state_rgb(svc.state, theme);
            hcpp::i32 y = pane.y + 8;
            canvas.draw_text(pane.x + 8, y + 15, svc.name, canvas.color(theme.accent));
            y += 24;
            canvas.fill_circle(pane.x + 12, y + 11, 4, canvas.color(badge));
            canvas.draw_text(pane.x + 24, y + 15, hype::svc::svc_state_name(svc.state),
                             canvas.color(badge));
            y += 22;
            hcpp::TextWriter line;
            line.append("uptime ");
            if (svc.state == SvcState::Running) {
                const hcpp::u64 now = hype::interrupts::pit_ticks();
                const hcpp::u64 up = now > svc.start_ticks ? now - svc.start_ticks : 0;
                line.append_dec32(static_cast<hcpp::u32>(up));
            } else {
                line.append('0');
            }
            line.append(" ticks");
            canvas.draw_text(pane.x + 8, y + 15, line.view(), canvas.color(theme.muted_fg));
            y += 26;
            dbg_btn_rects[0] = Rect{pane.x + 8, y, dbg_btn_w, dbg_btn_h};
            dbg_btn_rects[1] = Rect{pane.x + 8 + dbg_btn_w + dbg_btn_gap, y,
                                    dbg_btn_w, dbg_btn_h};
            dbg_btn_rects[2] = Rect{pane.x + 8 + 2 * (dbg_btn_w + dbg_btn_gap), y,
                                    dbg_btn_w, dbg_btn_h};
            paint_button(canvas, dbg_btn_rects[0], "Restart",
                         can_restart(svc.state), theme);
            paint_button(canvas, dbg_btn_rects[1], "Stop", can_stop(svc.state), theme);
            paint_button(canvas, dbg_btn_rects[2], "Start", can_start(svc.state), theme);
            y += dbg_btn_h + dbg_pad;
            hcpp::i32 log_h = pane.bottom() - y;
            const hcpp::i32 want =
                static_cast<hcpp::i32>(dbg_log_max) * dbg_log_line_h + 10;
            if (log_h > want) {
                log_h = want;
            }
            const Rect logbox{pane.x + 8, y, pane.w - 16, log_h};
            if (logbox.empty()) {
                return;
            }
            canvas.fill_rect(logbox, canvas.color(hype::ui::rui_shade(theme.bg, 4)));
            canvas.stroke_rect(logbox, canvas.color(theme.border));
            const hcpp::u32 total = hype::svc::svc_log_count(svc.id);
            const hcpp::u32 first = total > dbg_log_max ? total - dbg_log_max : 0;
            const hcpp::u32 room =
                static_cast<hcpp::u32>((logbox.h - 8) / dbg_log_line_h);
            hcpp::u32 drawn = 0;
            for (hcpp::u32 k = 0; k < dbg_log_max && k < room; ++k) {
                const char* text = hype::svc::svc_log_read(svc.id, first + k);
                if (text == nullptr) {
                    break;
                }
                canvas.draw_text(logbox.x + 6, logbox.y + 6 + static_cast<hcpp::i32>(k) * dbg_log_line_h + 11,
                                 text, canvas.color(theme.muted_fg));
                ++drawn;
            }
            dbg_logs = drawn;
        }

        void revdbg_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            dbg_refresh();
            hype::ui::Canvas canvas(*ctx.fb);
            canvas.set_clip(ctx.paint_clip());
            const hype::ui::Theme& theme = hype::ui::theme_active();
            hype::ui::rui_panel(canvas, ctx.region, hype::ui::rui_elev_window);
            paint_list(canvas, ctx.region, theme);
            paint_detail(canvas, ctx.region, theme);
            dbg_layout = true;
        }

        void revdbg_close(AppCtx& ctx) noexcept {
            (void)ctx;
            dbg_sel = dbg_none;
            dbg_layout = false;
        }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "RevDBGService", "HYPEOS://S/apps/RevDBGService.hxersys",
            revdbg_entry, revdbg_close};

    }

    const HxerHeader* revdbg_app() noexcept { return &desc; }

    hcpp::size_t dbg_refresh() noexcept {
        const hcpp::size_t count = hype::svc::svc_count();
        if (dbg_sel != dbg_none && dbg_sel >= count) {
            dbg_sel = dbg_none;
        }
        return count;
    }

    bool dbg_select(hcpp::size_t index) noexcept {
        if (index >= hype::svc::svc_count()) {
            return false;
        }
        dbg_sel = index;
        return true;
    }

    hcpp::size_t dbg_selected() noexcept { return dbg_sel; }

    bool dbg_restart() noexcept {
        if (dbg_sel == dbg_none || dbg_sel >= hype::svc::svc_count()) {
            return false;
        }
        const hype::svc::RevService& svc = hype::svc::svc_list()[dbg_sel];
        if (!can_restart(svc.state)) {
            return false;
        }
        return hype::svc::svc_restart(svc.name);
    }

    bool dbg_stop() noexcept {
        if (dbg_sel == dbg_none || dbg_sel >= hype::svc::svc_count()) {
            return false;
        }
        const hype::svc::RevService& svc = hype::svc::svc_list()[dbg_sel];
        if (!can_stop(svc.state)) {
            return false;
        }
        return hype::svc::svc_stop(svc.name);
    }

    bool dbg_start() noexcept {
        if (dbg_sel == dbg_none || dbg_sel >= hype::svc::svc_count()) {
            return false;
        }
        const hype::svc::RevService& svc = hype::svc::svc_list()[dbg_sel];
        if (!can_start(svc.state)) {
            return false;
        }
        return hype::svc::svc_start(svc.name);
    }

    bool dbg_click(hcpp::i32 x, hcpp::i32 y) noexcept {
        if (!dbg_layout) {
            return false;
        }
        const Point p{x, y};
        for (hcpp::u32 i = 0; i < dbg_rows; ++i) {
            if (dbg_row_rects[i].contains(p)) {
                return dbg_select(i);
            }
        }
        if (dbg_sel == dbg_none || dbg_sel >= hype::svc::svc_count()) {
            return false;
        }
        if (dbg_btn_rects[0].contains(p)) {
            return dbg_restart();
        }
        if (dbg_btn_rects[1].contains(p)) {
            return dbg_stop();
        }
        if (dbg_btn_rects[2].contains(p)) {
            return dbg_start();
        }
        return false;
    }

    bool revdbg_selftest() noexcept {
        static hcpp::u8 pixels[480 * 320 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 480, 320, 480 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        AppCtx ctx{};
        ctx.fb = &target;
        ctx.region = hype::ui::Rect{0, 0, 480, 320};
        const bool open_ok = target.valid() &&
                             hxrt_exec("RevDBGService", ctx) && dbg_layout;
        serial::write("revdbg open=");
        serial::write_line(open_ok ? "ok" : "fail");
        serial::write("revdbg list=");
        serial::write_dec32(dbg_rows);
        serial::write("\r\n");
        const hcpp::size_t count = hype::svc::svc_count();
        hcpp::size_t tidx = dbg_none;
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (name_equals(hype::svc::svc_list()[i].name, "svctst")) {
                tidx = i;
                break;
            }
        }
        if (tidx == dbg_none && count > 0) {
            tidx = 0;
        }
        bool ctl_ok = tidx != dbg_none && dbg_select(tidx);
        if (ctl_ok) {
            const hype::ui::Rect row = dbg_row_rects[tidx];
            ctl_ok = dbg_click(row.x + 6, row.y + row.h / 2) &&
                     dbg_sel == tidx;
        }
        const char* tname =
            tidx != dbg_none ? hype::svc::svc_list()[tidx].name : nullptr;
        if (ctl_ok && tname != nullptr) {
            const bool was_running =
                hype::svc::svc_state(tname) == hype::svc::SvcState::Running;
            ctl_ok = was_running && dbg_restart() &&
                     hype::svc::svc_state(tname) == hype::svc::SvcState::Running;
            ctl_ok = ctl_ok && dbg_stop() &&
                     hype::svc::svc_state(tname) == hype::svc::SvcState::Stopped;
            ctl_ok = ctl_ok && dbg_start() &&
                     hype::svc::svc_state(tname) == hype::svc::SvcState::Running;
        }
        serial::write("revdbg ctl=");
        serial::write_line(ctl_ok ? "ok" : "fail");
        dbg_logs = 0;
        hxrt_exec("RevDBGService", ctx);
        const hcpp::u32 tid =
            tname != nullptr ? hype::svc::svc_id(tname) : hype::svc::svc_invalid_id;
        const bool log_ok = ctl_ok && tid != hype::svc::svc_invalid_id &&
                            hype::svc::svc_log_count(tid) > 0 && dbg_logs > 0;
        serial::write("revdbg log=");
        serial::write_line(log_ok ? "ok" : "fail");
        return open_ok && dbg_rows == count && count > 0 && ctl_ok && log_ok;
    }

}
