#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class TextInput : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t capacity = 48;

        TextInput() noexcept = default;
        explicit TextInput(hcpp::StringView placeholder) noexcept;

        void set_masked(bool masked) noexcept;
        bool masked() const noexcept;

        void set_placeholder(hcpp::StringView placeholder) noexcept;
        hcpp::StringView placeholder() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        void set_padding(Insets padding) noexcept;
        Insets padding() const noexcept;

        bool push_char(char c) noexcept;
        bool backspace() noexcept;
        void clear() noexcept;

        const char* data() const noexcept;
        hcpp::size_t length() const noexcept;
        hcpp::StringView text() const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        char buffer_[capacity] = {};
        hcpp::size_t length_ = 0;
        hcpp::StringView placeholder_;
        Insets padding_{10, 6, 10, 6};
        bool masked_ = false;
        bool focused_ = false;
    };

}  // namespace hype::ui
