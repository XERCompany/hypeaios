#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Switch : public Widget, public Focusable, public Animatable {
    public:
        static constexpr hcpp::u8 knob_steps = 4;

        Switch() noexcept = default;
        explicit Switch(hcpp::StringView label) noexcept;

        void set_label(hcpp::StringView label) noexcept;
        hcpp::StringView label() const noexcept;

        void set_on(bool on) noexcept;
        bool on() const noexcept;
        void toggle() noexcept;

        void tick() noexcept;
        bool tick(hcpp::u32 frame) noexcept override;
        hcpp::u8 position() const noexcept;

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
        Rect track_rect() const noexcept;

        hcpp::StringView label_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::u8 position_ = 0;
        bool on_ = false;
        bool focused_ = false;
    };

}  // namespace hype::ui
