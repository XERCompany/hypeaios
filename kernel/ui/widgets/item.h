#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Item : public Widget {
    public:
        Item() noexcept = default;
        explicit Item(hcpp::StringView title) noexcept;

        void set_icon(hcpp::StringView glyph) noexcept;
        hcpp::StringView icon() const noexcept;

        void set_title(hcpp::StringView title) noexcept;
        hcpp::StringView title() const noexcept;

        void set_description(hcpp::StringView description) noexcept;
        hcpp::StringView description() const noexcept;

        void set_action(hcpp::StringView label) noexcept;
        hcpp::StringView action() const noexcept;

        void set_icon_size(hcpp::i32 size) noexcept;
        hcpp::i32 icon_size() const noexcept;

        void set_padding(Insets padding) noexcept;
        Insets padding() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Rect icon_rect() const noexcept;
        Rect action_rect() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView icon_;
        hcpp::StringView title_;
        hcpp::StringView description_;
        hcpp::StringView action_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        Insets padding_{8, 6, 8, 6};
        hcpp::i32 icon_size_ = 20;
    };

}  // namespace hype::ui
