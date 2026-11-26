#include "kernel/ui/widget.h"

namespace hype::ui {

    namespace {

        hcpp::i32 clamp_extent(hcpp::i32 value, hcpp::i32 limit) noexcept {
            if (limit >= 0 && value > limit) {
                return limit;
            }
            return value < 0 ? 0 : value;
        }

    }  // namespace

    Size Widget::measure(Size available) noexcept {
        Size desired = preferred_size_;
        if (desired.width <= 0 || desired.height <= 0) {
            for (hcpp::size_t i = 0; i < child_count_; ++i) {
                Widget* child = children_[i];
                if (child == nullptr || !child->is_visible()) {
                    continue;
                }
                const Size child_size = child->measure(available);
                if (child_size.width > desired.width) {
                    desired.width = child_size.width;
                }
                if (child_size.height > desired.height) {
                    desired.height = child_size.height;
                }
            }
        }
        desired.width = clamp_extent(desired.width, available.width);
        desired.height = clamp_extent(desired.height, available.height);
        return desired;
    }

    void Widget::layout(const Rect& rect) noexcept {
        bounds_ = rect;
        for (hcpp::size_t i = 0; i < child_count_; ++i) {
            Widget* child = children_[i];
            if (child != nullptr && child->is_visible()) {
                child->layout_tree(rect);
            }
        }
    }

    void Widget::paint(Canvas& canvas) noexcept { (void)canvas; }

    void Widget::layout_tree(const Rect& rect) noexcept {
        if (!visible_) {
            return;
        }
        layout(rect);
    }

    void Widget::paint_tree(Canvas& canvas) noexcept {
        if (!visible_) {
            return;
        }
        paint(canvas);
        if (child_count_ == 0) {
            return;
        }
        if (clips_children_) {
            const Rect saved = canvas.clip();
            canvas.set_clip(bounds_.intersection(saved));
            for (hcpp::size_t i = 0; i < child_count_; ++i) {
                if (children_[i] != nullptr) {
                    children_[i]->paint_tree(canvas);
                }
            }
            canvas.set_clip(saved);
            return;
        }
        for (hcpp::size_t i = 0; i < child_count_; ++i) {
            if (children_[i] != nullptr) {
                children_[i]->paint_tree(canvas);
            }
        }
    }

    bool Widget::add_child(Widget* child) noexcept {
        if (child == nullptr || child == this || child_count_ >= max_children ||
            child->is_ancestor_of(this)) {
            return false;
        }
        if (child->parent_ != nullptr) {
            child->parent_->remove_child(child);
        }
        children_[child_count_] = child;
        ++child_count_;
        child->parent_ = this;
        return true;
    }

    bool Widget::remove_child(Widget* child) noexcept {
        for (hcpp::size_t i = 0; i < child_count_; ++i) {
            if (children_[i] != child) {
                continue;
            }
            for (hcpp::size_t j = i + 1; j < child_count_; ++j) {
                children_[j - 1] = children_[j];
            }
            --child_count_;
            children_[child_count_] = nullptr;
            child->parent_ = nullptr;
            return true;
        }
        return false;
    }

    hcpp::size_t Widget::child_count() const noexcept { return child_count_; }

    Widget* Widget::child_at(hcpp::size_t index) const noexcept {
        if (index >= child_count_) {
            return nullptr;
        }
        return children_[index];
    }

    Widget* Widget::parent() const noexcept { return parent_; }

    bool Widget::is_visible() const noexcept { return visible_; }

    void Widget::set_visible(bool visible) noexcept { visible_ = visible; }

    Rect Widget::bounds() const noexcept { return bounds_; }

    void Widget::set_bounds(const Rect& rect) noexcept { bounds_ = rect; }

    Size Widget::preferred_size() const noexcept { return preferred_size_; }

    void Widget::set_preferred_size(Size size) noexcept { preferred_size_ = size; }

    bool Widget::clips_children() const noexcept { return clips_children_; }

    void Widget::set_clips_children(bool clips) noexcept { clips_children_ = clips; }

    bool Widget::is_ancestor_of(const Widget* widget) const noexcept {
        const Widget* cursor = widget;
        while (cursor != nullptr) {
            if (cursor == this) {
                return true;
            }
            cursor = cursor->parent_;
        }
        return false;
    }

}  // namespace hype::ui
