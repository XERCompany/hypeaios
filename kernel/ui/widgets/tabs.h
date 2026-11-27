#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Tabs : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t max_tabs = 8;
        using OnChange = void (*)(Tabs& tabs, hcpp::size_t index, void* context);

        Tabs() noexcept = default;

        bool add_tab(hcpp::StringView label, Widget* page) noexcept;
        hcpp::size_t tab_count() const noexcept;
        hcpp::StringView tab_label(hcpp::size_t index) const noexcept;

        bool activate(hcpp::size_t index) noexcept;
        hcpp::size_t active() const noexcept;
        Widget* active_page() const noexcept;

        void set_on_change(OnChange callback, void* context) noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        hcpp::i32 strip_height() const noexcept;
        Rect content_rect() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        void apply_visibility() noexcept;

        hcpp::StringView labels_[max_tabs] = {};
        Widget* pages_[max_tabs] = {};
        OnChange on_change_ = nullptr;
        void* context_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::size_t count_ = 0;
        hcpp::size_t active_ = 0;
        bool focused_ = false;
    };

}  // namespace hype::ui
