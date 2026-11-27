#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/flex.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/label.h"

namespace hype::ui {

    class Dialog : public Widget {
    public:
        using Callback = void (*)(void* context) noexcept;

        Dialog() noexcept;

        void set_title(hcpp::StringView title) noexcept;
        hcpp::StringView title() const noexcept;
        void set_description(hcpp::StringView description) noexcept;
        hcpp::StringView description() const noexcept;

        bool set_content(Widget* content) noexcept;
        Widget* content() const noexcept;

        void set_confirm_label(hcpp::StringView label) noexcept;
        void set_cancel_label(hcpp::StringView label) noexcept;
        void set_cancel_visible(bool visible) noexcept;
        void set_confirm_style(ButtonStyle style) noexcept;
        void set_on_confirm(Callback callback, void* context) noexcept;
        void set_on_cancel(Callback callback, void* context) noexcept;
        void set_max_width(hcpp::i32 width) noexcept;

        virtual bool confirm() noexcept;
        virtual bool cancel() noexcept;
        bool handle_key(char ascii) noexcept override;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    protected:
        Button& confirm_button() noexcept;
        Button& cancel_button() noexcept;

    private:
        Flex column_{FlexDirection::Column};
        Label title_label_;
        Label desc_label_;
        Flex content_slot_{FlexDirection::Column};
        Flex footer_{FlexDirection::Row};
        Button cancel_button_;
        Button confirm_button_;
        Widget* content_ = nullptr;
        Callback on_confirm_ = nullptr;
        void* confirm_context_ = nullptr;
        Callback on_cancel_ = nullptr;
        void* cancel_context_ = nullptr;
        Insets padding_{16, 16, 16, 16};
        hcpp::i32 min_width_ = 280;
        hcpp::i32 max_width_ = 440;
    };

    class AlertDialog : public Dialog {
    public:
        AlertDialog() noexcept;

        void set_message(hcpp::StringView message) noexcept;
    };

}  // namespace hype::ui
