#include "kernel/win/deskicons.h"

#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/apps/dxbg.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/deskman.h"
#include "kernel/win/winman.h"

namespace hype::win {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Point;
        using hype::ui::Rect;
        using hype::ui::Theme;

        enum DeskIconKind : hcpp::u8 { IconApp = 0, IconFile = 1 };

        struct DeskIcon {
            char name[32];
            hcpp::u8 kind;
        };

        constexpr hcpp::u32 icon_max = 48;
        constexpr hcpp::u32 icon_name_cap = 31;
        constexpr hcpp::u32 label_max = 11;
        constexpr hcpp::i32 glyph_size = 48;
        constexpr hcpp::u64 dblclick_ms = 500;
        constexpr hcpp::u8 sel_alpha = 76;

        constexpr const char* user_root = "HYPEOS://U";
        constexpr const char* wall_dir = "HYPEOS://U/wallpapers";

        // Context menu command ids (WmCtxItem::id).
        constexpr hcpp::u32 ctx_desk_apps = 1;
        constexpr hcpp::u32 ctx_desk_refresh = 2;
        constexpr hcpp::u32 ctx_desk_wall = 3;
        constexpr hcpp::u32 ctx_desk_props = 4;
        constexpr hcpp::u32 ctx_icon_open = 11;
        constexpr hcpp::u32 ctx_icon_rename = 12;
        constexpr hcpp::u32 ctx_icon_delete = 13;
        constexpr hcpp::u32 ctx_icon_props = 14;

        DeskIcon icons[icon_max];
        hcpp::u32 icon_count = 0;
        bool grid_built = false;
        hcpp::i32 work_w = 0;
        hcpp::i32 work_h = 0;
        hcpp::i32 selected = -1;
        hcpp::i32 last_click_icon = -1;
        hcpp::u64 last_click_ms = 0;
        hcpp::u32 wall_cursor = 0;

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

        hcpp::u32 name_copy(char* dst, const char* src) noexcept {
            hcpp::u32 n = 0;
            while (n < icon_name_cap && src[n] != '\0') {
                dst[n] = src[n];
                ++n;
            }
            dst[n] = '\0';
            return n;
        }

        hcpp::i32 grid_rows() noexcept {
            hcpp::i32 rows = work_h / desk_icon_cell_h;
            return rows > 0 ? rows : 1;
        }

        Rect cell_rect(hcpp::u32 index) noexcept {
            const hcpp::i32 rows = grid_rows();
            const hcpp::i32 i = static_cast<hcpp::i32>(index);
            return {desk_icon_margin + (i / rows) * desk_icon_cell_w,
                    desk_icon_margin + (i % rows) * desk_icon_cell_h,
                    desk_icon_cell_w, desk_icon_cell_h};
        }

        void invalidate_cell(hcpp::i32 index) noexcept {
            if (index >= 0 && static_cast<hcpp::u32>(index) < icon_count) {
                wm_invalidate(cell_rect(static_cast<hcpp::u32>(index)));
            }
        }

        hcpp::i32 icon_at(hcpp::i32 x, hcpp::i32 y) noexcept {
            const Point p{x, y};
            for (hcpp::u32 i = 0; i < icon_count; ++i) {
                if (cell_rect(i).contains(p)) {
                    return static_cast<hcpp::i32>(i);
                }
            }
            return -1;
        }

        void select_icon(hcpp::i32 index) noexcept {
            if (selected == index) {
                return;
            }
            const hcpp::i32 prev = selected;
            selected = index;
            invalidate_cell(prev);
            invalidate_cell(selected);
            if (index >= 0 &&
                static_cast<hcpp::u32>(index) < icon_count) {
                serial::write("icon sel=");
                serial::write_line(icons[index].name);
            }
        }

        void launch_icon(hcpp::u32 index) noexcept {
            if (index >= icon_count) {
                return;
            }
            serial::write("icon open=");
            serial::write_line(icons[index].name);
            if (icons[index].kind == IconApp) {
                desk_open_app(icons[index].name);
            }
            // Files only print the marker for now; there is no file
            // association table yet.
        }

        // "Change Wallpaper": cycle the U:/wallpapers pack through the
        // dxbg decoder. Prints a marker either way.
        void cycle_wallpaper() noexcept {
            hype::vfs::FsStat listing[16];
            const hcpp::u32 found =
                hype::vfs::fs_list(wall_dir, listing, 16);
            hcpp::u32 files = 0;
            const hcpp::u32 bound = found < 16 ? found : 16;
            for (hcpp::u32 i = 0; i < bound; ++i) {
                if ((listing[i].flags & hype::vfs::FS_DIR) == 0) {
                    ++files;
                }
            }
            if (files == 0) {
                serial::write_line("wallpaper cycle=none");
                return;
            }
            const hcpp::u32 target = wall_cursor++ % files;
            hcpp::u32 seen = 0;
            for (hcpp::u32 i = 0; i < bound; ++i) {
                if ((listing[i].flags & hype::vfs::FS_DIR) != 0) {
                    continue;
                }
                if (seen++ != target) {
                    continue;
                }
                char path[hype::vfs::hypepath_max_length];
                hcpp::u32 n = 0;
                const char* dir = wall_dir;
                while (*dir != '\0' && n + 1 < sizeof(path)) {
                    path[n++] = *dir++;
                }
                if (n + 1 < sizeof(path)) {
                    path[n++] = '/';
                }
                const char* leaf = listing[i].name;
                while (*leaf != '\0' && n + 1 < sizeof(path)) {
                    path[n++] = *leaf++;
                }
                path[n] = '\0';
                if (hype::hxrt::dxbg_apply(path)) {
                    serial::write("wallpaper=");
                    serial::write_line(listing[i].name);
                } else {
                    serial::write_line("wallpaper cycle=fail");
                }
                return;
            }
            serial::write_line("wallpaper cycle=none");
        }

        // WmCtxPick callback for the icon context menu. `user` is the
        // icon index encoded as a pointer-sized value.
        void icon_menu_pick(hcpp::u32 id, void* user) noexcept {
            const hcpp::u32 index =
                static_cast<hcpp::u32>(
                    reinterpret_cast<hcpp::uintptr_t>(user));
            if (index >= icon_count) {
                return;
            }
            const DeskIcon& icon = icons[index];
            switch (id) {
                case ctx_icon_open:
                    launch_icon(index);
                    break;
                case ctx_icon_rename:
                    serial::write("icon rename=");
                    serial::write_line(icon.name);
                    break;
                case ctx_icon_delete: {
                    if (icon.kind != IconFile) {
                        break;
                    }
                    char path[hype::vfs::hypepath_max_length];
                    hcpp::u32 n = 0;
                    const char* dir = user_root;
                    while (*dir != '\0' && n + 1 < sizeof(path)) {
                        path[n++] = *dir++;
                    }
                    if (n + 1 < sizeof(path)) {
                        path[n++] = '/';
                    }
                    const char* leaf = icon.name;
                    while (*leaf != '\0' && n + 1 < sizeof(path)) {
                        path[n++] = *leaf++;
                    }
                    path[n] = '\0';
                    const bool gone =
                        hype::vfs::fs_delete(path) ==
                        hype::vfs::FsStatus::Ok;
                    serial::write("icon delete=");
                    serial::write(icon.name);
                    serial::write(" ok=");
                    serial::write_line(gone ? "1" : "0");
                    desk_icons_refresh();
                    break;
                }
                case ctx_icon_props:
                    serial::write("icon props=");
                    serial::write_line(icon.name);
                    break;
                default:
                    break;
            }
        }

        void desk_menu_pick(hcpp::u32 id, void* user) noexcept {
            (void)user;
            switch (id) {
                case ctx_desk_apps:
                    hype::ui::deskbar_open_start();
                    break;
                case ctx_desk_refresh:
                    desk_icons_refresh();
                    break;
                case ctx_desk_wall:
                    cycle_wallpaper();
                    break;
                case ctx_desk_props:
                    serial::write_line("desk props=ok");
                    break;
                default:
                    break;
            }
        }

        void paint_glyph_app(Canvas& canvas, const Theme& theme,
                             const Rect& glyph, const DeskIcon& icon) noexcept {
            hype::ui::rui_rounded_gradient(
                canvas, glyph, static_cast<hcpp::i32>(theme.radius_md),
                hype::ui::rui_lift(theme.primary, 1), theme.primary);
            hype::gfx::gfx_inner_glow_edge(
                canvas, glyph, static_cast<hcpp::i32>(theme.radius_md),
                theme.ring);
            char mono[2] = {upper(icon.name[0]),
                            icon.name[1] != '\0' ? upper(icon.name[1])
                                                 : ' '};
            canvas.draw_text(glyph.x + (glyph.w - 16) / 2,
                             glyph.y + glyph.h / 2 + 6,
                             hcpp::StringView{mono, 2},
                             canvas.color(theme.primary_fg));
        }

        void paint_glyph_file(Canvas& canvas, const Theme& theme,
                              const Rect& glyph) noexcept {
            // Page glyph: card sheet with a folded top-right corner and
            // two muted text lines.
            const hcpp::i32 px = glyph.x + (glyph.w - 34) / 2;
            const hcpp::i32 py = glyph.y + 2;
            const Rect page{px, py, 34, 44};
            canvas.fill_rect(page, canvas.color(theme.card));
            canvas.stroke_rect(page, canvas.color(theme.border));
            const hcpp::u32 edge = canvas.color(theme.border);
            canvas.draw_line(px + 24, py, px + 33, py + 9, edge);
            canvas.draw_line(px + 24, py, px + 24, py + 9, edge);
            canvas.draw_line(px + 24, py + 9, px + 33, py + 9, edge);
            const hcpp::u32 ink = canvas.color(theme.muted_fg);
            canvas.fill_rect(Rect{px + 6, py + 16, 22, 2}, ink);
            canvas.fill_rect(Rect{px + 6, py + 22, 22, 2}, ink);
            canvas.fill_rect(Rect{px + 6, py + 28, 14, 2}, ink);
        }

        void paint_icon(Canvas& canvas, const Theme& theme,
                        hcpp::u32 index) noexcept {
            const Rect cell = cell_rect(index);
            const DeskIcon& icon = icons[index];
            if (static_cast<hcpp::i32>(index) == selected) {
                const Rect sel_box = cell.inset(hype::ui::Insets::uniform(2));
                canvas.fill_rect_alpha(sel_box, canvas.color(theme.accent),
                                       sel_alpha);
                canvas.stroke_rounded_rect(
                    sel_box, static_cast<hcpp::i32>(theme.radius_sm),
                    canvas.color(theme.accent));
            }
            const Rect glyph{cell.x + (cell.w - glyph_size) / 2, cell.y + 2,
                             glyph_size, glyph_size};
            if (icon.kind == IconApp) {
                paint_glyph_app(canvas, theme, glyph, icon);
            } else {
                paint_glyph_file(canvas, theme, glyph);
            }
            char label[label_max + 1];
            hcpp::u32 len = 0;
            while (len < label_max && icon.name[len] != '\0') {
                label[len] = icon.name[len];
                ++len;
            }
            label[len] = '\0';
            const hcpp::i32 text_w = static_cast<hcpp::i32>(len) * 8;
            hcpp::i32 tx = cell.x + (cell.w - text_w) / 2;
            if (tx < cell.x) {
                tx = cell.x;
            }
            canvas.draw_text(tx, cell.y + cell.h - 14,
                             hcpp::StringView{label, len},
                             canvas.color(theme.fg));
        }

    }

    hcpp::u32 desk_icons_init() noexcept {
        icon_count = 0;
        // User .hxer apps, alphabetical — the same ordering the deskbar
        // Start menu builds; system entries are launchers/services and
        // stay off the desktop per desktop-shell.md §4.
        hcpp::u32 order[hype::hxrt::hxrt_max_apps];
        hcpp::u32 order_count = 0;
        const hcpp::u32 app_total = hype::hxrt::hxrt_count();
        for (hcpp::u32 i = 0; i < app_total; ++i) {
            if (!hype::hxrt::hxrt_is_system(i)) {
                order[order_count++] = i;
            }
        }
        const hype::hxrt::HxerHeader* const* list = hype::hxrt::hxrt_list();
        for (hcpp::u32 i = 1; i < order_count; ++i) {
            const hcpp::u32 key = order[i];
            hcpp::u32 j = i;
            while (j > 0 &&
                   name_less(list[key]->name, list[order[j - 1]]->name)) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = key;
        }
        for (hcpp::u32 i = 0; i < order_count && icon_count < icon_max; ++i) {
            name_copy(icons[icon_count].name, list[order[i]]->name);
            icons[icon_count].kind = IconApp;
            ++icon_count;
        }
        // Files at the HYPEOS://U root become page-glyph icons.
        hype::vfs::FsStat listing[icon_max];
        const hcpp::u32 found =
            hype::vfs::fs_list(user_root, listing, icon_max);
        const hcpp::u32 bound = found < icon_max ? found : icon_max;
        for (hcpp::u32 i = 0; i < bound && icon_count < icon_max; ++i) {
            if ((listing[i].flags & hype::vfs::FS_DIR) != 0) {
                continue;
            }
            name_copy(icons[icon_count].name, listing[i].name);
            icons[icon_count].kind = IconFile;
            ++icon_count;
        }
        grid_built = true;
        selected = -1;
        last_click_icon = -1;
        serial::write("icons grid=");
        serial::write_dec32(icon_count);
        serial::write("\r\n");
        return icon_count;
    }

    void desk_icons_refresh() noexcept {
        desk_icons_init();
        wm_invalidate_all();
    }

    hcpp::u32 desk_icons_count() noexcept { return icon_count; }

    void desk_icons_paint(hcpp::graphics::Framebuffer& fb,
                          Rect clip) noexcept {
        if (!fb.valid() || clip.empty()) {
            return;
        }
        work_w = static_cast<hcpp::i32>(fb.width());
        work_h = static_cast<hcpp::i32>(fb.height()) -
                 hype::ui::deskbar_height - desk_icon_margin;
        if (!grid_built) {
            desk_icons_init();
        }
        Canvas canvas(fb);
        canvas.set_clip(clip);
        const Theme& theme = hype::ui::theme_active();
        for (hcpp::u32 i = 0; i < icon_count; ++i) {
            if (!cell_rect(i).intersects(clip)) {
                continue;
            }
            paint_icon(canvas, theme, i);
        }
    }

    bool desk_icons_handle_click(hcpp::i32 x, hcpp::i32 y) noexcept {
        if (!grid_built || work_w == 0) {
            return false;
        }
        const hcpp::i32 hit = icon_at(x, y);
        if (hit < 0) {
            select_icon(-1);
            return false;
        }
        const hcpp::u64 now = hype::time::uptime_ms();
        const bool dbl = last_click_icon == hit &&
                         now - last_click_ms <= dblclick_ms;
        select_icon(hit);
        last_click_icon = hit;
        last_click_ms = now;
        if (dbl) {
            launch_icon(static_cast<hcpp::u32>(hit));
            last_click_icon = -1;
        }
        return true;
    }

    bool desk_icons_handle_rclick(hcpp::i32 x, hcpp::i32 y) noexcept {
        if (!grid_built || work_w == 0) {
            return false;
        }
        const hcpp::i32 hit = icon_at(x, y);
        if (hit >= 0) {
            select_icon(hit);
            const DeskIcon& icon = icons[hit];
            const WmCtxItem items[] = {
                {"Open", ctx_icon_open, 0},
                {nullptr, 0, wm_ctx_separator},
                {"Rename", ctx_icon_rename, 0},
                {"Delete", ctx_icon_delete,
                 static_cast<hcpp::u8>(icon.kind == IconApp
                                           ? wm_ctx_disabled
                                           : 0)},
                {nullptr, 0, wm_ctx_separator},
                {"Properties", ctx_icon_props, 0},
            };
            return wm_ctx_open(x, y, items, 6, icon_menu_pick,
                               reinterpret_cast<void*>(
                                   static_cast<hcpp::uintptr_t>(hit)));
        }
        const WmCtxItem items[] = {
            {"Open Apps", ctx_desk_apps, 0},
            {nullptr, 0, wm_ctx_separator},
            {"Refresh", ctx_desk_refresh, 0},
            {"Change Wallpaper", ctx_desk_wall, 0},
            {nullptr, 0, wm_ctx_separator},
            {"Properties", ctx_desk_props, 0},
        };
        return wm_ctx_open(x, y, items, 6, desk_menu_pick, nullptr);
    }

}
