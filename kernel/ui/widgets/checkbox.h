#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Checkbox : public Widget, public Focusable {
    public:
        Checkbox() noexcept = default;
        explicit Checkbox(hcpp::StringView label) noexcept;

        void set_label(hcpp::StringView label) noexcept;
        hcpp::StringView label() const noexcept;

        void set_checked(bool checked) noexcept;
        bool checked() const noexcept;
        void toggle() noexcept;

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
        Rect box_rect() const noexcept;

        hcpp::StringView label_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        bool checked_ = false;
        bool focused_ = false;
    };

}  // namespace hype::ui
