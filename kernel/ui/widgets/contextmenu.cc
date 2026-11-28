#include "kernel/ui/widgets/contextmenu.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    bool ContextMenu::open_at(Point point, const Item* items,
                              hcpp::u32 count, CtxMenuPick pick,
                              void* user) noexcept {
        if (items == nullptr || count == 0 || pick == nullptr) {
            return false;
        }
        if (count > DropdownMenu::max_items) {
            count = DropdownMenu::max_items;
        }
        menu_.clear();
        for (hcpp::u32 i = 0; i < count; ++i) {
            items_[i] = items[i];
            const Item& it = items[i];
            if ((it.flags & menu_f_separator) != 0) {
                menu_.add_separator();
                continue;
            }
            if (!menu_.add_item(it.label != nullptr ? it.label : "")) {
                return false;
            }
            // Flag bits are shared with DropdownMenu (same values), so
            // disabled/checked/destructive carry straight through.
            menu_.set_item_flags(i, it.flags);
        }
        pick_ = pick;
        user_ = user;
        menu_.set_on_select(&ContextMenu::on_menu_select, this);
        menu_.set_screen(screen_);
        menu_.open_at(point);
        set_visible(true);
        return true;
    }

    void ContextMenu::close() noexcept {
        menu_.close();
        pick_ = nullptr;
        user_ = nullptr;
        set_visible(false);
    }

    bool ContextMenu::is_open() const noexcept { return menu_.is_open(); }

    void ContextMenu::set_screen(Rect screen) noexcept {
        screen_ = screen;
        menu_.set_screen(screen);
    }

    Rect ContextMenu::menu_rect() const noexcept {
        return menu_.menu_rect();
    }

    hcpp::size_t ContextMenu::highlighted() const noexcept {
        return menu_.highlighted();
    }

    const DropdownMenu& ContextMenu::menu() const noexcept {
        return menu_;
    }

    void ContextMenu::set_font(
        const hcpp::fonts::FontFace* font) noexcept {
        menu_.set_font(font);
    }

    bool ContextMenu::handle_mouse(Point point,
                                   MenuMouse event) noexcept {
        return menu_.handle_mouse(point, event);
    }

    bool ContextMenu::handle_key(char ascii) noexcept {
        return menu_.handle_key(ascii);
    }

    Size ContextMenu::measure(Size available) noexcept {
        (void)available;
        const Size size = menu_rect().size();
        return size.empty() ? preferred_size() : size;
    }

    void ContextMenu::layout(const Rect& rect) noexcept {
        (void)rect;
        set_bounds(menu_.menu_rect());
    }

    void ContextMenu::paint(Canvas& canvas) noexcept {
        if (!menu_.is_open()) {
            return;
        }
        menu_.paint(canvas);
    }

    void ContextMenu::on_menu_select(DropdownMenu& menu,
                                     hcpp::size_t index,
                                     void* context) noexcept {
        ContextMenu* self = static_cast<ContextMenu*>(context);
        if (self == nullptr || index >= DropdownMenu::max_items ||
            self->pick_ == nullptr) {
            return;
        }
        (void)menu;
        self->pick_(self->items_[index].id, self->user_);
    }

}  // namespace hype::ui
