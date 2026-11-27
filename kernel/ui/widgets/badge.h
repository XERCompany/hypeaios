#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class BadgeVariant : hcpp::u8 { Solid, Outline, Destructive, Secondary };

    class Badge : public Widget {
    public:
        Badge() noexcept = default;
        explicit Badge(hcpp::StringView text) noexcept;

        void set_text(hcpp::StringView text) noexcept;
        hcpp::StringView text() const noexcept;

        void set_variant(BadgeVariant variant) noexcept;
        BadgeVariant variant() const noexcept;

        void set_padding(Insets padding) noexcept;
        Insets padding() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView text_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        Insets padding_{8, 2, 8, 2};
        BadgeVariant variant_ = BadgeVariant::Solid;
    };

}  // namespace hype::ui
