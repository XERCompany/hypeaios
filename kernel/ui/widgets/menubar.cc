#include "kernel/ui/widgets/menubar.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 title_pad_x = 12;
        constexpr hcpp::i32 title_gap = 2;
        constexpr hcpp::i32 title_radius = 4;
        constexpr hcpp::i32 title_min_w = 36;

    }  // namespace

    bool MenuBar::add_menu(hcpp::StringView title) noexcept {
        if (count_ >= max_menus) {
            return false;
        }
        titles_[count_++] = title;
        return true;
    }

    void MenuBar::clear_menus() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            titles_[i] = {};
            title_rects_[i] = {};
        }
        count_ = 0;
        close_menu();
        hover_index_ = no_menu;
    }

    hcpp::size_t MenuBar::menu_count() const noexcept { return count_; }

    void MenuBar::set_on_menu(MenuBarFillCallback callback,
                              void* context) noexcept {
        on_menu_ = callback;
        menu_ctx_ = context;
    }

    void MenuBar::set_on_select(MenuBarSelectCallback callback,
                                void* context) noexcept {
        on_select_ = callback;
        select_ctx_ = context;
    }

    void MenuBar::set_screen(Rect screen) noexcept {
        screen_ = screen;
        menu_.set_screen(screen);
    }

    void MenuBar::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
        menu_.set_font(font);
    }

    bool MenuBar::open_menu(hcpp::size_t index) noexcept {
        if (index >= count_ || on_menu_ == nullptr) {
            return false;
        }
        menu_.clear();
        on_menu_(*this, index, menu_, menu_ctx_);
        if (menu_.item_count() == 0) {
            return false;
        }
        menu_.set_on_select(&MenuBar::on_menu_pick, this);
        menu_.set_screen(screen_);
        menu_.open(title_rects_[index]);
        open_index_ = index;
        return true;
    }

    void MenuBar::close_menu() noexcept {
        if (menu_.is_open()) {
            menu_.close();
        }
        open_index_ = no_menu;
    }

    bool MenuBar::is_open() const noexcept {
        return open_index_ != no_menu && menu_.is_open();
    }

    hcpp::size_t MenuBar::open_menu_index() const noexcept {
        return open_index_;
    }

    Rect MenuBar::title_rect(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return {};
        }
        return title_rects_[index];
    }

    hcpp::size_t MenuBar::title_at(Point point) const noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (title_rects_[i].contains(point)) {
                return i;
            }
        }
        return no_menu;
    }

    Rect MenuBar::popup_rect() const noexcept {
        return menu_.menu_rect();
    }

    bool MenuBar::handle_mouse(Point point, MenuMouse event) noexcept {
        if (count_ == 0) {
            return false;
        }
        const hcpp::size_t over = title_at(point);
        if (event == MenuMouse::Move) {
            bool changed = false;
            if (over != hover_index_) {
                hover_index_ = over;
                changed = true;
            }
            // Menu-bar tracking: while a popup is up, gliding across a
            // sibling title switches menus.
            if (is_open() && over != no_menu && over != open_index_) {
                open_menu(over);
                return true;
            }
            if (is_open() && menu_.handle_mouse(point, MenuMouse::Move)) {
                changed = true;
            }
            return changed;
        }
        if (event == MenuMouse::Down) {
            if (over != no_menu) {
                if (is_open() && over == open_index_) {
                    close_menu();
                    return true;
                }
                open_menu(over);
                return true;
            }
            if (is_open()) {
                menu_.handle_mouse(point, MenuMouse::Down);
                if (!menu_.is_open()) {
                    open_index_ = no_menu;
                }
                return true;
            }
            return false;
        }
        // Up: commits through the open popup, or toggles open on a
        // release that started elsewhere.
        if (is_open()) {
            menu_.handle_mouse(point, MenuMouse::Up);
            if (!menu_.is_open()) {
                open_index_ = no_menu;
            }
            return true;
        }
        if (over != no_menu) {
            open_menu(over);
            return true;
        }
        return false;
    }

    bool MenuBar::handle_key(char ascii) noexcept {
        if (!is_open()) {
            return false;
        }
        if (ascii == keys::left || ascii == keys::right) {
            const hcpp::i32 dir = ascii == keys::right ? 1 : -1;
            const hcpp::size_t next = static_cast<hcpp::size_t>(
                (static_cast<hcpp::i32>(open_index_) + dir +
                 static_cast<hcpp::i32>(count_)) %
                static_cast<hcpp::i32>(count_));
            open_menu(next);
            return true;
        }
        const bool was_open = menu_.is_open();
        const bool handled = menu_.handle_key(ascii);
        if (was_open && !menu_.is_open()) {
            open_index_ = no_menu;
        }
        return handled;
    }

    Size MenuBar::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0) {
            const hcpp::fonts::FontFace* face = resolved_font();
            hcpp::i32 w = title_gap;
            for (hcpp::size_t i = 0; i < count_; ++i) {
                hcpp::i32 tw = title_min_w;
                if (face != nullptr) {
                    tw = text_extent(titles_[i], *face).width +
                         title_pad_x * 2;
                    if (tw < title_min_w) {
                        tw = title_min_w;
                    }
                }
                w += tw + title_gap;
            }
            desired.width = w;
        }
        if (desired.height <= 0) {
            desired.height = bar_height;
        }
        if (available.width > 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height > 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void MenuBar::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        layout_titles();
    }

    void MenuBar::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();

        canvas.fill_rect(box, canvas.color(theme.card));
        canvas.fill_rect(Rect{box.x, box.bottom() - 1, box.w, 1},
                         canvas.color(theme.border));

        for (hcpp::size_t i = 0; i < count_; ++i) {
            const Rect tr = title_rects_[i];
            if (tr.empty() || !tr.intersects(box)) {
                continue;
            }
            if (i == open_index_ || i == hover_index_) {
                canvas.fill_rounded_rect(
                    tr.inset(Insets::symmetric(0, 2)), title_radius,
                    canvas.color(theme.secondary));
            }
            if (face != nullptr) {
                const hcpp::i32 base =
                    tr.y + (tr.h - face->height) / 2 + face->baseline;
                const Rect saved = canvas.clip();
                canvas.set_clip(saved.intersection(tr));
                canvas.draw_text(tr.x + title_pad_x, base, titles_[i],
                                 canvas.color(theme.fg), *face);
                canvas.set_clip(saved);
            }
        }
        menu_.paint(canvas);
    }

    void MenuBar::on_menu_pick(DropdownMenu& menu, hcpp::size_t index,
                               void* context) noexcept {
        MenuBar* self = static_cast<MenuBar*>(context);
        if (self == nullptr) {
            return;
        }
        (void)menu;
        const hcpp::size_t which = self->open_index_;
        if (self->on_select_ != nullptr && which != no_menu) {
            self->on_select_(*self, which, index, self->select_ctx_);
        }
    }

    const hcpp::fonts::FontFace* MenuBar::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    void MenuBar::layout_titles() noexcept {
        const Rect box = bounds();
        const hcpp::fonts::FontFace* face = resolved_font();
        hcpp::i32 x = box.x + title_gap;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            hcpp::i32 tw = title_min_w;
            if (face != nullptr) {
                tw = text_extent(titles_[i], *face).width +
                     title_pad_x * 2;
                if (tw < title_min_w) {
                    tw = title_min_w;
                }
            }
            title_rects_[i] = {x, box.y, tw, box.h};
            x += tw + title_gap;
        }
    }

}  // namespace hype::ui
