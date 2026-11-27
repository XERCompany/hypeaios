#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class TagsInput : public Widget, public Focusable {
    public:
        static constexpr hcpp::size_t max_tags = 8;
        static constexpr hcpp::size_t max_tag_length = 12;
        static constexpr hcpp::size_t buffer_capacity = 16;

        TagsInput() noexcept = default;

        bool add_tag(hcpp::StringView text) noexcept;
        bool remove_last() noexcept;
        void clear() noexcept;
        hcpp::size_t tag_count() const noexcept;
        hcpp::StringView tag_at(hcpp::size_t index) const noexcept;

        bool push_char(char c) noexcept;
        bool backspace() noexcept;
        bool commit_pending() noexcept;
        hcpp::StringView pending() const noexcept;

        void set_placeholder(hcpp::StringView placeholder) noexcept;
        hcpp::StringView placeholder() const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        char tags_[max_tags][max_tag_length] = {};
        hcpp::size_t tag_lengths_[max_tags] = {};
        hcpp::size_t tag_count_ = 0;
        char pending_[buffer_capacity] = {};
        hcpp::size_t pending_length_ = 0;
        hcpp::StringView placeholder_;
        Insets padding_{8, 5, 8, 5};
        bool focused_ = false;
    };

}  // namespace hype::ui
