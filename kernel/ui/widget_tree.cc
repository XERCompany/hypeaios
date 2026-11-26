#include "kernel/ui/widget_tree.h"

namespace hype::ui {

    void WidgetTree::set_root(Widget* root) noexcept { root_ = root; }

    Widget* WidgetTree::root() const noexcept { return root_; }

    void WidgetTree::layout_root(const Rect& rect) noexcept {
        if (root_ == nullptr) {
            return;
        }
        root_->measure(rect.size());
        root_->layout_tree(rect);
    }

    void WidgetTree::paint_tree(Canvas& canvas) noexcept {
        if (root_ == nullptr) {
            return;
        }
        root_->paint_tree(canvas);
    }

}  // namespace hype::ui
