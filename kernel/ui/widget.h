#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    class Widget {
    public:
        static constexpr hcpp::size_t max_children = 16;

        Widget() noexcept = default;

        virtual Size measure(Size available) noexcept;
        virtual void layout(const Rect& rect) noexcept;
        virtual void paint(Canvas& canvas) noexcept;

        void layout_tree(const Rect& rect) noexcept;
        void paint_tree(Canvas& canvas) noexcept;

        bool add_child(Widget* child) noexcept;
        bool remove_child(Widget* child) noexcept;
        hcpp::size_t child_count() const noexcept;
        Widget* child_at(hcpp::size_t index) const noexcept;

        Widget* parent() const noexcept;
        bool is_visible() const noexcept;
        void set_visible(bool visible) noexcept;

        Rect bounds() const noexcept;
        void set_bounds(const Rect& rect) noexcept;

        Size preferred_size() const noexcept;
        void set_preferred_size(Size size) noexcept;

        bool clips_children() const noexcept;
        void set_clips_children(bool clips) noexcept;

    private:
        bool is_ancestor_of(const Widget* widget) const noexcept;

        Widget* parent_ = nullptr;
        Widget* children_[max_children] = {};
        hcpp::size_t child_count_ = 0;
        Rect bounds_;
        Size preferred_size_;
        bool visible_ = true;
        bool clips_children_ = true;
    };

}  // namespace hype::ui
