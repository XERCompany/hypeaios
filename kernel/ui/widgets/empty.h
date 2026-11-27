#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class EmptyState : public Widget {
    public:
        EmptyState() noexcept = default;
        explicit EmptyState(hcpp::StringView title) noexcept;

        void set_glyph(hcpp::StringView glyph) noexcept;
        hcpp::StringView glyph() const noexcept;

        void set_title(hcpp::StringView title) noexcept;
        hcpp::StringView title() const noexcept;

        void set_description(hcpp::StringView description) noexcept;
        hcpp::StringView description() const noexcept;

        void set_cta(hcpp::StringView label) noexcept;
        hcpp::StringView cta() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Rect glyph_rect() const noexcept;
        Rect cta_rect() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        hcpp::i32 content_height() const noexcept;

        hcpp::StringView glyph_;
        hcpp::StringView title_;
        hcpp::StringView description_;
        hcpp::StringView cta_;
        const hcpp::fonts::FontFace* font_ = nullptr;
    };

}  // namespace hype::ui
