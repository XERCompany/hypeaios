#include "kernel/hxrt/apps/speedstart.h"

#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/apps/builtins.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/uiscale.h"
#include "kernel/win/deskman.h"
#include "kernel/win/winman.h"

namespace hype::hxrt {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Point;
        using hype::ui::Rect;
        using hype::ui::Theme;

        constexpr hcpp::i32 pad = 16;
        constexpr hcpp::i32 search_h = 30;
        constexpr hcpp::i32 grid_gap = 14;
        constexpr hcpp::i32 cell_w = 92;
        constexpr hcpp::i32 cell_h = 100;
        constexpr hcpp::i32 icon_size = 52;

        hcpp::u32 order[hxrt_max_apps];
        hcpp::u32 order_count = 0;
        Rect last_region{};

        char upper(char c) noexcept {
            return c >= 'a' && c <= 'z'
                       ? static_cast<char>(c - 'a' + 'A')
                       : c;
        }

        bool name_less(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0') {
                const char ca = upper(a[i]);
                const char cb = upper(b[i]);
                if (ca != cb) {
                    return ca < cb;
                }
                ++i;
            }
            return a[i] == '\0' && b[i] != '\0';
        }

        bool name_equals(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        bool is_system(const HxerHeader* app) noexcept {
            return app->kind == static_cast<hcpp::u8>(HxerKind::System);
        }

        bool tile_before(hcpp::u32 a, hcpp::u32 b) noexcept {
            const HxerHeader* const* list = hxrt_list();
            const bool as = is_system(list[a]);
            const bool bs = is_system(list[b]);
            if (as != bs) {
                return as;
            }
            return name_less(list[a]->name, list[b]->name);
        }

        void rebuild_order() noexcept {
            const hcpp::u32 n = hxrt_count();
            order_count = 0;
            for (hcpp::u32 i = 0; i < n && i < hxrt_max_apps; ++i) {
                order[order_count++] = i;
            }
            for (hcpp::u32 i = 1; i < order_count; ++i) {
                const hcpp::u32 key = order[i];
                hcpp::u32 j = i;
                while (j > 0 && tile_before(key, order[j - 1])) {
                    order[j] = order[j - 1];
                    --j;
                }
                order[j] = key;
            }
        }

        hcpp::i32 grid_cols() noexcept {
            const hcpp::i32 usable = last_region.w - ui::ui_px(pad) * 2;
            const hcpp::i32 cols = usable / ui::ui_px(cell_w);
            return cols > 0 ? cols : 1;
        }

        void sync_region() noexcept {
            if (!last_region.empty()) {
                return;
            }
            const hcpp::u32 id = hype::win::desk_open_app("SpeedStart");
            const hype::win::Win* w = hype::win::wm_window(id);
            if (w != nullptr) {
                last_region = w->content_rect;
            }
        }

        hcpp::i32 measure_px(hcpp::StringView text,
                             hcpp::u32 px) noexcept {
            if (hype::font::hypefont_ready()) {
                return hype::font::text_measure(text, px);
            }
            return static_cast<hcpp::i32>(text.size()) * 8;
        }

        // Fits `name` into `max_w` px with a trailing U+2026 ellipsis
        // (reskin v2): grid labels must clip to the tile, never collide
        // with the neighbour cell's label.
        hcpp::size_t ellipsize(char* buf, hcpp::size_t cap,
                               const char* name, hcpp::i32 max_w) noexcept {
            hcpp::size_t len = 0;
            while (len + 4 < cap && name[len] != '\0') {
                buf[len] = name[len];
                ++len;
            }
            if (measure_px(hcpp::StringView{buf, len}, 13) <= max_w) {
                return len;
            }
            while (len > 0) {
                --len;
                buf[len] = '\xE2';
                buf[len + 1] = '\x80';
                buf[len + 2] = '\xA6';
                if (measure_px(hcpp::StringView{buf, len + 3}, 13) <=
                    max_w) {
                    return len + 3;
                }
            }
            buf[0] = '\xE2';
            buf[1] = '\x80';
            buf[2] = '\xA6';
            return 3;
        }

        void paint_tile(Canvas& canvas, const Theme& theme, const Rect& tile,
                        const HxerHeader* app) noexcept {
            const Rect icon{tile.x + (tile.w - ui::ui_px(icon_size)) / 2, tile.y + 4,
                            ui::ui_px(icon_size), ui::ui_px(icon_size)};
            hype::ui::rui_rounded_gradient(
                canvas, icon, static_cast<hcpp::i32>(theme.radius_md),
                hype::ui::rui_lift(theme.primary, 1), theme.primary);
            hype::gfx::gfx_inner_glow_edge(
                canvas, icon, static_cast<hcpp::i32>(theme.radius_md),
                theme.ring);
            char mono[2] = {upper(app->name[0]),
                            app->name[1] != '\0' ? upper(app->name[1]) : ' '};
            canvas.draw_text(icon.x + (icon.w - 16) / 2,
                             icon.y + icon.h / 2 + 6,
                             hcpp::StringView{mono, 2},
                             canvas.color(theme.primary_fg));
            if (is_system(app)) {
                canvas.fill_circle(icon.right() - 4, icon.y + 4, 7,
                                   canvas.color(theme.accent));
                canvas.fill_circle(icon.right() - 4, icon.y + 4, 3,
                                   canvas.color(theme.primary_fg));
            }
            // Label: ellipsis-fit to the tile so names cannot bleed
            // into the next grid cell (reskin v2 overlap fix).
            char label[36];
            const hcpp::size_t len =
                ellipsize(label, sizeof(label), app->name, tile.w - 8);
            const hcpp::i32 text_w =
                measure_px(hcpp::StringView{label, len}, 13);
            hcpp::i32 tx = tile.x + (tile.w - text_w) / 2;
            if (tx < tile.x) {
                tx = tile.x;
            }
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(tile));
            canvas.draw_text(tx, tile.bottom() - 8,
                             hcpp::StringView{label, len},
                             canvas.color(theme.fg));
            canvas.set_clip(saved_clip);
        }

        void ss_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            last_region = ctx.region;
            rebuild_order();
            Canvas canvas(*ctx.fb);
            canvas.set_clip(ctx.paint_clip());
            const Theme& theme = hype::ui::theme_active();
            const Rect& box = ctx.region;
            canvas.fill_rect(box, canvas.color(theme.card));
            const Rect search{box.x + ui::ui_px(pad), box.y + ui::ui_px(pad) - 4,
                              box.w - ui::ui_px(pad) * 2, ui::ui_px(search_h)};
            // Input at rest: sunk surface + border_strong edge (reskin
            // v2 input contract).
            hype::ui::rui_control_bg(canvas, search, theme.surface_sunk,
                                     theme.border_strong);
            const hcpp::i32 lens_x = search.x + 18;
            const hcpp::i32 lens_y = search.y + search.h / 2;
            canvas.fill_circle(lens_x, lens_y, 5,
                               canvas.color(theme.muted_fg));
            canvas.fill_circle(lens_x, lens_y, 3,
                               canvas.color(theme.surface_sunk));
            canvas.draw_line(lens_x + 3, lens_y + 3, lens_x + 8, lens_y + 8,
                             canvas.color(theme.muted_fg));
            canvas.draw_text(search.x + 34, search.y + 20, "Search apps",
                             canvas.color(theme.muted_fg));
            for (hcpp::u32 i = 0; i < order_count; ++i) {
                const Rect tile = ss_tile_rect(i);
                if (tile.empty() || !tile.intersects(box)) {
                    continue;
                }
                paint_tile(canvas, theme, tile, hxrt_list()[order[i]]);
            }
        }

        void ss_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "SpeedStart", "HYPEOS://S/apps/SpeedStart.hxersys",
            ss_entry, ss_close, nullptr};

    }

    const HxerHeader* speedstart_app() noexcept { return &desc; }

    hcpp::u32 ss_count() noexcept {
        rebuild_order();
        return order_count;
    }

    Rect ss_tile_rect(hcpp::u32 index) noexcept {
        if (index >= order_count || last_region.empty()) {
            return {};
        }
        const hcpp::i32 cols = grid_cols();
        const hcpp::i32 i = static_cast<hcpp::i32>(index);
        return {last_region.x + ui::ui_px(pad) + (i % cols) * ui::ui_px(cell_w),
                last_region.y + ui::ui_px(pad) + ui::ui_px(search_h) + ui::ui_px(grid_gap) +
                    (i / cols) * ui::ui_px(cell_h),
                ui::ui_px(cell_w), ui::ui_px(cell_h)};
    }

    hcpp::i32 ss_hit(hcpp::i32 x, hcpp::i32 y) noexcept {
        const Point p{x, y};
        for (hcpp::u32 i = 0; i < order_count; ++i) {
            if (ss_tile_rect(i).contains(p)) {
                return static_cast<hcpp::i32>(i);
            }
        }
        return -1;
    }

    hcpp::u32 ss_launch(hcpp::u32 index) noexcept {
        rebuild_order();
        if (index >= order_count) {
            return 0;
        }
        const Rect tile = ss_tile_rect(index);
        if (!tile.empty()) {
            hype::win::wm_test_click(tile.x + tile.w / 2,
                                     tile.y + tile.h / 2);
        }
        return hype::win::desk_open_app(hxrt_list()[order[index]]->name);
    }

    bool ss_selftest() noexcept {
        const hcpp::u32 tiles = ss_count();
        sync_region();
        serial::write("ss tiles=");
        serial::write_dec32(tiles);
        serial::write("\r\n");
        const bool count_ok = tiles == hxrt_count();
        bool launch_ok = false;
        for (hcpp::u32 i = 0; i < order_count && !launch_ok; ++i) {
            const HxerHeader* app = hxrt_list()[order[i]];
            if (name_equals(app->name, "SpeedStart")) {
                continue;
            }
            const Rect tile = ss_tile_rect(i);
            if (tile.empty()) {
                continue;
            }
            const hcpp::i32 hit =
                ss_hit(tile.x + tile.w / 2, tile.y + tile.h / 2);
            const hcpp::u32 before = hype::win::wm_count();
            const hcpp::u32 wid = ss_launch(i);
            const hype::win::Win* w = hype::win::wm_window(wid);
            launch_ok = hit == static_cast<hcpp::i32>(i) && wid != 0 &&
                        w != nullptr &&
                        hype::win::wm_count() == before + 1;
            if (wid != 0) {
                hype::win::wm_close(wid);
            }
        }
        serial::write("ss launch=");
        serial::write_line(launch_ok ? "ok" : "fail");
        return count_ok && launch_ok;
    }

}
