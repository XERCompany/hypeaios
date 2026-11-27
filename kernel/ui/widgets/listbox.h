#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Listbox : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t max_options = 8;
        static constexpr hcpp::size_t none = static_cast<hcpp::size_t>(-1);

        Listbox() noexcept = default;

        bool add_option(hcpp::StringView label) noexcept;
        void clear() noexcept;
        hcpp::size_t option_count() const noexcept;
        hcpp::StringView option_at(hcpp::size_t index) const noexcept;

        void set_visible_count(hcpp::size_t count) noexcept;
        hcpp::size_t visible_count() const noexcept;

        bool highlight(hcpp::size_t index) noexcept;
        hcpp::size_t highlighted() const noexcept;
        hcpp::size_t selected() const noexcept;
        void reset() noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        hcpp::i32 row_height() const noexcept;
        void ensure_visible() noexcept;

        hcpp::StringView options_[max_options] = {};
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::size_t count_ = 0;
        hcpp::size_t highlighted_ = 0;
        hcpp::size_t selected_ = none;
        hcpp::size_t scroll_ = 0;
        hcpp::size_t visible_ = max_options;
        bool focused_ = false;
    };

}  // namespace hype::ui
