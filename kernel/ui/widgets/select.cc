#include "kernel/ui/widgets/select.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 pad_x = 10;
        constexpr hcpp::i32 chevron_w = 8;
        constexpr hcpp::i32 chevron_h = 5;
        constexpr hcpp::i32 chevron_gap = 12;

    }  // namespace

    Select::Select(hcpp::StringView placeholder) noexcept
        : placeholder_(placeholder) {}

    bool Select::add_option(hcpp::StringView label) noexcept {
        if (count_ >= max_options) {
            return false;
        }
        options_[count_++] = label;
        return true;
    }

    void Select::clear_options() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            options_[i] = {};
        }
        count_ = 0;
        selected_ = no_selection;
        close();
    }

    hcpp::size_t Select::option_count() const noexcept { return count_; }

    bool Select::set_selected(hcpp::size_t index) noexcept {
        if (index >= count_) {
            return false;
        }
        if (selected_ == index) {
            return true;
        }
        selected_ = index;
        return true;
    }

    hcpp::size_t Select::selected() const noexcept { return selected_; }

    hcpp::StringView Select::value() const noexcept {
        if (selected_ == no_selection || selected_ >= count_) {
            return placeholder_;
        }
        return options_[selected_];
    }

    void Select::set_placeholder(hcpp::StringView text) noexcept {
        placeholder_ = text;
    }

    void Select::set_enabled(bool enabled) noexcept {
        enabled_ = enabled;
        if (!enabled_) {
            pressed_ = false;
            close();
        }
    }

    bool Select::enabled() const noexcept { return enabled_; }

    void Select::set_screen(Rect screen) noexcept {
        menu_.set_screen(screen);
    }

    void Select::open() noexcept {
        if (!enabled_ || count_ == 0 || is_open()) {
            return;
        }
        menu_.clear();
        for (hcpp::size_t i = 0; i < count_; ++i) {
            menu_.add_item(options_[i]);
            menu_.set_item_checked(i, i == selected_);
        }
        menu_.set_on_select(&Select::on_menu_select, this);
        menu_.open(bounds());
    }

    void Select::close() noexcept {
        if (is_open()) {
            menu_.close();
        }
    }

    bool Select::is_open() const noexcept { return menu_.is_open(); }

    Rect Select::popup_rect() const noexcept {
        return menu_.menu_rect();
    }

    bool Select::hovered() const noexcept { return hovered_; }

    void Select::set_on_change(SelectCallback callback,
                               void* context) noexcept {
        on_change_ = callback;
        change_ctx_ = context;
    }

    void Select::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
        menu_.set_font(font);
    }

    bool Select::handle_mouse(Point point, SelectMouse event) noexcept {
        if (!enabled_) {
            return false;
        }
        if (is_open()) {
            const MenuMouse mapped =
                event == SelectMouse::Move     ? MenuMouse::Move
                : event == SelectMouse::Down   ? MenuMouse::Down
                                               : MenuMouse::Up;
            const bool before = menu_.is_open();
            const bool changed = menu_.handle_mouse(point, mapped);
            if (before && !menu_.is_open()) {
                pressed_ = false;
            }
            return changed || !menu_.is_open();
        }
        const Rect box = bounds();
        if (event == SelectMouse::Move) {
            const bool over = box.contains(point);
            if (over == hovered_) {
                return false;
            }
            hovered_ = over;
            return true;
        }
        if (event == SelectMouse::Down) {
            if (!box.contains(point)) {
                return false;
            }
            pressed_ = true;
            return true;
        }
        // Up: commit only when the press began on the trigger and the
        // release stays inside it (state machine COMMIT rule).
        const bool commit = pressed_ && box.contains(point);
        pressed_ = false;
        if (commit) {
            open();
        }
        return commit || box.contains(point);
    }

    void Select::set_focused(bool focused) noexcept {
        focused_ = focused;
    }

    bool Select::focused() const noexcept { return focused_; }

    bool Select::on_key(char key) noexcept {
        if (is_open()) {
            const bool handled = menu_.handle_key(key);
            return handled || !menu_.is_open();
        }
        if (!enabled_) {
            return false;
        }
        if (key == keys::enter || key == keys::space ||
            key == keys::down) {
            open();
            return is_open();
        }
        return false;
    }

    Rect Select::focus_ring_rect() const noexcept { return bounds(); }

    Size Select::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0) {
            desired.width = 160;
            const hcpp::fonts::FontFace* face = resolved_font();
            if (face != nullptr) {
                for (hcpp::size_t i = 0; i < count_; ++i) {
                    const hcpp::i32 w =
                        pad_x * 2 + chevron_gap +
                        text_extent(options_[i], *face).width;
                    if (w > desired.width) {
                        desired.width = w;
                    }
                }
            }
        }
        if (desired.height <= 0) {
            desired.height = trigger_height;
        }
        if (available.width > 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height > 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Select::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();

        const hcpp::u32 fill = !enabled_  ? theme.muted
                               : pressed_ ? theme.accent_active
                                          : theme.secondary;
        const hcpp::u32 edge =
            !enabled_          ? theme.border
            : focused_         ? theme.ring
                               : (hovered_ ? theme.border_strong
                                           : theme.border);
        rui_control_bg(canvas, box, fill, edge);

        if (face != nullptr) {
            const hcpp::StringView text = value();
            const bool dim = selected_ == no_selection || !enabled_;
            const Rect saved_clip = canvas.clip();
            canvas.set_clip(saved_clip.intersection(
                Rect{box.x + pad_x, box.y,
                     box.w - pad_x * 2 - chevron_gap, box.h}));
            const hcpp::i32 base =
                box.y + (box.h - face->height) / 2 + face->baseline;
            canvas.draw_text(box.x + pad_x, base, text,
                             canvas.color(dim ? theme.muted_fg
                                              : theme.fg),
                             *face);
            canvas.set_clip(saved_clip);
        }
        // Chevron: a small downward V right-aligned in the trigger.
        const hcpp::i32 vx = box.right() - pad_x - chevron_w;
        const hcpp::i32 vy = box.y + box.h / 2 - chevron_h / 2 + 1;
        const hcpp::u32 chevron = canvas.color(theme.muted_fg);
        canvas.draw_line(vx, vy, vx + chevron_w / 2, vy + chevron_h,
                         chevron);
        canvas.draw_line(vx + chevron_w / 2, vy + chevron_h,
                         vx + chevron_w, vy, chevron);

        // The open popup paints above and outside the trigger rect.
        menu_.paint(canvas);
    }

    void Select::on_menu_select(DropdownMenu& menu, hcpp::size_t index,
                                void* context) noexcept {
        Select* self = static_cast<Select*>(context);
        if (self == nullptr || index >= self->count_) {
            return;
        }
        (void)menu;
        self->selected_ = index;
        if (self->on_change_ != nullptr) {
            self->on_change_(*self, index, self->change_ctx_);
        }
    }

    const hcpp::fonts::FontFace* Select::resolved_font() const noexcept {
        return resolve_font(font_);
    }

}  // namespace hype::ui
