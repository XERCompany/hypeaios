#pragma once

// Select — trigger + anchored popup list (design spec
// docs/research/hypeui-design.md §5 `select`, Radix anatomy:
// Trigger{value, chevron} + Content{Item{check-slot,label}}). The
// closed state is a button showing the current value (or the dimmed
// placeholder); opening anchors a DropdownMenu under the trigger and a
// commit sets the value, fires on_change and closes.
//
// States: normal / hover / pressed / focused (ring) / disabled /
// open (parallel open bit — owns an L-layer popup, Esc or
// outside-press closes without commit). Keyboard contract while
// closed: Enter/Space/Down opens. While open: Up/Down/Enter/Esc are
// forwarded to the popup; first-letter jump works via the menu.

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"
#include "kernel/ui/widgets/dropdownmenu.h"

namespace hype::ui {

    class Canvas;

    // Pointer event kinds routed to Select::handle_mouse.
    enum class SelectMouse : hcpp::u8 { Move, Down, Up };

    class Select;
    using SelectCallback = void (*)(Select& select, hcpp::size_t index,
                                    void* context) noexcept;

    class Select : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t max_options =
            DropdownMenu::max_items;
        static constexpr hcpp::size_t no_selection =
            static_cast<hcpp::size_t>(-1);
        static constexpr hcpp::i32 trigger_height = 30;

        Select() noexcept = default;
        explicit Select(hcpp::StringView placeholder) noexcept;

        // Option strings are borrowed (like DropdownMenu labels).
        bool add_option(hcpp::StringView label) noexcept;
        void clear_options() noexcept;
        hcpp::size_t option_count() const noexcept;

        bool set_selected(hcpp::size_t index) noexcept;
        hcpp::size_t selected() const noexcept;
        hcpp::StringView value() const noexcept;
        void set_placeholder(hcpp::StringView text) noexcept;

        void set_enabled(bool enabled) noexcept;
        bool enabled() const noexcept;

        // Screen bounds for popup edge-flip/clamp.
        void set_screen(Rect screen) noexcept;
        void open() noexcept;
        void close() noexcept;
        bool is_open() const noexcept;
        Rect popup_rect() const noexcept;
        bool hovered() const noexcept;

        void set_on_change(SelectCallback callback, void* context) noexcept;
        void set_font(const hcpp::fonts::FontFace* font) noexcept;

        // Returns true when visual state changed (repaint wanted).
        bool handle_mouse(Point point, SelectMouse event) noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        static void on_menu_select(DropdownMenu& menu,
                                   hcpp::size_t index,
                                   void* context) noexcept;
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        DropdownMenu menu_;
        hcpp::StringView options_[max_options] = {};
        hcpp::StringView placeholder_;
        hcpp::size_t count_ = 0;
        hcpp::size_t selected_ = no_selection;
        SelectCallback on_change_ = nullptr;
        void* change_ctx_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
        bool enabled_ = true;
        bool focused_ = false;
        bool hovered_ = false;
        bool pressed_ = false;
    };

}  // namespace hype::ui
