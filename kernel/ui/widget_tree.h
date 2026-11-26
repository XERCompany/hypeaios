#pragma once

#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    hcpp::u32 widget_count(const Widget& root) noexcept;
    Rect centered_window(const Rect& screen, hcpp::i32 width, hcpp::i32 height) noexcept;

    class WidgetTree {
    public:
        void set_root(Widget* root) noexcept;
        Widget* root() const noexcept;

        void layout_root(const Rect& rect) noexcept;
        void paint_tree(Canvas& canvas) noexcept;

    private:
        Widget* root_ = nullptr;
    };

}  // namespace hype::ui
