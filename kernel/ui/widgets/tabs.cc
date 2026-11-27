#include "kernel/ui/widgets/tabs.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    bool Tabs::add_tab(hcpp::StringView label, Widget* page) noexcept {
        if (page == nullptr || count_ >= max_tabs || !add_child(page)) {
            return false;
        }
        labels_[count_] = label;
        pages_[count_] = page;
        ++count_;
        apply_visibility();
        return true;
    }

    hcpp::size_t Tabs::tab_count() const noexcept { return count_; }

    hcpp::StringView Tabs::tab_label(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return {};
        }
        return labels_[index];
    }

    bool Tabs::activate(hcpp::size_t index) noexcept {
        if (index >= count_ || index == active_) {
            return false;
        }
        active_ = index;
        apply_visibility();
        if (on_change_ != nullptr) {
            on_change_(*this, active_, context_);
        }
        return true;
    }

    hcpp::size_t Tabs::active() const noexcept { return active_; }

    Widget* Tabs::active_page() const noexcept {
        if (active_ >= count_) {
            return nullptr;
        }
        return pages_[active_];
    }

    void Tabs::set_on_change(OnChange callback, void* context) noexcept {
        on_change_ = callback;
        context_ = context;
    }

    void Tabs::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Tabs::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Tabs::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 Tabs::strip_height() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        return (face != nullptr ? static_cast<hcpp::i32>(face->height) : 16) + 10;
    }

    Rect Tabs::content_rect() const noexcept {
        const Rect b = bounds();
        const hcpp::i32 strip = strip_height();
        if (b.h <= strip) {
            return {b.x, b.y + strip, b.w, 0};
        }
        return {b.x, b.y + strip, b.w, b.h - strip};
    }

    void Tabs::set_focused(bool focused) noexcept { focused_ = focused; }

    bool Tabs::focused() const noexcept { return focused_; }

    bool Tabs::on_key(char key) noexcept {
        if (count_ == 0) {
            return false;
        }
        if (key == keys::left) {
            return activate((active_ + count_ - 1) % count_);
        }
        if (key == keys::right) {
            return activate((active_ + 1) % count_);
        }
        return false;
    }

    Rect Tabs::focus_ring_rect() const noexcept { return bounds(); }

    Size Tabs::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            hcpp::i32 tabs_w = 0;
            for (hcpp::size_t i = 0; i < count_; ++i) {
                const Size text =
                    face != nullptr ? text_extent(labels_[i], *face) : Size{};
                tabs_w += text.width + 24;
            }
            hcpp::i32 content_w = 0;
            hcpp::i32 content_h = 0;
            for (hcpp::size_t i = 0; i < count_; ++i) {
                if (pages_[i] == nullptr) {
                    continue;
                }
                const Size page = pages_[i]->measure(available);
                if (page.width > content_w) {
                    content_w = page.width;
                }
                if (page.height > content_h) {
                    content_h = page.height;
                }
            }
            desired = {tabs_w > content_w ? tabs_w : content_w,
                       strip_height() + content_h};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Tabs::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        apply_visibility();
        const Rect content = content_rect();
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (pages_[i] != nullptr) {
                pages_[i]->layout_tree(content);
            }
        }
    }

    void Tabs::paint(Canvas& canvas) noexcept {
        const Rect b = bounds();
        if (b.empty() || count_ == 0) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 strip = strip_height();
        canvas.draw_line(b.x, b.y + strip - 1, b.right() - 1, b.y + strip - 1,
                         canvas.color(theme.border));
        const hcpp::i32 tab_w = b.w / static_cast<hcpp::i32>(count_);
        for (hcpp::size_t i = 0; i < count_; ++i) {
            const hcpp::i32 tx = b.x + static_cast<hcpp::i32>(i) * tab_w;
            const bool current = i == active_;
            if (current) {
                canvas.fill_rect(
                    Rect{tx + 4, b.y + strip - 2, tab_w - 8, 2},
                    canvas.color(theme.accent));
            }
            if (face == nullptr || labels_[i].empty()) {
                continue;
            }
            const Size text = text_extent(labels_[i], *face);
            hcpp::i32 text_x = tx + (tab_w - text.width) / 2;
            if (text_x < tx + 4) {
                text_x = tx + 4;
            }
            hcpp::i32 offset = (strip - 4 - face->height) / 2;
            if (offset < 0) {
                offset = 0;
            }
            canvas.draw_text(text_x, b.y + offset + face->baseline, labels_[i],
                             canvas.color(current ? theme.fg : theme.muted_fg),
                             *face);
        }
        if (focused_) {
            draw_focus_ring(canvas, b);
        }
    }

    void Tabs::apply_visibility() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (pages_[i] != nullptr) {
                pages_[i]->set_visible(i == active_);
            }
        }
    }

}  // namespace hype::ui
