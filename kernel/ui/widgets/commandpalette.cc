#include "kernel/ui/widgets/commandpalette.h"

#include "kernel/ui/focus.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 field_h = 34;
        constexpr hcpp::i32 icon_slot = 24;
        constexpr hcpp::i32 hint_pad = 10;
        constexpr hcpp::i32 caret_w = 1;

        char lower_ascii(char c) noexcept {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        }

        bool word_break(char c) noexcept {
            return c == ' ' || c == '-' || c == '_' || c == '/' ||
                   c == '.' || c == ':';
        }

    }  // namespace

    bool CommandPalette::add_item(hcpp::StringView icon,
                                  hcpp::StringView label,
                                  hcpp::StringView hint,
                                  hcpp::u32 id) noexcept {
        if (count_ >= max_items) {
            return false;
        }
        items_[count_] = {icon, label, hint, id};
        ++count_;
        return true;
    }

    void CommandPalette::clear_items() noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            items_[i] = {};
        }
        count_ = 0;
        result_count_ = 0;
        selected_ = 0;
    }

    hcpp::size_t CommandPalette::item_count() const noexcept {
        return count_;
    }

    void CommandPalette::reset() noexcept {
        query_[0] = '\0';
        query_len_ = 0;
        selected_ = 0;
        rebuild();
    }

    hcpp::StringView CommandPalette::query() const noexcept {
        return hcpp::StringView{query_, query_len_};
    }

    hcpp::size_t CommandPalette::query_length() const noexcept {
        return query_len_;
    }

    hcpp::size_t CommandPalette::result_count() const noexcept {
        return result_count_;
    }

    const PaletteItem& CommandPalette::result_item(
        hcpp::size_t row) const noexcept {
        const hcpp::size_t index =
            row < result_count_ ? result_idx_[row] : 0;
        return items_[index];
    }

    hcpp::size_t CommandPalette::selected() const noexcept {
        return selected_;
    }

    hcpp::i32 CommandPalette::fuzzy_score(hcpp::StringView query,
                                          hcpp::StringView text) noexcept {
        if (query.empty()) {
            return 0;
        }
        if (text.empty()) {
            return -1;
        }
        hcpp::size_t qi = 0;
        hcpp::i32 score = 0;
        hcpp::size_t prev = static_cast<hcpp::size_t>(-1);
        for (hcpp::size_t ti = 0; ti < text.size() && qi < query.size();
             ++ti) {
            if (lower_ascii(text[ti]) != lower_ascii(query[qi])) {
                continue;
            }
            score += 1;
            if (prev != static_cast<hcpp::size_t>(-1) && ti == prev + 1) {
                score += 3;  // consecutive-hit bonus
            }
            const bool boundary =
                ti == 0 || word_break(text[ti - 1]) ||
                (text[ti] >= 'A' && text[ti] <= 'Z' &&
                 text[ti - 1] >= 'a' && text[ti - 1] <= 'z');
            if (boundary) {
                score += 2;
            }
            prev = ti;
            ++qi;
        }
        return qi == query.size() ? score : -1;
    }

    void CommandPalette::set_on_accept(PaletteCallback callback,
                                       void* context) noexcept {
        on_accept_ = callback;
        accept_ctx_ = context;
    }

    void CommandPalette::set_on_dismiss(PaletteDismiss callback,
                                        void* context) noexcept {
        on_dismiss_ = callback;
        dismiss_ctx_ = context;
    }

    bool CommandPalette::accept() noexcept {
        if (result_count_ == 0 || selected_ >= result_count_) {
            return false;
        }
        const hcpp::u32 id = items_[result_idx_[selected_]].id;
        if (on_accept_ != nullptr) {
            on_accept_(*this, id, accept_ctx_);
        }
        return true;
    }

    bool CommandPalette::handle_mouse(Point point,
                                      PaletteMouse event) noexcept {
        const Rect box = bounds();
        if (event == PaletteMouse::Move) {
            const hcpp::size_t visible =
                result_count_ < max_rows ? result_count_ : max_rows;
            hcpp::size_t over = no_row;
            for (hcpp::size_t i = 0; i < visible; ++i) {
                if (row_rect(i).contains(point)) {
                    over = i;
                    break;
                }
            }
            if (over != no_row && over != selected_) {
                selected_ = over;
                return true;
            }
            return false;
        }
        if (event == PaletteMouse::Down) {
            if (!box.contains(point) && on_dismiss_ != nullptr) {
                on_dismiss_(*this, dismiss_ctx_);
                return true;
            }
            return box.contains(point);
        }
        // Up: commit the row under the pointer.
        if (!box.contains(point)) {
            return false;
        }
        const hcpp::size_t visible =
            result_count_ < max_rows ? result_count_ : max_rows;
        for (hcpp::size_t i = 0; i < visible; ++i) {
            if (row_rect(i).contains(point)) {
                selected_ = i;
                accept();
                return true;
            }
        }
        return true;
    }

    bool CommandPalette::handle_key(char ascii) noexcept {
        if (ascii == 27) {
            // Esc: first press clears a live query; an empty query lets
            // the host close the panel (OverlayHost closes on Esc).
            if (query_len_ > 0) {
                query_[0] = '\0';
                query_len_ = 0;
                selected_ = 0;
                rebuild();
            }
            return true;
        }
        if (ascii == keys::up) {
            return move_selection(-1);
        }
        if (ascii == keys::down) {
            return move_selection(1);
        }
        if (ascii == keys::enter || ascii == '\r') {
            return accept();
        }
        if (ascii == '\b') {
            return pop_char();
        }
        if (ascii >= 0x20 && ascii <= 0x7E) {
            return push_char(ascii);
        }
        return false;
    }

    void CommandPalette::set_font(
        const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    Size CommandPalette::measure(Size available) noexcept {
        const hcpp::size_t visible =
            result_count_ < max_rows ? result_count_ : max_rows;
        const bool empty_row = result_count_ == 0;
        hcpp::i32 h = pad * 2 + field_h + list_gap;
        h += static_cast<hcpp::i32>(visible) * row_height;
        if (empty_row) {
            h += row_height;
        }
        if (!empty_row && visible == 0) {
            h -= list_gap;
        }
        hcpp::i32 w = panel_width;
        if (available.width > 0 && w > available.width) {
            w = available.width;
        }
        if (available.height > 0 && h > available.height) {
            h = available.height;
        }
        return {w, h};
    }

    void CommandPalette::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();

        rui_panel(canvas, box, rui_elev_dialog);

        // Input row: sunk field with an accent edge + magnifier glyph.
        const Rect field = input_rect();
        rui_control_bg(canvas, field, theme.surface_sunk, theme.accent);
        const hcpp::i32 cy = field.y + field.h / 2;
        const hcpp::u32 muted = canvas.color(theme.muted_fg);
        canvas.stroke_circle(field.x + 14, cy - 1, 4, muted);
        canvas.draw_line(field.x + 17, cy + 2, field.x + 20, cy + 5, muted);

        const hcpp::i32 tx = field.x + 30;
        const Rect saved_clip = canvas.clip();
        canvas.set_clip(saved_clip.intersection(
            Rect{tx, field.y, field.right() - tx - 6, field.h}));
        if (face != nullptr) {
            const hcpp::i32 base =
                field.y + (field.h - face->height) / 2 + face->baseline;
            if (query_len_ == 0) {
                canvas.draw_text(tx, base, "Type a command\xE2\x80\xA6",
                                 muted, *face);
            } else {
                canvas.draw_text(tx, base, query(),
                                 canvas.color(theme.fg), *face);
            }
        }
        canvas.set_clip(saved_clip);
        if (face != nullptr) {
            hcpp::i32 cx =
                tx + text_extent(query(), *face).width + 1;
            if (cx > field.right() - 6) {
                cx = field.right() - 6;
            }
            canvas.fill_rect(Rect{cx, cy - 7, caret_w, 14},
                             canvas.color(theme.accent));
        }

        // Result rows (or the dim "No results" row).
        const hcpp::size_t visible =
            result_count_ < max_rows ? result_count_ : max_rows;
        if (result_count_ == 0 && face != nullptr) {
            const Rect row = row_rect(0);
            const hcpp::i32 base =
                row.y + (row.h - face->height) / 2 + face->baseline;
            canvas.draw_text(row.x + pad, base, "No results", muted,
                             *face);
            return;
        }
        for (hcpp::size_t i = 0; i < visible; ++i) {
            const PaletteItem& item = items_[result_idx_[i]];
            const Rect row = row_rect(i);
            if (i == selected_) {
                canvas.fill_rect_alpha(
                    row, canvas.color(theme.accent_soft),
                    theme.accent_soft_alpha);
                canvas.fill_rect(Rect{row.x, row.y, 2, row.h},
                                 canvas.color(theme.accent));
            }
            const hcpp::i32 rcy = row.y + row.h / 2;
            if (!item.icon.empty() && face != nullptr) {
                canvas.draw_text(row.x + pad, rcy - face->height / 2 +
                                                face->baseline,
                                 item.icon, muted, *face);
            } else {
                // Monogram chip fallback (app entries carry no icon).
                const Rect chip{row.x + pad, rcy - 8, 16, 16};
                canvas.fill_rounded_rect(
                    chip, 4, canvas.color(rui_lift(theme.primary, 1)));
                if (face != nullptr && !item.label.empty()) {
                    char mono[2] = {item.label[0] >= 'a' &&
                                            item.label[0] <= 'z'
                                        ? static_cast<char>(
                                              item.label[0] - 32)
                                        : item.label[0],
                                    '\0'};
                    canvas.draw_text(chip.x + 5,
                                     rcy - face->height / 2 +
                                         face->baseline,
                                     hcpp::StringView{mono, 1},
                                     canvas.color(theme.primary_fg),
                                     *face);
                }
            }
            if (face != nullptr) {
                const hcpp::i32 base =
                    row.y + (row.h - face->height) / 2 + face->baseline;
                canvas.draw_text(row.x + pad + icon_slot, base,
                                 item.label, canvas.color(theme.fg),
                                 *face);
                if (!item.hint.empty()) {
                    const Size hw = text_extent(item.hint, *face);
                    canvas.draw_text(
                        row.right() - hint_pad - hw.width, base,
                        item.hint, muted, *face);
                }
            }
        }
    }

    Rect CommandPalette::input_rect() const noexcept {
        const Rect box = bounds();
        return {box.x + pad, box.y + pad, box.w - pad * 2, field_h};
    }

    Rect CommandPalette::row_rect(hcpp::size_t row) const noexcept {
        const Rect box = bounds();
        return {box.x + pad,
                box.y + pad + field_h + list_gap +
                    static_cast<hcpp::i32>(row) * row_height,
                box.w - pad * 2, row_height};
    }

    void CommandPalette::rebuild() noexcept {
        // Score every item, keep matches ranked best-first (insertion
        // into bounded arrays; ties keep registration order).
        const hcpp::StringView q{query_, query_len_};
        result_count_ = 0;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            const hcpp::i32 score = fuzzy_score(q, items_[i].label);
            if (score < 0) {
                continue;
            }
            hcpp::size_t at = result_count_;
            for (hcpp::size_t j = 0; j < result_count_; ++j) {
                const hcpp::i32 other =
                    fuzzy_score(q, items_[result_idx_[j]].label);
                if (score > other) {
                    at = j;
                    break;
                }
            }
            for (hcpp::size_t j = result_count_; j > at; --j) {
                result_idx_[j] = result_idx_[j - 1];
            }
            result_idx_[at] = static_cast<hcpp::u8>(i);
            ++result_count_;
        }
        if (selected_ >= result_count_) {
            selected_ = result_count_ == 0 ? 0 : result_count_ - 1;
        }
    }

    bool CommandPalette::move_selection(hcpp::i32 delta) noexcept {
        const hcpp::size_t visible =
            result_count_ < max_rows ? result_count_ : max_rows;
        if (visible == 0) {
            return false;
        }
        // delta is +/-1; adding `visible` keeps the expression positive.
        const hcpp::size_t next =
            (selected_ + static_cast<hcpp::size_t>(
                             delta + static_cast<hcpp::i32>(visible))) %
            visible;
        if (next == selected_) {
            return false;
        }
        selected_ = next;
        return true;
    }

    bool CommandPalette::push_char(char c) noexcept {
        if (query_len_ >= query_cap) {
            return false;
        }
        query_[query_len_++] = c;
        query_[query_len_] = '\0';
        selected_ = 0;
        rebuild();
        return true;
    }

    bool CommandPalette::pop_char() noexcept {
        if (query_len_ == 0) {
            return false;
        }
        query_[--query_len_] = '\0';
        selected_ = 0;
        rebuild();
        return true;
    }

    const hcpp::fonts::FontFace* CommandPalette::resolved_font()
        const noexcept {
        return resolve_font(font_);
    }

}  // namespace hype::ui
