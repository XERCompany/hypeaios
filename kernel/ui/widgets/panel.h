#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Panel : public Widget {
    public:
        Panel() noexcept = default;
        explicit Panel(hcpp::StringView title) noexcept;

        void set_title(hcpp::StringView title) noexcept;
        hcpp::StringView title() const noexcept;

        void set_title_bar_height(hcpp::i32 height) noexcept;
        hcpp::i32 title_bar_height() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Rect content_rect() const noexcept;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView title_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::i32 title_height_ = 0;
    };

}  // namespace hype::ui
