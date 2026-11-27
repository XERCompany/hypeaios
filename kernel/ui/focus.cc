#include "kernel/ui/focus.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    void draw_focus_ring(Canvas& canvas, const Rect& bounds) noexcept {
        if (bounds.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect ring{bounds.x - 2, bounds.y - 2, bounds.w + 4, bounds.h + 4};
        canvas.stroke_rounded_rect(ring,
                                   static_cast<hcpp::i32>(theme.radius_md) + 2,
                                   canvas.color(theme.ring));
    }

    bool FocusManager::add(Focusable* widget) noexcept {
        if (widget == nullptr || count_ >= capacity) {
            return false;
        }
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (items_[i] == widget) {
                return false;
            }
        }
        items_[count_] = widget;
        ++count_;
        if (focused_ == no_focus) {
            focused_ = count_ - 1;
            widget->set_focused(true);
        }
        return true;
    }

    bool FocusManager::remove(Focusable* widget) noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (items_[i] != widget) {
                continue;
            }
            for (hcpp::size_t j = i + 1; j < count_; ++j) {
                items_[j - 1] = items_[j];
            }
            --count_;
            items_[count_] = nullptr;
            if (focused_ == i) {
                widget->set_focused(false);
                focused_ = no_focus;
                focus_next();
            } else if (focused_ != no_focus && focused_ > i) {
                --focused_;
            }
            return true;
        }
        return false;
    }

    void FocusManager::clear() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (items_[i] != nullptr) {
                items_[i]->set_focused(false);
            }
            items_[i] = nullptr;
        }
        count_ = 0;
        focused_ = no_focus;
    }

    hcpp::size_t FocusManager::count() const noexcept { return count_; }

    hcpp::size_t FocusManager::focus_index() const noexcept { return focused_; }

    Focusable* FocusManager::focused() const noexcept { return item_at(focused_); }

    Focusable* FocusManager::item_at(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return nullptr;
        }
        return items_[index];
    }

    bool FocusManager::focus_at(hcpp::size_t index) noexcept {
        if (index >= count_ || index == focused_) {
            return false;
        }
        Focusable* previous = focused();
        if (previous != nullptr) {
            previous->set_focused(false);
        }
        focused_ = index;
        items_[focused_]->set_focused(true);
        return true;
    }

    bool FocusManager::focus_next() noexcept {
        if (count_ == 0) {
            return false;
        }
        const hcpp::size_t next =
            focused_ == no_focus ? 0 : (focused_ + 1) % count_;
        return focus_at(next);
    }

    bool FocusManager::focus_prev() noexcept {
        if (count_ == 0) {
            return false;
        }
        const hcpp::size_t prev =
            focused_ == no_focus ? count_ - 1
                                 : (focused_ + count_ - 1) % count_;
        return focus_at(prev);
    }

    bool FocusManager::handle_key(char c) noexcept {
        if (c == keys::tab) {
            return focus_next();
        }
        if (c == keys::backtab) {
            return focus_prev();
        }
        Focusable* target = focused();
        return target != nullptr && target->on_key(c);
    }

    void FocusManager::paint_ring(Canvas& canvas) const noexcept {
        Focusable* target = focused();
        if (target == nullptr) {
            return;
        }
        draw_focus_ring(canvas, target->focus_ring_rect());
    }

}  // namespace hype::ui
