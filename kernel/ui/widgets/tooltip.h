#pragma once

// Tooltip — timed hover hint anchored to a target rect (issue #178,
// design spec docs/research/hypeui-design.md §3.14).
//
// States: hidden -> armed (hover held, delay_frames elapsed via
// Animatable ticks) -> visible. Any input or hover-leave dismisses it
// (dismiss()). Never interactive — no hit testing, no focus.
//
// Placement: Above (default) / Below the target with a 4px offset, or
// Auto which prefers Above and flips when the tip would clip the top of
// `screen`. The tip is horizontally centered on the target and clamped
// inside `screen` when one is set.
//
// Inverted paint per spec: fg-color fill, surface-color text, r6,
// shadow-lg.

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    class Canvas;

    enum class TooltipPlacement : hcpp::u8 { Above, Below, Auto };

    class Tooltip : public Animatable {
    public:
        static constexpr hcpp::u32 delay_frames = 30;
        static constexpr hcpp::u32 fade_frames = 6;
        static constexpr hcpp::i32 gap = 4;

        Tooltip() noexcept = default;

        // Arms the delay timer for `target`. Re-arming the same target
        // keeps the timer; a different target restarts it.
        void arm(Rect target, hcpp::StringView text) noexcept;
        void arm(Rect target, hcpp::StringView text,
                 hcpp::StringView title) noexcept;
        // Hover-leave / any input: hide immediately, reset the timer.
        void dismiss() noexcept;

        bool armed() const noexcept;
        bool visible() const noexcept;
        hcpp::u32 armed_for() const noexcept;

        void set_placement(TooltipPlacement placement) noexcept;
        TooltipPlacement placement() const noexcept;
        // Screen bounds used by Auto-flip and x clamping.
        void set_screen(Rect screen) noexcept;
        Rect screen() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        // Resolved tip rect (valid once armed/visible).
        Rect tip_rect() const noexcept;
        Rect target() const noexcept;

        // Advances the arm timer / fade. Returns true when the paint
        // state changed (armed->visible edge or still fading in).
        bool tick(hcpp::u32 frame) noexcept override;

        // No-op while hidden.
        void paint(Canvas& canvas) const noexcept;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        Size content_size() const noexcept;

        Rect target_{};
        Rect screen_{};
        hcpp::StringView text_;
        hcpp::StringView title_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        TooltipPlacement placement_ = TooltipPlacement::Above;
        hcpp::u32 arm_frame_ = 0;
        hcpp::u32 armed_for_ = 0;
        bool armed_ = false;
        bool visible_ = false;
    };

}  // namespace hype::ui
