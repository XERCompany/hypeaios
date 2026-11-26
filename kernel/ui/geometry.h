#pragma once

#include "hcpp/types.hpp"

namespace hype::ui {

    struct Point {
        hcpp::i32 x = 0;
        hcpp::i32 y = 0;

        constexpr Point operator+(const Point& other) const noexcept {
            return {x + other.x, y + other.y};
        }
        constexpr Point operator-(const Point& other) const noexcept {
            return {x - other.x, y - other.y};
        }
        constexpr bool operator==(const Point& other) const noexcept {
            return x == other.x && y == other.y;
        }
        constexpr bool operator!=(const Point& other) const noexcept { return !(*this == other); }
    };

    struct Size {
        hcpp::i32 width = 0;
        hcpp::i32 height = 0;

        constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
        constexpr bool operator==(const Size& other) const noexcept {
            return width == other.width && height == other.height;
        }
        constexpr bool operator!=(const Size& other) const noexcept { return !(*this == other); }
    };

    struct Insets {
        hcpp::i32 left = 0;
        hcpp::i32 top = 0;
        hcpp::i32 right = 0;
        hcpp::i32 bottom = 0;

        static constexpr Insets uniform(hcpp::i32 value) noexcept {
            return {value, value, value, value};
        }
        static constexpr Insets symmetric(hcpp::i32 horizontal, hcpp::i32 vertical) noexcept {
            return {horizontal, vertical, horizontal, vertical};
        }

        constexpr hcpp::i32 horizontal() const noexcept { return left + right; }
        constexpr hcpp::i32 vertical() const noexcept { return top + bottom; }
        constexpr bool empty() const noexcept {
            return left == 0 && top == 0 && right == 0 && bottom == 0;
        }
    };

    struct Rect {
        hcpp::i32 x = 0;
        hcpp::i32 y = 0;
        hcpp::i32 w = 0;
        hcpp::i32 h = 0;

        constexpr hcpp::i32 left() const noexcept { return x; }
        constexpr hcpp::i32 top() const noexcept { return y; }
        constexpr hcpp::i32 right() const noexcept { return x + w; }
        constexpr hcpp::i32 bottom() const noexcept { return y + h; }
        constexpr Point origin() const noexcept { return {x, y}; }
        constexpr Size size() const noexcept { return {w, h}; }
        constexpr bool empty() const noexcept { return w <= 0 || h <= 0; }

        constexpr bool contains(Point point) const noexcept {
            return point.x >= x && point.x < x + w && point.y >= y && point.y < y + h;
        }
        constexpr bool contains(const Rect& other) const noexcept {
            return other.x >= x && other.y >= y && other.x + other.w <= x + w &&
                   other.y + other.h <= y + h;
        }
        constexpr bool intersects(const Rect& other) const noexcept {
            return x < other.x + other.w && x + w > other.x && y < other.y + other.h &&
                   y + h > other.y;
        }
        constexpr Rect intersection(const Rect& other) const noexcept {
            const hcpp::i32 new_x = x > other.x ? x : other.x;
            const hcpp::i32 new_y = y > other.y ? y : other.y;
            const hcpp::i32 new_right = right() < other.right() ? right() : other.right();
            const hcpp::i32 new_bottom = bottom() < other.bottom() ? bottom() : other.bottom();
            if (new_x >= new_right || new_y >= new_bottom) {
                return {};
            }
            return {new_x, new_y, new_right - new_x, new_bottom - new_y};
        }
        constexpr Rect united(const Rect& other) const noexcept {
            const hcpp::i32 new_x = x < other.x ? x : other.x;
            const hcpp::i32 new_y = y < other.y ? y : other.y;
            const hcpp::i32 new_right = right() > other.right() ? right() : other.right();
            const hcpp::i32 new_bottom = bottom() > other.bottom() ? bottom() : other.bottom();
            return {new_x, new_y, new_right - new_x, new_bottom - new_y};
        }
        constexpr Rect inset(const Insets& insets) const noexcept {
            return {x + insets.left, y + insets.top, w - insets.horizontal(),
                    h - insets.vertical()};
        }
        constexpr Rect translated(Point delta) const noexcept {
            return {x + delta.x, y + delta.y, w, h};
        }
        constexpr bool operator==(const Rect& other) const noexcept {
            return x == other.x && y == other.y && w == other.w && h == other.h;
        }
        constexpr bool operator!=(const Rect& other) const noexcept { return !(*this == other); }
    };

}  // namespace hype::ui
