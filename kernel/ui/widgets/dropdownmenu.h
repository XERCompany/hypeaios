#pragma once

// DropdownMenu — anchored popup menu (issue #178, design spec
// docs/research/hypeui-design.md §3.15). Powers context menus and
// menu-bar popups: rows of items with an icon/check slot, labels,
// right-aligned shortcuts, separators and section labels.
//
// States: closed / open. Per-item: hover-highlight (accent wash),
// disabled (dim, no wash), destructive (danger label + danger wash),
// checked (glyph in the icon slot). Dismissal contract: Esc or a
// pointer event outside menu_rect() closes without committing; Enter
// or pointer release on an item commits via on_select and closes.
//
// The widget is immediate-mode: the host lays out the anchor, calls
// open()/open_at(), routes mouse + key events, and paints it above the
// rest of the UI (it draws outside its anchor's bounds like an L3
// overlay).

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    class Canvas;

    // Pointer event kinds routed to DropdownMenu::handle_mouse.
    enum class MenuMouse : hcpp::u8 { Move, Down, Up };

    class DropdownMenu;
    using MenuCallback = void (*)(DropdownMenu& menu, hcpp::size_t index,
                                  void* context) noexcept;

    struct MenuItem {
        hcpp::StringView icon;      // single glyph drawn in the 16px slot
        hcpp::StringView label;
        hcpp::StringView shortcut;
        hcpp::u8 flags = 0;
    };

    // MenuItem flag bits.
    constexpr hcpp::u8 menu_f_separator = 1u << 0;  // 1px rule, no label
    constexpr hcpp::u8 menu_f_heading = 1u << 1;    // dim section label
    constexpr hcpp::u8 menu_f_disabled = 1u << 2;
    constexpr hcpp::u8 menu_f_destructive = 1u << 3;
    constexpr hcpp::u8 menu_f_checked = 1u << 4;

    class DropdownMenu {
    public:
        static constexpr hcpp::size_t max_items = 12;
        static constexpr hcpp::size_t no_item =
            static_cast<hcpp::size_t>(-1);
        static constexpr hcpp::i32 item_height = 28;
        static constexpr hcpp::i32 heading_height = 22;
        static constexpr hcpp::i32 separator_height = 9;
        static constexpr hcpp::i32 min_width = 160;

        DropdownMenu() noexcept = default;

        bool add_item(hcpp::StringView label) noexcept;
        bool add_item(hcpp::StringView icon, hcpp::StringView label,
                      hcpp::StringView shortcut) noexcept;
        bool add_heading(hcpp::StringView text) noexcept;
        bool add_separator() noexcept;
        void clear() noexcept;
        hcpp::size_t item_count() const noexcept;
        const MenuItem& item_at(hcpp::size_t index) const noexcept;

        bool set_item_flags(hcpp::size_t index, hcpp::u8 flags) noexcept;
        bool set_item_enabled(hcpp::size_t index, bool enabled) noexcept;
        bool set_item_checked(hcpp::size_t index, bool checked) noexcept;

        // Anchored open: menu's top-left sits under `anchor`, flipping
        // above it when it would clip the bottom of `screen`.
        void open(Rect anchor) noexcept;
        // Point open (context menu): top-left at `point`.
        void open_at(Point point) noexcept;
        void close() noexcept;
        bool is_open() const noexcept;

        void set_screen(Rect screen) noexcept;
        Rect screen() const noexcept;
        Rect menu_rect() const noexcept;
        bool hit_test(Point point) const noexcept;
        hcpp::size_t item_at_point(Point point) const noexcept;
        hcpp::size_t highlighted() const noexcept;

        void set_on_select(MenuCallback callback, void* context) noexcept;

        // Both return true when visual state changed (repaint wanted).
        bool handle_mouse(Point point, MenuMouse event) noexcept;
        bool handle_key(char key) noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        // No-op while closed.
        void paint(Canvas& canvas) const noexcept;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        hcpp::i32 row_height(hcpp::size_t index) const noexcept;
        Rect row_rect(hcpp::size_t index) const noexcept;
        bool selectable(hcpp::size_t index) const noexcept;
        bool move_highlight(hcpp::i32 delta) noexcept;
        bool activate(hcpp::size_t index) noexcept;
        hcpp::i32 content_height() const noexcept;
        hcpp::i32 content_width() const noexcept;

        MenuItem items_[max_items] = {};
        MenuCallback on_select_ = nullptr;
        void* select_ctx_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
        Rect screen_{};
        Rect rect_{};
        hcpp::size_t count_ = 0;
        hcpp::size_t highlighted_ = no_item;
        bool open_ = false;
    };

}  // namespace hype::ui
