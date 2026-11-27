#include "kernel/hxrt/apps/speedstart.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/apps/builtins.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
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
        constexpr hcpp::u32 name_max = 11;

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
            const hcpp::i32 usable = last_region.w - pad * 2;
            const hcpp::i32 cols = usable / cell_w;
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

        hcpp::u32 copy_name(char* out, const char* name,
                            hcpp::u32 max) noexcept {
            hcpp::u32 n = 0;
            while (n < max && name[n] != '\0') {
                out[n] = name[n];
                ++n;
            }
            return n;
        }

        void paint_tile(Canvas& canvas, const Theme& theme, const Rect& tile,
                        const HxerHeader* app) noexcept {
            const Rect icon{tile.x + (tile.w - icon_size) / 2, tile.y + 4,
                            icon_size, icon_size};
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
            char label[name_max];
            const hcpp::u32 len = copy_name(label, app->name, name_max);
            const hcpp::i32 text_w = static_cast<hcpp::i32>(len) * 8;
            hcpp::i32 tx = tile.x + (tile.w - text_w) / 2;
            if (tx < tile.x) {
                tx = tile.x;
            }
            canvas.draw_text(tx, tile.bottom() - 8,
                             hcpp::StringView{label, len},
                             canvas.color(theme.fg));
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
            const Rect search{box.x + pad, box.y + pad - 4,
                              box.w - pad * 2, search_h};
            hype::ui::rui_control_bg(canvas, search, theme.bg, theme.border);
            const hcpp::i32 lens_x = search.x + 18;
            const hcpp::i32 lens_y = search.y + search.h / 2;
            canvas.fill_circle(lens_x, lens_y, 5,
                               canvas.color(theme.muted_fg));
            canvas.fill_circle(lens_x, lens_y, 3, canvas.color(theme.bg));
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
            ss_entry, ss_close};

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
        return {last_region.x + pad + (i % cols) * cell_w,
                last_region.y + pad + search_h + grid_gap +
                    (i / cols) * cell_h,
                cell_w, cell_h};
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
