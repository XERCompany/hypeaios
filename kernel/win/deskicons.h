#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::win {

    // Desktop icon grid: column-major cells (~96x88 px: 48 px glyph plus a
    // centered label row) starting at the top-left of the work area with a
    // 12 px margin. Icons live between the wallpaper and the window band and
    // are painted only inside the current damage clip.
    // DPI-aware (issue #174): scaled by ui_scale_init, 96/88/12 px at 1.00x.
    extern hcpp::i32 desk_icon_cell_w;
    extern hcpp::i32 desk_icon_cell_h;
    extern hcpp::i32 desk_icon_margin;

    // Seeds the grid from hxrt_list() (user .hxer apps, alphabetical like
    // the Start menu) plus files at the HYPEOS://U root via fs_list.
    // Returns the icon count and prints "icons grid=N".
    hcpp::u32 desk_icons_init() noexcept;
    void desk_icons_refresh() noexcept;
    hcpp::u32 desk_icons_count() noexcept;

    // Paints every icon cell intersecting `clip`. Called by desk_paint
    // between the wallpaper pass and wm_paint.
    void desk_icons_paint(hcpp::graphics::Framebuffer& fb,
                          hype::ui::Rect clip) noexcept;

    // Left-button press on desktop space that hit no window: selects the
    // icon under the cursor (or clears the selection on empty cells) and
    // launches on a double-press within ~500 ms. Returns true when the
    // press was consumed by the icon band.
    bool desk_icons_handle_click(hcpp::i32 x, hcpp::i32 y) noexcept;

    // Right-button press on desktop space: opens the icon context menu
    // (Open/Rename/Delete/Properties) on a cell or the desktop menu
    // (Open Apps/Refresh/Change Wallpaper/Properties) on empty space.
    bool desk_icons_handle_rclick(hcpp::i32 x, hcpp::i32 y) noexcept;

}
