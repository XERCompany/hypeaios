#pragma once

// ContextMenu — point-anchored right-click menu (design spec
// docs/research/hypeui-design.md §5 `context-menu`). Thin Widget
// adapter over DropdownMenu: the host opens it at a cursor point, the
// widget positions its popup with screen-edge clamping, and item picks
// are reported as caller-owned ids through a pick callback.
//
// Item flags reuse the menu_f_* bit values from dropdownmenu.h, which
// deliberately match the legacy wm_ctx_* bits (separator=1,
// disabled=2) so existing WmCtxItem tables translate 1:1.
//
// States: closed / open. Dismissal contract (same as DropdownMenu):
// Esc or a pointer event outside the menu closes without committing;
// Enter or release on a selectable row commits via the pick callback.
// While open the widget owns an L-layer region — it is intended to be
// hosted as an OverlayHost modal; its layout() ignores the rect the
// host passes and anchors at the stored open point instead.

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"
#include "kernel/ui/widgets/dropdownmenu.h"

namespace hype::ui {

    class Canvas;

    class ContextMenu;
    // Fired once on commit with the picked item's caller id.
    using CtxMenuPick = void (*)(hcpp::u32 id, void* user) noexcept;

    class ContextMenu : public Widget {
    public:
        struct Item {
            const char* label;
            hcpp::u32 id;
            hcpp::u8 flags;
        };

        ContextMenu() noexcept = default;

        // Copies `items` (bounded to DropdownMenu::max_items), anchors
        // the popup at `point` (clamped to the screen set earlier) and
        // marks the widget open. `pick` receives the item id on commit.
        bool open_at(Point point, const Item* items, hcpp::u32 count,
                     CtxMenuPick pick, void* user) noexcept;
        void close() noexcept;
        bool is_open() const noexcept;

        void set_screen(Rect screen) noexcept;
        Rect menu_rect() const noexcept;
        hcpp::size_t highlighted() const noexcept;
        const DropdownMenu& menu() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;

        // Route pointer + keys through to the embedded menu. Returns
        // true when visual state changed or the event was consumed.
        bool handle_mouse(Point point, MenuMouse event) noexcept;
        bool handle_key(char ascii) noexcept override;

        Size measure(Size available) noexcept override;
        // Ignores `rect`: the popup anchors at the open point.
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        static void on_menu_select(DropdownMenu& menu,
                                   hcpp::size_t index,
                                   void* context) noexcept;

        DropdownMenu menu_;
        Item items_[DropdownMenu::max_items] = {};
        CtxMenuPick pick_ = nullptr;
        void* user_ = nullptr;
        Rect screen_{};
    };

}  // namespace hype::ui
