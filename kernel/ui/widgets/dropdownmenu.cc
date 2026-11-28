#include "kernel/ui/widgets/dropdownmenu.h"

#include "kernel/ui/focus.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 pad_y = 6;      // content top/bottom inset
        constexpr hcpp::i32 pad_x = 8;      // content side inset
        constexpr hcpp::i32 icon_slot = 20; // reserved check/icon column
        constexpr hcpp::i32 shortcut_pad = 16;
        constexpr hcpp::i32 radius = 8;
        constexpr char escape_ascii = 27;

        char lower_ascii(char c) noexcept {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        }

    }  // namespace

    bool DropdownMenu::add_item(hcpp::StringView label) noexcept {
        return add_item({}, label, {});
    }

    bool DropdownMenu::add_item(hcpp::StringView icon,
                                hcpp::StringView label,
                                hcpp::StringView shortcut) noexcept {
        if (count_ >= max_items) {
            return false;
        }
        items_[count_] = {icon, label, shortcut, 0};
        ++count_;
        return true;
    }

    bool DropdownMenu::add_heading(hcpp::StringView text) noexcept {
        if (count_ >= max_items) {
            return false;
        }
        items_[count_] = {{}, text, {}, menu_f_heading};
        ++count_;
        return true;
    }

    bool DropdownMenu::add_separator() noexcept {
        if (count_ >= max_items) {
            return false;
        }
        items_[count_] = {{}, {}, {}, menu_f_separator};
        ++count_;
        return true;
    }

    void DropdownMenu::clear() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            items_[i] = {};
        }
        count_ = 0;
        highlighted_ = no_item;
    }

    hcpp::size_t DropdownMenu::item_count() const noexcept { return count_; }

    const MenuItem& DropdownMenu::item_at(hcpp::size_t index) const noexcept {
        return items_[index < count_ ? index : 0];
    }

    bool DropdownMenu::set_item_flags(hcpp::size_t index,
                                      hcpp::u8 flags) noexcept {
        if (index >= count_) {
            return false;
        }
        items_[index].flags = flags;
        return true;
    }

    bool DropdownMenu::set_item_enabled(hcpp::size_t index,
                                        bool enabled) noexcept {
        if (index >= count_) {
            return false;
        }
        if (enabled) {
            items_[index].flags &= ~menu_f_disabled;
        } else {
            items_[index].flags |= menu_f_disabled;
        }
        return true;
    }

    bool DropdownMenu::set_item_checked(hcpp::size_t index,
                                        bool checked) noexcept {
        if (index >= count_) {
            return false;
        }
        if (checked) {
            items_[index].flags |= menu_f_checked;
        } else {
            items_[index].flags &= ~menu_f_checked;
        }
        return true;
    }

    void DropdownMenu::open(Rect anchor) noexcept {
        rect_ = {anchor.x, anchor.bottom(),
                 content_width(), content_height()};
        if (!screen_.empty()) {
            if (rect_.bottom() > screen_.bottom() &&
                anchor.y - rect_.h >= screen_.y) {
                rect_.y = anchor.y - rect_.h;
            }
            if (rect_.x + rect_.w > screen_.right()) {
                rect_.x = screen_.right() - rect_.w;
            }
            if (rect_.x < screen_.x) {
                rect_.x = screen_.x;
            }
            if (rect_.bottom() > screen_.bottom()) {
                rect_.h = screen_.bottom() - rect_.y;
            }
        }
        open_ = true;
        // First selectable row starts highlighted — keyboard parity
        // with pointer hover (spec: arrows move the highlight).
        highlighted_ = no_item;
        move_highlight(1);
    }

    void DropdownMenu::open_at(Point point) noexcept {
        open(Rect{point.x, point.y, 1, 0});
    }

    void DropdownMenu::close() noexcept {
        open_ = false;
        highlighted_ = no_item;
    }

    bool DropdownMenu::is_open() const noexcept { return open_; }

    void DropdownMenu::set_screen(Rect screen) noexcept { screen_ = screen; }

    Rect DropdownMenu::screen() const noexcept { return screen_; }

    Rect DropdownMenu::menu_rect() const noexcept { return rect_; }

    bool DropdownMenu::hit_test(Point point) const noexcept {
        return open_ && rect_.contains(point);
    }

    hcpp::size_t DropdownMenu::item_at_point(Point point) const noexcept {
        if (!open_ || !rect_.contains(point)) {
            return no_item;
        }
        hcpp::i32 y = rect_.y + pad_y;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            const hcpp::i32 h = row_height(i);
            if (point.y >= y && point.y < y + h) {
                return i;
            }
            y += h;
        }
        return no_item;
    }

    hcpp::size_t DropdownMenu::highlighted() const noexcept {
        return highlighted_;
    }

    void DropdownMenu::set_on_select(MenuCallback callback,
                                     void* context) noexcept {
        on_select_ = callback;
        select_ctx_ = context;
    }

    bool DropdownMenu::handle_mouse(Point point, MenuMouse event) noexcept {
        if (!open_) {
            return false;
        }
        if (event == MenuMouse::Move) {
            const hcpp::size_t over = item_at_point(point);
            const hcpp::size_t next =
                over != no_item && selectable(over) ? over : no_item;
            if (next != highlighted_) {
                highlighted_ = next;
                return true;
            }
            return false;
        }
        if (event == MenuMouse::Down) {
            if (!rect_.contains(point)) {
                close();
                return true;
            }
            const hcpp::size_t over = item_at_point(point);
            if (over != highlighted_ && over != no_item && selectable(over)) {
                highlighted_ = over;
                return true;
            }
            return false;
        }
        // Up: commit a selectable item; outside -> already closed on Down,
        // but an Up outside without a prior Down still dismisses.
        if (!rect_.contains(point)) {
            close();
            return true;
        }
        const hcpp::size_t over = item_at_point(point);
        if (over != no_item && selectable(over)) {
            activate(over);
        }
        return true;
    }

    bool DropdownMenu::handle_key(char key) noexcept {
        if (!open_) {
            return false;
        }
        if (key == escape_ascii) {
            close();
            return true;
        }
        if (key == keys::up) {
            return move_highlight(-1);
        }
        if (key == keys::down) {
            return move_highlight(1);
        }
        if (key == keys::enter || key == '\r') {
            if (highlighted_ != no_item) {
                return activate(highlighted_);
            }
            return true;
        }
        if (key >= 0x20 && key <= 0x7E) {
            const char want = lower_ascii(key);
            for (hcpp::size_t step = 1; step <= count_; ++step) {
                const hcpp::size_t index =
                    (highlighted_ == no_item ? step - 1
                                             : highlighted_ + step) %
                    count_;
                if (selectable(index) && !items_[index].label.empty() &&
                    lower_ascii(items_[index].label[0]) == want) {
                    highlighted_ = index;
                    return true;
                }
            }
            return true;
        }
        return false;
    }

    void DropdownMenu::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* DropdownMenu::font() const noexcept {
        return font_;
    }

    void DropdownMenu::paint(Canvas& canvas) const noexcept {
        if (!open_ || rect_.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();

        rui_shadow(canvas, rect_, radius, 16, 120);
        canvas.fill_rounded_rect(rect_, radius, canvas.color(theme.card));
        canvas.stroke_rounded_rect(rect_, radius,
                                   canvas.color(theme.border));

        hcpp::i32 y = rect_.y + pad_y;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            const MenuItem& item = items_[i];
            const hcpp::i32 h = row_height(i);
            const Rect row{rect_.x + pad_x, y, rect_.w - pad_x * 2, h};
            if (row.bottom() > rect_.bottom()) {
                break;
            }

            if (item.flags & menu_f_separator) {
                canvas.fill_rect({row.x + pad_x, y + h / 2,
                                  row.w - pad_x * 2, 1},
                                 canvas.color(theme.border));
                y += h;
                continue;
            }
            if (item.flags & menu_f_heading) {
                if (face != nullptr) {
                    const hcpp::i32 base =
                        y + (h - face->height) / 2 + face->baseline;
                    canvas.draw_text(row.x + pad_x, base, item.label,
                                     canvas.color(theme.muted_fg), *face);
                }
                y += h;
                continue;
            }

            const bool disabled = (item.flags & menu_f_disabled) != 0;
            const bool danger = (item.flags & menu_f_destructive) != 0;
            if (i == highlighted_ && !disabled) {
                const Rect wash{row.x - 2, y, row.w + 4, h};
                if (danger) {
                    canvas.fill_rect_alpha(wash,
                                           canvas.color(theme.destructive),
                                           48);
                } else {
                    canvas.fill_rounded_rect(wash, 4,
                                             canvas.color(theme.secondary));
                }
            }

            // Icon/check slot — always reserved so labels align.
            const hcpp::i32 slot_cx = row.x + pad_x / 2 + icon_slot / 2;
            const hcpp::i32 cy = y + h / 2;
            if (item.flags & menu_f_checked) {
                const hcpp::u32 mark = canvas.color(theme.accent);
                canvas.draw_line(slot_cx - 4, cy, slot_cx - 1, cy + 3, mark);
                canvas.draw_line(slot_cx - 1, cy + 3, slot_cx + 4, cy - 4,
                                 mark);
                canvas.draw_line(slot_cx - 4, cy + 1, slot_cx - 1, cy + 4,
                                 mark);
                canvas.draw_line(slot_cx - 1, cy + 4, slot_cx + 4, cy - 3,
                                 mark);
            } else if (face != nullptr && !item.icon.empty()) {
                canvas.draw_text(row.x + pad_x / 2,
                                 cy - face->height / 2 + face->baseline,
                                 item.icon, canvas.color(theme.muted_fg),
                                 *face);
            }

            if (face != nullptr) {
                const hcpp::i32 base =
                    y + (h - face->height) / 2 + face->baseline;
                const hcpp::u32 label_rgb =
                    disabled      ? theme.muted_fg
                    : danger      ? theme.destructive
                                  : theme.fg;
                canvas.draw_text(row.x + pad_x / 2 + icon_slot, base,
                                 item.label, canvas.color(label_rgb), *face);
                if (!item.shortcut.empty()) {
                    const Size sc = text_extent(item.shortcut, *face);
                    canvas.draw_text(row.right() - pad_x - sc.width, base,
                                     item.shortcut,
                                     canvas.color(theme.muted_fg), *face);
                }
            }
            y += h;
        }
    }

    const hcpp::fonts::FontFace* DropdownMenu::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 DropdownMenu::row_height(hcpp::size_t index) const noexcept {
        const hcpp::u8 flags = items_[index].flags;
        if (flags & menu_f_separator) {
            return separator_height;
        }
        if (flags & menu_f_heading) {
            return heading_height;
        }
        return item_height;
    }

    Rect DropdownMenu::row_rect(hcpp::size_t index) const noexcept {
        hcpp::i32 y = rect_.y + pad_y;
        for (hcpp::size_t i = 0; i < index && i < count_; ++i) {
            y += row_height(i);
        }
        return {rect_.x, y, rect_.w, row_height(index)};
    }

    bool DropdownMenu::selectable(hcpp::size_t index) const noexcept {
        if (index >= count_) {
            return false;
        }
        const hcpp::u8 flags = items_[index].flags;
        return (flags & (menu_f_separator | menu_f_heading |
                         menu_f_disabled)) == 0;
    }

    bool DropdownMenu::move_highlight(hcpp::i32 delta) noexcept {
        if (count_ == 0) {
            return false;
        }
        hcpp::size_t index = highlighted_;
        for (hcpp::size_t step = 0; step < count_; ++step) {
            if (index == no_item) {
                index = delta > 0 ? 0 : count_ - 1;
            } else {
                index = (index + count_ + static_cast<hcpp::size_t>(delta)) %
                        count_;
            }
            if (selectable(index)) {
                if (index != highlighted_) {
                    highlighted_ = index;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    bool DropdownMenu::activate(hcpp::size_t index) noexcept {
        if (!selectable(index)) {
            return false;
        }
        close();
        if (on_select_ != nullptr) {
            on_select_(*this, index, select_ctx_);
        }
        return true;
    }

    hcpp::i32 DropdownMenu::content_height() const noexcept {
        hcpp::i32 h = pad_y * 2;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            h += row_height(i);
        }
        return h;
    }

    hcpp::i32 DropdownMenu::content_width() const noexcept {
        const hcpp::fonts::FontFace* face = resolved_font();
        hcpp::i32 widest = min_width;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (items_[i].flags & menu_f_separator) {
                continue;
            }
            hcpp::i32 w = pad_x * 2 + pad_x / 2 + icon_slot;
            if (face != nullptr) {
                w += text_extent(items_[i].label, *face).width;
                if (!items_[i].shortcut.empty()) {
                    w += shortcut_pad +
                         text_extent(items_[i].shortcut, *face).width;
                }
            }
            if (w > widest) {
                widest = w;
            }
        }
        return widest;
    }

}  // namespace hype::ui
