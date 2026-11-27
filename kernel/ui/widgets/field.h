#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Field : public Widget, public Focusable {
    public:
        Field() noexcept = default;
        explicit Field(hcpp::StringView label) noexcept;

        void set_label(hcpp::StringView label) noexcept;
        hcpp::StringView label() const noexcept;

        void set_description(hcpp::StringView description) noexcept;
        hcpp::StringView description() const noexcept;

        void set_error(hcpp::StringView error) noexcept;
        hcpp::StringView error() const noexcept;
        bool has_error() const noexcept;
        void clear_error() noexcept;

        bool set_control(Widget* control, Focusable* key_target) noexcept;
        Widget* control() const noexcept;
        Focusable* key_target() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        hcpp::i32 line_height() const noexcept;
        hcpp::StringView hint_text() const noexcept;
        Rect slot_rect() const noexcept;

        hcpp::StringView label_;
        hcpp::StringView description_;
        hcpp::StringView error_;
        Widget* control_ = nullptr;
        Focusable* key_target_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
        bool focused_ = false;
    };

}  // namespace hype::ui
