#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    // Progress — determinate fill bar + optional indeterminate sweep
    // (issue #178, design spec §3.18: a 32px accent segment sweeping the
    // track on a 90-frame loop, driven by AnimTicker).
    class Progress : public Widget, public Animatable {
    public:
        static constexpr hcpp::i32 default_track_height = 8;
        static constexpr hcpp::i32 default_width = 120;
        static constexpr hcpp::u32 sweep_period = 90;

        Progress() noexcept = default;
        explicit Progress(hcpp::u8 value) noexcept;

        void set_progress(hcpp::u8 value) noexcept;
        hcpp::u8 progress() const noexcept;

        // Indeterminate mode: paints a sweeping accent segment instead
        // of the determinate fill; drive it with AnimTicker ticks.
        void set_indeterminate(bool indeterminate) noexcept;
        bool indeterminate() const noexcept;
        hcpp::u8 phase() const noexcept;
        bool tick(hcpp::u32 frame) noexcept override;

        void set_label(hcpp::StringView label) noexcept;
        hcpp::StringView label() const noexcept;

        void set_track_height(hcpp::i32 height) noexcept;
        hcpp::i32 track_height() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView label_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::u8 value_ = 0;
        hcpp::u8 phase_ = 0;
        hcpp::i32 track_height_ = 0;
        bool indeterminate_ = false;
    };

}  // namespace hype::ui
