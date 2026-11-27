#include "kernel/ui/widgets/table.h"

#include "hcpp/utf8.hpp"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr hcpp::i32 cell_pad_x = 6;

        hcpp::size_t copy_text(char* dst, hcpp::size_t cap,
                               hcpp::StringView src) noexcept {
            hcpp::size_t count = src.size();
            if (count > cap - 1) {
                count = cap - 1;
            }
            for (hcpp::size_t i = 0; i < count; ++i) {
                dst[i] = src[i];
            }
            dst[count] = '\0';
            return count;
        }

        hcpp::size_t fit_bytes(hcpp::StringView text,
                               const hcpp::fonts::FontFace& font,
                               hcpp::i32 max_width) noexcept {
            hcpp::i32 width = 0;
            hcpp::size_t cursor = 0;
            hcpp::size_t fit = 0;
            while (cursor < text.size()) {
                const hcpp::u32 codepoint = hcpp::utf8::next_codepoint(text, cursor);
                const hcpp::fonts::FontGlyph glyph = font.glyph_for(codepoint);
                width += glyph.advance != 0 ? glyph.advance : font.advance;
                if (width > max_width) {
                    break;
                }
                fit = cursor;
            }
            return fit;
        }

    }  // namespace

    bool Table::set_column_count(hcpp::size_t count) noexcept {
        if (count > max_cols) {
            return false;
        }
        column_count_ = count;
        return true;
    }

    hcpp::size_t Table::column_count() const noexcept { return column_count_; }

    bool Table::set_column_width(hcpp::size_t column, hcpp::i32 width) noexcept {
        if (column >= max_cols) {
            return false;
        }
        column_widths_[column] = width;
        return true;
    }

    hcpp::i32 Table::column_width(hcpp::size_t column) const noexcept {
        if (column >= max_cols) {
            return 0;
        }
        return column_widths_[column];
    }

    bool Table::set_header(hcpp::size_t column, hcpp::StringView text) noexcept {
        if (column >= max_cols) {
            return false;
        }
        copy_text(header_[column], cell_capacity, text);
        return true;
    }

    bool Table::set_row_count(hcpp::size_t count) noexcept {
        if (count > max_rows) {
            return false;
        }
        row_count_ = count;
        if (selected_ >= static_cast<hcpp::i32>(row_count_)) {
            selected_ = -1;
        }
        return true;
    }

    hcpp::size_t Table::row_count() const noexcept { return row_count_; }

    bool Table::set_cell(hcpp::size_t row, hcpp::size_t column,
                         hcpp::StringView text) noexcept {
        if (row >= max_rows || column >= max_cols) {
            return false;
        }
        copy_text(cells_[row][column], cell_capacity, text);
        return true;
    }

    void Table::set_selected(hcpp::i32 row) noexcept {
        selected_ = row >= 0 && row < static_cast<hcpp::i32>(row_count_) ? row : -1;
    }

    hcpp::i32 Table::selected() const noexcept { return selected_; }

    void Table::set_row_height(hcpp::i32 height) noexcept {
        row_height_ = height;
    }

    hcpp::i32 Table::row_height() const noexcept {
        if (row_height_ > 0) {
            return row_height_;
        }
        const hcpp::fonts::FontFace* face = resolved_font();
        return (face != nullptr ? face->height : 16) + 8;
    }

    void Table::set_font(const hcpp::fonts::FontFace* font) noexcept {
        font_ = font;
    }

    const hcpp::fonts::FontFace* Table::font() const noexcept { return font_; }

    const hcpp::fonts::FontFace* Table::resolved_font() const noexcept {
        return resolve_font(font_);
    }

    hcpp::i32 Table::resolved_column_width(hcpp::size_t column) const noexcept {
        if (column >= column_count_) {
            return 0;
        }
        hcpp::i32 fixed = 0;
        hcpp::size_t automatic = 0;
        for (hcpp::size_t i = 0; i < column_count_; ++i) {
            if (column_widths_[i] > 0) {
                fixed += column_widths_[i];
            } else {
                ++automatic;
            }
        }
        if (column_widths_[column] > 0) {
            return column_widths_[column];
        }
        hcpp::i32 remaining = bounds().w - fixed;
        if (remaining < 0 || automatic == 0) {
            return 0;
        }
        return remaining / static_cast<hcpp::i32>(automatic);
    }

    hcpp::i32 Table::column_x(hcpp::size_t column) const noexcept {
        hcpp::i32 x = bounds().x;
        for (hcpp::size_t i = 0; i < column && i < column_count_; ++i) {
            x += resolved_column_width(i);
        }
        return x;
    }

    Size Table::measure(Size available) noexcept {
        const hcpp::i32 line = row_height();
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            hcpp::i32 width = 0;
            for (hcpp::size_t i = 0; i < column_count_; ++i) {
                width += column_widths_[i] > 0 ? column_widths_[i] : 64;
            }
            desired = {width, line * static_cast<hcpp::i32>(row_count_ + 1) + 1};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Table::draw_cell(Canvas& canvas, Rect cell, hcpp::StringView text,
                          hcpp::u32 rgb,
                          const hcpp::fonts::FontFace& face) noexcept {
        if (cell.empty() || text.empty()) {
            return;
        }
        const Rect saved = canvas.clip();
        canvas.set_clip(cell.intersection(saved));
        hcpp::i32 offset = (cell.h - face.height) / 2;
        if (offset < 0) {
            offset = 0;
        }
        const hcpp::i32 baseline_y = cell.y + offset + face.baseline;
        const hcpp::i32 avail = cell.w - 2 * cell_pad_x;
        const hcpp::i32 x = cell.x + cell_pad_x;
        if (avail > 0 && text_extent(text, face).width <= avail) {
            canvas.draw_text(x, baseline_y, text, rgb, face);
        } else if (avail > 0) {
            const hcpp::StringView dots("...", 3);
            const hcpp::i32 dots_w = text_extent(dots, face).width;
            const hcpp::size_t kept = fit_bytes(text, face, avail - dots_w);
            const hcpp::StringView head(text.data(), kept);
            canvas.draw_text(x, baseline_y, head, rgb, face);
            canvas.draw_text(x + text_extent(head, face).width, baseline_y, dots,
                             rgb, face);
        }
        canvas.set_clip(saved);
    }

    void Table::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty() || column_count_ == 0) {
            return;
        }
        const Theme& theme = theme_active();
        const hcpp::fonts::FontFace* face = resolved_font();
        const hcpp::i32 line = row_height();
        canvas.fill_rect(box, canvas.color(theme.card));
        canvas.stroke_rect(box, canvas.color(theme.border));

        const Rect header_rect{box.x + 1, box.y + 1, box.w - 2, line};
        canvas.fill_rect(header_rect, canvas.color(rui_lift(theme.muted, 1)));
        canvas.fill_rect_alpha(
            Rect{header_rect.x, header_rect.bottom(), header_rect.w, 2},
            canvas.color(theme.shadow), 48);
        const hcpp::i32 rows_y = header_rect.bottom();

        for (hcpp::size_t row = 0; row < row_count_; ++row) {
            const Rect row_rect{box.x + 1, rows_y + static_cast<hcpp::i32>(row) * line,
                                box.w - 2, line};
            if (row_rect.y >= box.bottom() - 1) {
                break;
            }
            if (static_cast<hcpp::i32>(row) == selected_) {
                canvas.fill_rect(row_rect, canvas.color(theme.primary));
            } else {
                canvas.fill_rect({row_rect.x, row_rect.bottom() - 1, row_rect.w, 1},
                                 canvas.color(theme.border));
            }
        }

        if (face == nullptr) {
            return;
        }
        const hcpp::u32 header_rgb = canvas.color(theme.muted_fg);
        const hcpp::u32 cell_rgb = canvas.color(theme.fg);
        const hcpp::u32 selected_rgb = canvas.color(theme.primary_fg);
        for (hcpp::size_t col = 0; col < column_count_; ++col) {
            const hcpp::i32 x = column_x(col);
            const hcpp::i32 w = resolved_column_width(col);
            const Rect cell{x, header_rect.y, w, header_rect.h};
            draw_cell(canvas, cell.intersection(box.inset(Insets::uniform(1))),
                      hcpp::StringView(header_[col]), header_rgb, *face);
        }
        for (hcpp::size_t row = 0; row < row_count_; ++row) {
            const hcpp::i32 row_y = rows_y + static_cast<hcpp::i32>(row) * line;
            if (row_y >= box.bottom() - 1) {
                break;
            }
            const bool sel = static_cast<hcpp::i32>(row) == selected_;
            for (hcpp::size_t col = 0; col < column_count_; ++col) {
                const Rect cell{column_x(col), row_y, resolved_column_width(col),
                                line};
                draw_cell(canvas, cell.intersection(box.inset(Insets::uniform(1))),
                          hcpp::StringView(cells_[row][col]),
                          sel ? selected_rgb : cell_rgb, *face);
            }
        }
    }

}  // namespace hype::ui
