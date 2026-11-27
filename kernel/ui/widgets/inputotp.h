#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class InputOTP : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t capacity = 8;

        InputOTP() noexcept = default;
        explicit InputOTP(hcpp::size_t cells) noexcept;

        bool set_cells(hcpp::size_t cells) noexcept;
        hcpp::size_t cells() const noexcept;

        bool push_char(char c) noexcept;
        bool backspace() noexcept;
        void clear() noexcept;

        const char* data() const noexcept;
        hcpp::size_t length() const noexcept;
        hcpp::StringView value() const noexcept;
        bool complete() const noexcept;
        hcpp::size_t active() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::i32 cell_side() const noexcept;
        Rect cell_rect(hcpp::size_t index) const noexcept;

        char digits_[capacity] = {};
        hcpp::size_t cells_ = 4;
        hcpp::size_t length_ = 0;
        bool focused_ = false;
    };

}  // namespace hype::ui
