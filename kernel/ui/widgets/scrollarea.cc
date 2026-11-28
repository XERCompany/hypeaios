#include "kernel/ui/widgets/scrollarea.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    bool ScrollArea::set_content(Widget* content) noexcept {
        if (content_ == content) {
            return true;
        }
        if (content_ != nullptr) {
            remove_child(content_);
        }
        content_ = content;
        if (content_ == nullptr) {
            return true;
        }
        return add_child(content_);
    }

    Widget* ScrollArea::content() const noexcept { return content_; }

    void ScrollArea::set_scroll(hcpp::i32 offset) noexcept {
        const hcpp::i32 next = clamped(offset);
        if (next == offset_) {
            return;
        }
        offset_ = next;
        // Relayout the content so its paint origin tracks the offset.
        layout(bounds());
    }

    hcpp::i32 ScrollArea::scroll() const noexcept { return offset_; }

    hcpp::i32 ScrollArea::max_scroll() const noexcept {
        // Viewport height == bounds height; the rail only eats width.
        const hcpp::i32 max = content_h_ - bounds().h;
        return max > 0 ? max : 0;
    }

    hcpp::i32 ScrollArea::content_height() const noexcept {
        return content_h_;
    }

    bool ScrollArea::scrollable() const noexcept {
        return max_scroll() > 0;
    }

    Rect ScrollArea::viewport_rect() const noexcept {
        const Rect box = bounds();
        return {box.x, box.y,
                box.w - (scrollable() ? bar_width : 0), box.h};
    }

    Rect ScrollArea::track_rect() const noexcept {
        const Rect box = bounds();
        return {box.right() - bar_width, box.y, bar_width, box.h};
    }

    Rect ScrollArea::thumb_rect() const noexcept {
        if (!scrollable()) {
            return {};
        }
        const Rect track = track_rect();
        const Rect vp = viewport_rect();
        hcpp::i32 thumb_h = track.h * vp.h / content_h_;
        if (thumb_h < thumb_min) {
            thumb_h = thumb_min;
        }
        if (thumb_h > track.h) {
            thumb_h = track.h;
        }
        const hcpp::i32 travel = track.h - thumb_h;
        const hcpp::i32 y =
            travel > 0 ? track.y + offset_ * travel / max_scroll()
                       : track.y;
        return {track.x + 2, y, track.w - 4, thumb_h};
    }

    bool ScrollArea::scroll_by(hcpp::i32 delta) noexcept {
        const hcpp::i32 next = clamped(offset_ + delta);
        if (next == offset_) {
            return false;
        }
        offset_ = next;
        layout(bounds());
        return true;
    }

    bool ScrollArea::hovered() const noexcept { return hovered_; }

    bool ScrollArea::grabbed() const noexcept { return grabbed_; }

    bool ScrollArea::handle_mouse(Point point,
                                  ScrollMouse event) noexcept {
        if (event == ScrollMouse::Move) {
            if (grabbed_) {
                const Rect track = track_rect();
                const Rect thumb = thumb_rect();
                const hcpp::i32 travel = track.h - thumb.h;
                if (travel <= 0) {
                    return false;
                }
                const hcpp::i32 next =
                    (point.y - track.y - grab_off_) * max_scroll() /
                    travel;
                const hcpp::i32 clamped_next = clamped(next);
                if (clamped_next == offset_) {
                    return false;
                }
                offset_ = clamped_next;
                layout(bounds());
                return true;
            }
            const bool over =
                scrollable() && thumb_rect().contains(point);
            if (over == hovered_) {
                return false;
            }
            hovered_ = over;
            return true;
        }
        if (event == ScrollMouse::Down) {
            if (!scrollable()) {
                return false;
            }
            if (thumb_rect().contains(point)) {
                grabbed_ = true;
                grab_off_ = point.y - thumb_rect().y;
                return true;
            }
            if (track_rect().contains(point)) {
                // Track click: page the viewport toward the pointer.
                return scroll_by(point.y < thumb_rect().y
                                     ? -viewport_rect().h
                                     : viewport_rect().h);
            }
            return false;
        }
        // Up: release a grab wherever the pointer ended.
        if (grabbed_) {
            grabbed_ = false;
            return true;
        }
        return false;
    }

    bool ScrollArea::handle_key(char ascii) noexcept {
        if (ascii == keys::up) {
            return scroll_by(-scroll_step);
        }
        if (ascii == keys::down) {
            return scroll_by(scroll_step);
        }
        return false;
    }

    Size ScrollArea::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            if (content_ != nullptr) {
                const Size need = content_->measure(available);
                if (desired.width <= 0) {
                    desired.width = need.width + bar_width;
                }
                if (desired.height <= 0) {
                    desired.height = need.height;
                }
            }
        }
        if (available.width > 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height > 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void ScrollArea::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        if (content_ == nullptr) {
            return;
        }
        // First pass at full width decides whether the rail shows;
        // when it does, re-measure at the reduced viewport width so
        // wrapped content reports the right height. The height axis is
        // deliberately unclamped (-1): Widget::measure clamps the
        // desired height to the available height, which would shrink
        // tall content to the viewport and leave nothing to scroll.
        Size need = content_->measure({rect.w, -1});
        content_h_ = need.height > rect.h ? need.height : rect.h;
        if (scrollable()) {
            need = content_->measure({rect.w - bar_width, -1});
            content_h_ = need.height > rect.h ? need.height : rect.h;
        }
        const Rect vp = viewport_rect();
        offset_ = clamped(offset_);
        // Content is anchored at the viewport top minus the scroll
        // offset; clips_children (default) clips it to our bounds.
        content_->layout_tree(
            {vp.x, vp.y - offset_, vp.w, content_h_});
    }

    void ScrollArea::paint(Canvas& canvas) noexcept {
        if (!scrollable()) {
            return;
        }
        const Theme& theme = theme_active();
        const Rect track = track_rect();
        const Rect thumb = thumb_rect();
        // Hairline rail on the viewport's right edge + rounded thumb
        // (radius_xs per the token scale).
        canvas.fill_rect(
            Rect{track.x, track.y, 1, track.h},
            canvas.color(theme.border));
        const hcpp::u32 fill = (grabbed_ || hovered_)
                                   ? theme.muted_fg
                                   : theme.muted;
        canvas.fill_rounded_rect(
            thumb, static_cast<hcpp::i32>(theme.radius_xs),
            canvas.color(fill));
    }

    hcpp::i32 ScrollArea::clamped(hcpp::i32 offset) const noexcept {
        const hcpp::i32 max = max_scroll();
        if (offset > max) {
            return max;
        }
        return offset < 0 ? 0 : offset;
    }

}  // namespace hype::ui
