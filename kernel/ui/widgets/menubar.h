#pragma once

// MenuBar — persistent horizontal menu row (design spec
// docs/research/hypeui-design.md §5 `menubar`): top-level titles
// (File/Edit/View style), each opening a DropdownMenu anchored under
// its title. The host supplies menu content on demand via on_menu so a
// single shared DropdownMenu backs every popup.
//
// States: closed / open. Per-title: hover-highlight (secondary wash),
// open (stays highlighted while its popup is up). While a popup is
// open, hovering a different title switches to that menu (classic
// menu-bar tracking); Esc or an outside press closes the whole thing.
// Keyboard while open: Left/Right switch menus, Up/Down/Enter/Esc are
// forwarded to the popup.

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"
#include "kernel/ui/widgets/dropdownmenu.h"

namespace hype::ui {

    class Canvas;

    class MenuBar;
    // Host fills `content` for `menu` each time it opens.
    using MenuBarFillCallback = void (*)(MenuBar& bar, hcpp::size_t menu,
                                         DropdownMenu& content,
                                         void* context) noexcept;
    // Fired on commit with the top-level menu + item indexes.
    using MenuBarSelectCallback = void (*)(MenuBar& bar,
                                           hcpp::size_t menu,
                                           hcpp::size_t item,
                                           void* context) noexcept;

    class MenuBar : public Widget {
    public:
        static constexpr hcpp::size_t max_menus = 6;
        static constexpr hcpp::size_t no_menu =
            static_cast<hcpp::size_t>(-1);
        static constexpr hcpp::i32 bar_height = 30;

        MenuBar() noexcept = default;

        // Title strings are borrowed (like DropdownMenu labels).
        bool add_menu(hcpp::StringView title) noexcept;
        void clear_menus() noexcept;
        hcpp::size_t menu_count() const noexcept;

        void set_on_menu(MenuBarFillCallback callback,
                         void* context) noexcept;
        void set_on_select(MenuBarSelectCallback callback,
                           void* context) noexcept;
        void set_screen(Rect screen) noexcept;
        void set_font(const hcpp::fonts::FontFace* font) noexcept;

        // Opens the popup under title `index` (asks on_menu to fill it).
        bool open_menu(hcpp::size_t index) noexcept;
        void close_menu() noexcept;
        bool is_open() const noexcept;
        hcpp::size_t open_menu_index() const noexcept;
        Rect title_rect(hcpp::size_t index) const noexcept;
        hcpp::size_t title_at(Point point) const noexcept;
        Rect popup_rect() const noexcept;

        // Returns true when visual state changed (repaint wanted).
        bool handle_mouse(Point point, MenuMouse event) noexcept;
        bool handle_key(char ascii) noexcept override;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        static void on_menu_pick(DropdownMenu& menu, hcpp::size_t index,
                                 void* context) noexcept;
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        void layout_titles() noexcept;

        hcpp::StringView titles_[max_menus] = {};
        Rect title_rects_[max_menus] = {};
        DropdownMenu menu_;
        hcpp::size_t count_ = 0;
        hcpp::size_t open_index_ = no_menu;
        hcpp::size_t hover_index_ = no_menu;
        Rect screen_{};
        MenuBarFillCallback on_menu_ = nullptr;
        void* menu_ctx_ = nullptr;
        MenuBarSelectCallback on_select_ = nullptr;
        void* select_ctx_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
    };

}  // namespace hype::ui
