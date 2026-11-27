#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class LabelColor : hcpp::u8 { Primary, Muted, Accent };

    class Label : public Widget {
    public:
        Label() noexcept = default;
        explicit Label(hcpp::StringView text) noexcept;

        void set_text(hcpp::StringView text) noexcept;
        hcpp::StringView text() const noexcept;

        void set_color(LabelColor color) noexcept;
        LabelColor color() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        void set_text_size(hcpp::u32 size_px) noexcept;
        hcpp::u32 text_size() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView text_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        LabelColor color_ = LabelColor::Primary;
        hcpp::u32 size_px_ = 0;
    };

}  // namespace hype::ui
