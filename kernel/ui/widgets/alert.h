#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class AlertTone : hcpp::u8 { Info, Success, Warning, Destructive };

    class Alert : public Widget {
    public:
        Alert() noexcept = default;
        Alert(hcpp::StringView title, hcpp::StringView description) noexcept;

        void set_title(hcpp::StringView title) noexcept;
        hcpp::StringView title() const noexcept;

        void set_description(hcpp::StringView description) noexcept;
        hcpp::StringView description() const noexcept;

        void set_tone(AlertTone tone) noexcept;
        AlertTone tone() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;

        hcpp::StringView title_;
        hcpp::StringView description_;
        const hcpp::fonts::FontFace* font_ = nullptr;
        AlertTone tone_ = AlertTone::Info;
    };

}  // namespace hype::ui
