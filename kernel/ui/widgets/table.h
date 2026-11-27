#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Table : public Widget {
    public:
        static constexpr hcpp::size_t max_rows = 8;
        static constexpr hcpp::size_t max_cols = 4;
        static constexpr hcpp::size_t cell_capacity = 24;

        Table() noexcept = default;

        bool set_column_count(hcpp::size_t count) noexcept;
        hcpp::size_t column_count() const noexcept;

        bool set_column_width(hcpp::size_t column, hcpp::i32 width) noexcept;
        hcpp::i32 column_width(hcpp::size_t column) const noexcept;

        bool set_header(hcpp::size_t column, hcpp::StringView text) noexcept;
        bool set_row_count(hcpp::size_t count) noexcept;
        hcpp::size_t row_count() const noexcept;
        bool set_cell(hcpp::size_t row, hcpp::size_t column,
                      hcpp::StringView text) noexcept;

        void set_selected(hcpp::i32 row) noexcept;
        hcpp::i32 selected() const noexcept;

        void set_row_height(hcpp::i32 height) noexcept;
        hcpp::i32 row_height() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        hcpp::i32 column_x(hcpp::size_t column) const noexcept;
        hcpp::i32 resolved_column_width(hcpp::size_t column) const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        void draw_cell(Canvas& canvas, Rect cell, hcpp::StringView text,
                       hcpp::u32 rgb, const hcpp::fonts::FontFace& face) noexcept;

        char header_[max_cols][cell_capacity] = {};
        char cells_[max_rows][max_cols][cell_capacity] = {};
        hcpp::i32 column_widths_[max_cols] = {};
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::size_t column_count_ = 0;
        hcpp::size_t row_count_ = 0;
        hcpp::i32 selected_ = -1;
        hcpp::i32 row_height_ = 0;
    };

}  // namespace hype::ui
