#pragma once

#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

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
