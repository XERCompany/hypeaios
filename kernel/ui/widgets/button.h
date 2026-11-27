#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class ButtonStyle : hcpp::u8 { Accent, Outline, Destructive };

    class Button : public Widget {
    public:
        Button() noexcept = default;
        explicit Button(hcpp::StringView label) noexcept;

        void set_label(hcpp::StringView label) noexcept;
        hcpp::StringView label() const noexcept;

        void set_style(ButtonStyle style) noexcept;
        ButtonStyle style() const noexcept;

        void set_padding(Insets padding) noexcept;
        Insets padding() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView label_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        Insets padding_{14, 7, 14, 7};
        ButtonStyle style_ = ButtonStyle::Accent;
    };

}  // namespace hype::ui
