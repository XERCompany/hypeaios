#include "kernel/win/deskicons.h"

#include "kernel/cfg/settings.h"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hxrt/apps/dxbg.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/huitokens.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/uiscale.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/deskbg.h"
#include "kernel/win/deskman.h"
#include "kernel/win/winman.h"

namespace hype::win {

    // Base 96/88/12 px; ui_scale_init rewrites them with the DPI scale.
    hcpp::i32 desk_icon_cell_w = 96;
    hcpp::i32 desk_icon_cell_h = 88;
    hcpp::i32 desk_icon_margin = 12;

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
        constexpr hcpp::i32 glyph_size = 48;
        constexpr hcpp::u64 dblclick_ms = 500;

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

        // "Change Wallpaper": cycle the wallpaper engine's pack — the
        // procedural designs first, then the U:/wallpapers image files,
        // then any HYPEOS://U/wallpaper.{png,jpg,qoi,bmp} image probes.
        // Prints a marker either way.
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
            static const char* const probe_ext[] = {"png", "jpg", "qoi",
                                                    "bmp"};
            hcpp::u32 probes = 0;
            hcpp::u8 probe_mask = 0;
            for (hcpp::u32 e = 0; e < 4; ++e) {
                char path[hype::vfs::hypepath_max_length];
                hcpp::u32 n = 0;
                const char* base = "HYPEOS://U/wallpaper.";
                while (*base != '\0' && n + 1 < sizeof(path)) {
                    path[n++] = *base++;
                }
                const char* ex = probe_ext[e];
                while (*ex != '\0' && n + 1 < sizeof(path)) {
                    path[n++] = *ex++;
                }
                path[n] = '\0';
                hype::vfs::FsStat st{};
                if (hype::vfs::fs_stat(path, st) ==
                        hype::vfs::FsStatus::Ok &&
                    (st.flags & hype::vfs::FS_DIR) == 0 && st.size != 0) {
                    probe_mask |= static_cast<hcpp::u8>(1u << e);
                    ++probes;
                }
            }
            const hcpp::u32 designs = hype::win::desk_bg_design_count();
            const hcpp::u32 total = designs + files + probes;
            if (total == 0) {
                serial::write_line("wallpaper cycle=none");
                return;
            }
            hcpp::u32 target = wall_cursor++ % total;
            char spec[hype::vfs::fs_name_size + 8];
            hcpp::u32 sn = 0;
            if (target < designs) {
                const char* dn = hype::win::desk_bg_design_name(target);
                while (dn != nullptr && *dn != '\0' && sn + 1 < sizeof(spec)) {
                    spec[sn++] = *dn++;
                }
            } else if (target - designs < files) {
                target -= designs;
                hcpp::u32 seen = 0;
                for (hcpp::u32 i = 0; i < bound; ++i) {
                    if ((listing[i].flags & hype::vfs::FS_DIR) != 0) {
                        continue;
                    }
                    if (seen++ != target) {
                        continue;
                    }
                    const char* leaf = listing[i].name;
                    while (*leaf != '\0' && sn + 1 < sizeof(spec)) {
                        spec[sn++] = *leaf++;
                    }
                    break;
                }
            } else {
                target -= designs + files;
                const char* pre = "img:wallpaper.";
                while (*pre != '\0' && sn + 1 < sizeof(spec)) {
                    spec[sn++] = *pre++;
                }
                hcpp::u32 seen = 0;
                for (hcpp::u32 e = 0; e < 4; ++e) {
                    if ((probe_mask & (1u << e)) == 0) {
                        continue;
                    }
                    if (seen++ != target) {
                        continue;
                    }
                    const char* ex = probe_ext[e];
                    while (*ex != '\0' && sn + 1 < sizeof(spec)) {
                        spec[sn++] = *ex++;
                    }
                    break;
                }
            }
            spec[sn] = '\0';
            if (sn == 0) {
                serial::write_line("wallpaper cycle=none");
                return;
            }
            if (hype::hxrt::dxbg_apply_spec(spec)) {
                serial::write("wallpaper=");
                serial::write_line(spec);
                // Persisted pref (issue #130): restored at next boot.
                hype::cfg::settings_set("desktop.wallpaper", spec);
                hype::cfg::settings_request_save();
            } else {
                serial::write_line("wallpaper cycle=fail");
            }
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

        // t/255 lerp of `top` over `base` — pre-blends translucent
        // chips so fills stay rounded (the canvas has no rounded-alpha
        // fill); same trick as the deskbar's accent_wash.
        hcpp::u32 mix_rgb(hcpp::u32 base, hcpp::u32 top,
                          hcpp::u8 t) noexcept {
            const hcpp::u32 inv = 255 - t;
            const hcpp::u32 r =
                (((base >> 16) & 0xFF) * inv +
                 ((top >> 16) & 0xFF) * t) / 255;
            const hcpp::u32 g =
                (((base >> 8) & 0xFF) * inv +
                 ((top >> 8) & 0xFF) * t) / 255;
            const hcpp::u32 b =
                ((base & 0xFF) * inv + (top & 0xFF) * t) / 255;
            return (r << 16) | (g << 8) | b;
        }

        hcpp::i32 measure_px(hcpp::StringView text,
                             hcpp::u32 px) noexcept {
            if (hype::font::hypefont_ready()) {
                return hype::font::text_measure(text, px);
            }
            return static_cast<hcpp::i32>(text.size()) * 8;
        }

        // Fits `name` into `max_w` px with a trailing U+2026 ellipsis
        // (reskin v2): labels clip to their cell so long names can
        // never collide with a neighbour icon's label.
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

        void paint_icon(Canvas& canvas, const Theme& theme,
                        hcpp::u32 index) noexcept {
            const Rect cell = cell_rect(index);
            const DeskIcon& icon = icons[index];
            if (static_cast<hcpp::i32>(index) == selected) {
                // Selected state: accent_soft wash box + accent edge
                // (reskin v2 — was a flat solid accent tint).
                const Rect sel_box = cell.inset(hype::ui::Insets::uniform(2));
                canvas.fill_rounded_rect(
                    sel_box, static_cast<hcpp::i32>(theme.radius_sm),
                    canvas.color(
                        mix_rgb(theme.bg, theme.accent_soft,
                                theme.accent_soft_alpha)));
                canvas.stroke_rounded_rect(
                    sel_box, static_cast<hcpp::i32>(theme.radius_sm),
                    canvas.color(theme.accent));
            }
            const hcpp::i32 glyph_px = hype::ui::ui_px(glyph_size);
            const Rect glyph{cell.x + (cell.w - glyph_px) / 2, cell.y + 2,
                             glyph_px, glyph_px};
            if (icon.kind == IconApp) {
                paint_glyph_app(canvas, theme, glyph, icon);
            } else {
                paint_glyph_file(canvas, theme, glyph);
            }
            char label[36];
            const hcpp::size_t len = ellipsize(
                label, sizeof(label), icon.name,
                cell.w - hype::ui::ui_px(8));
            // Scaled-font-aware width (issue #174): the 8 px/em estimate
            // drifts once hypetext applies the DPI scale, so measure
            // through the same scaled path canvas.draw_text renders with.
            hcpp::i32 text_w =
                measure_px(hcpp::StringView{label, len}, 13);
            if (text_w <= 0) {
                text_w = static_cast<hcpp::i32>(len) * 8;
            }
            hcpp::i32 tx = cell.x + (cell.w - text_w) / 2;
            if (tx < cell.x) {
                tx = cell.x;
            }
            // Label chip (reskin v2): a small r4 surface_sunk pill
            // behind the text keeps it readable over any wallpaper.
            const hcpp::i32 chip_pad = hype::ui::ui_px(4);
            Rect chip{tx - chip_pad, cell.y + cell.h - 17,
                      text_w + chip_pad * 2, 14};
            if (chip.x < cell.x) {
                chip.x = cell.x;
            }
            if (chip.right() > cell.right()) {
                chip.w = cell.right() - chip.x;
            }
            canvas.fill_rounded_rect(
                chip, static_cast<hcpp::i32>(theme.radius_xs),
                canvas.color(mix_rgb(theme.bg, theme.card, 200)));
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(chip));
            canvas.draw_text(tx, cell.y + cell.h - 14,
                             hcpp::StringView{label, len},
                             canvas.color(theme.fg));
            canvas.set_clip(saved_clip);
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
