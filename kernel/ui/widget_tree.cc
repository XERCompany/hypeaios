#include "kernel/ui/widget_tree.h"

namespace hype::ui {

    hcpp::u32 widget_count(const Widget& root) noexcept {
        hcpp::u32 count = 1;
        for (hcpp::size_t i = 0; i < root.child_count(); ++i) {
            const Widget* child = root.child_at(i);
            if (child != nullptr) {
                count += widget_count(*child);
            }
        }
        return count;
    }

    Rect centered_window(const Rect& screen, hcpp::i32 width, hcpp::i32 height) noexcept {
        if (width > screen.w - 32) {
            width = screen.w - 32;
        }
        if (height > screen.h - 32) {
            height = screen.h - 32;
        }
        return {(screen.w - width) / 2, (screen.h - height) / 2, width, height};
    }

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
