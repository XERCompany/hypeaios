#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Progress : public Widget {
    public:
        static constexpr hcpp::i32 default_track_height = 8;
        static constexpr hcpp::i32 default_width = 120;

        Progress() noexcept = default;
        explicit Progress(hcpp::u8 value) noexcept;

        void set_progress(hcpp::u8 value) noexcept;
        hcpp::u8 progress() const noexcept;

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
        hcpp::i32 track_height_ = 0;
    };

}  // namespace hype::ui
