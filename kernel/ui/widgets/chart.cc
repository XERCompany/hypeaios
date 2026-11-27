#include "kernel/ui/widgets/chart.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        void paint_grid(Canvas& canvas, const Rect& plot,
                        hcpp::i32 grid_lines) noexcept {
            const Theme& theme = theme_active();
            const hcpp::u32 grid = canvas.color(theme.muted);
            for (hcpp::i32 i = 1; i <= grid_lines; ++i) {
                const hcpp::i32 y =
                    plot.y + i * plot.h / (grid_lines + 1);
                canvas.fill_rect({plot.x, y, plot.w, 1}, grid);
            }
            canvas.fill_rect({plot.x, plot.bottom() - 1, plot.w, 1},
                             canvas.color(theme.border));
        }

    }  // namespace

    bool BarChart::set_bar_count(hcpp::size_t count) noexcept {
        if (count > max_bars) {
            return false;
        }
        bar_count_ = count;
        return true;
    }

    hcpp::size_t BarChart::bar_count() const noexcept { return bar_count_; }

    bool BarChart::set_value(hcpp::size_t bar, hcpp::i32 value) noexcept {
        if (bar >= max_bars) {
            return false;
        }
        values_[bar] = value;
        if (bar >= bar_count_) {
            bar_count_ = bar + 1;
        }
        return true;
    }

    hcpp::i32 BarChart::value(hcpp::size_t bar) const noexcept {
        if (bar >= bar_count_) {
            return 0;
        }
        return values_[bar];
    }

    bool BarChart::add_value(hcpp::i32 value) noexcept {
        if (bar_count_ >= max_bars) {
            return false;
        }
        values_[bar_count_++] = value;
        return true;
    }

    void BarChart::set_max(hcpp::i32 value) noexcept { max_ = value; }

    hcpp::i32 BarChart::max() const noexcept { return max_; }

    hcpp::i32 BarChart::resolved_max() const noexcept {
        if (max_ > 0) {
            return max_;
        }
        hcpp::i32 found = 1;
        for (hcpp::size_t i = 0; i < bar_count_; ++i) {
            if (values_[i] > found) {
                found = values_[i];
            }
        }
        return found;
    }

    void BarChart::set_grid_lines(hcpp::i32 count) noexcept {
        grid_lines_ = count >= 0 ? count : 0;
    }

    hcpp::i32 BarChart::grid_lines() const noexcept { return grid_lines_; }

    Rect BarChart::plot_rect() const noexcept {
        return bounds().inset(Insets{2, 2, 2, 2});
    }

    Rect BarChart::bar_rect(hcpp::size_t bar) const noexcept {
        const Rect plot = plot_rect();
        if (bar >= bar_count_ || bar_count_ == 0 || plot.empty()) {
            return {};
        }
        const hcpp::i32 slot = plot.w / static_cast<hcpp::i32>(bar_count_);
        hcpp::i32 gap = slot / 4;
        if (gap < 1) {
            gap = 1;
        }
        hcpp::i32 width = slot - gap;
        if (width < 1) {
            width = 1;
        }
        const hcpp::i32 vmax = resolved_max();
        hcpp::i32 height = values_[bar] > 0
                               ? values_[bar] * (plot.h - 1) / vmax
                               : 0;
        if (values_[bar] > 0 && height < 1) {
            height = 1;
        }
        return {plot.x + slot * static_cast<hcpp::i32>(bar) + gap / 2,
                plot.bottom() - 1 - height, width, height};
    }

    Size BarChart::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            desired = {160, 80};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void BarChart::paint(Canvas& canvas) noexcept {
        const Rect plot = plot_rect();
        if (plot.empty() || bar_count_ == 0) {
            return;
        }
        const Theme& theme = theme_active();
        paint_grid(canvas, plot, grid_lines_);
        for (hcpp::size_t i = 0; i < bar_count_; ++i) {
            const Rect bar = bar_rect(i);
            if (!bar.empty()) {
                canvas.fill_rect(
                    bar, canvas.color(theme.chart[i % 5]));
            }
        }
    }

    bool LineChart::set_series_count(hcpp::size_t count) noexcept {
        if (count > max_series) {
            return false;
        }
        series_count_ = count;
        return true;
    }

    hcpp::size_t LineChart::series_count() const noexcept {
        return series_count_;
    }

    bool LineChart::set_point_count(hcpp::size_t count) noexcept {
        if (count > max_points) {
            return false;
        }
        point_count_ = count;
        return true;
    }

    hcpp::size_t LineChart::point_count() const noexcept {
        return point_count_;
    }

    bool LineChart::set_point(hcpp::size_t series, hcpp::size_t index,
                              hcpp::i32 value) noexcept {
        if (series >= max_series || index >= max_points) {
            return false;
        }
        values_[series][index] = value;
        if (series >= series_count_) {
            series_count_ = series + 1;
        }
        if (index >= point_count_) {
            point_count_ = index + 1;
        }
        return true;
    }

    hcpp::i32 LineChart::point(hcpp::size_t series,
                               hcpp::size_t index) const noexcept {
        if (series >= series_count_ || index >= point_count_) {
            return 0;
        }
        return values_[series][index];
    }

    void LineChart::set_max(hcpp::i32 value) noexcept { max_ = value; }

    hcpp::i32 LineChart::resolved_max() const noexcept {
        if (max_ > 0) {
            return max_;
        }
        hcpp::i32 found = 1;
        for (hcpp::size_t s = 0; s < series_count_; ++s) {
            for (hcpp::size_t i = 0; i < point_count_; ++i) {
                if (values_[s][i] > found) {
                    found = values_[s][i];
                }
            }
        }
        return found;
    }

    void LineChart::set_show_points(bool show) noexcept {
        show_points_ = show;
    }

    bool LineChart::show_points() const noexcept { return show_points_; }

    void LineChart::set_grid_lines(hcpp::i32 count) noexcept {
        grid_lines_ = count >= 0 ? count : 0;
    }

    hcpp::i32 LineChart::grid_lines() const noexcept { return grid_lines_; }

    Rect LineChart::plot_rect() const noexcept {
        return bounds().inset(Insets{2, 2, 2, 2});
    }

    Point LineChart::point_pos(hcpp::size_t series,
                               hcpp::size_t index) const noexcept {
        const Rect plot = plot_rect();
        if (series >= series_count_ || index >= point_count_ ||
            point_count_ == 0 || plot.empty()) {
            return {};
        }
        hcpp::i32 x = plot.x;
        if (point_count_ > 1) {
            x += static_cast<hcpp::i32>(index) * (plot.w - 1) /
                 static_cast<hcpp::i32>(point_count_ - 1);
        }
        const hcpp::i32 vmax = resolved_max();
        hcpp::i32 value = values_[series][index];
        if (value < 0) {
            value = 0;
        }
        const hcpp::i32 y =
            plot.bottom() - 2 - value * (plot.h - 3) / vmax;
        return {x, y};
    }

    Size LineChart::measure(Size available) noexcept {
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            desired = {160, 80};
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void LineChart::paint(Canvas& canvas) noexcept {
        const Rect plot = plot_rect();
        if (plot.empty() || series_count_ == 0 || point_count_ == 0) {
            return;
        }
        const Theme& theme = theme_active();
        paint_grid(canvas, plot, grid_lines_);
        for (hcpp::size_t s = 0; s < series_count_; ++s) {
            const hcpp::u32 rgb = canvas.color(theme.chart[s % 5]);
            for (hcpp::size_t i = 1; i < point_count_; ++i) {
                const Point a = point_pos(s, i - 1);
                const Point b = point_pos(s, i);
                canvas.draw_line(a.x, a.y, b.x, b.y, rgb);
            }
            if (show_points_) {
                for (hcpp::size_t i = 0; i < point_count_; ++i) {
                    const Point p = point_pos(s, i);
                    canvas.fill_circle(p.x, p.y, 2, rgb);
                }
            }
        }
    }

}  // namespace hype::ui
