#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class RadioGroup : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t max_options = 8;

        RadioGroup() noexcept = default;

        bool add_option(hcpp::StringView label) noexcept;
        hcpp::size_t option_count() const noexcept;
        hcpp::StringView option_at(hcpp::size_t index) const noexcept;

        bool select(hcpp::size_t index) noexcept;
        hcpp::size_t selected() const noexcept;

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

        hcpp::StringView options_[max_options] = {};
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::size_t count_ = 0;
        hcpp::size_t selected_ = 0;
        bool focused_ = false;
    };

}  // namespace hype::ui
