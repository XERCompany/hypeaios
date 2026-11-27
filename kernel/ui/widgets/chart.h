#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class BarChart : public Widget {
    public:
        static constexpr hcpp::size_t max_bars = 16;

        BarChart() noexcept = default;

        bool set_bar_count(hcpp::size_t count) noexcept;
        hcpp::size_t bar_count() const noexcept;

        bool set_value(hcpp::size_t bar, hcpp::i32 value) noexcept;
        hcpp::i32 value(hcpp::size_t bar) const noexcept;
        bool add_value(hcpp::i32 value) noexcept;

        void set_max(hcpp::i32 value) noexcept;
        hcpp::i32 max() const noexcept;
        hcpp::i32 resolved_max() const noexcept;

        void set_grid_lines(hcpp::i32 count) noexcept;
        hcpp::i32 grid_lines() const noexcept;

        Rect plot_rect() const noexcept;
        Rect bar_rect(hcpp::size_t bar) const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::i32 values_[max_bars] = {};
        hcpp::size_t bar_count_ = 0;
        hcpp::i32 max_ = 0;
        hcpp::i32 grid_lines_ = 3;
    };

    class LineChart : public Widget {
    public:
        static constexpr hcpp::size_t max_series = 5;
        static constexpr hcpp::size_t max_points = 16;

        LineChart() noexcept = default;

        bool set_series_count(hcpp::size_t count) noexcept;
        hcpp::size_t series_count() const noexcept;

        bool set_point_count(hcpp::size_t count) noexcept;
        hcpp::size_t point_count() const noexcept;

        bool set_point(hcpp::size_t series, hcpp::size_t index,
                       hcpp::i32 value) noexcept;
        hcpp::i32 point(hcpp::size_t series, hcpp::size_t index) const noexcept;

        void set_max(hcpp::i32 value) noexcept;
        hcpp::i32 resolved_max() const noexcept;

        void set_show_points(bool show) noexcept;
        bool show_points() const noexcept;

        void set_grid_lines(hcpp::i32 count) noexcept;
        hcpp::i32 grid_lines() const noexcept;

        Rect plot_rect() const noexcept;
        Point point_pos(hcpp::size_t series, hcpp::size_t index) const noexcept;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::i32 values_[max_series][max_points] = {};
        hcpp::size_t series_count_ = 0;
        hcpp::size_t point_count_ = 0;
        hcpp::i32 max_ = 0;
        hcpp::i32 grid_lines_ = 3;
        bool show_points_ = true;
    };

}  // namespace hype::ui
