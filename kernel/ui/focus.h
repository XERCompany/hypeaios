#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"

namespace hype::ui {

    namespace keys {
        constexpr char tab = '\t';
        constexpr char backtab = '\x02';
        constexpr char up = '\x11';
        constexpr char down = '\x12';
        constexpr char left = '\x13';
        constexpr char right = '\x14';
        constexpr char enter = '\n';
        constexpr char space = ' ';
    }  // namespace keys

    class Focusable {
    public:
        virtual void set_focused(bool focused) noexcept = 0;
        virtual bool focused() const noexcept = 0;
        virtual bool on_key(char key) noexcept = 0;
        virtual Rect focus_ring_rect() const noexcept = 0;
    };

    void draw_focus_ring(Canvas& canvas, const Rect& bounds) noexcept;

    class FocusManager {
    public:
        static constexpr hcpp::size_t capacity = 16;
        static constexpr hcpp::size_t no_focus = capacity;

        bool add(Focusable* widget) noexcept;
        bool remove(Focusable* widget) noexcept;
        void clear() noexcept;

        hcpp::size_t count() const noexcept;
        hcpp::size_t focus_index() const noexcept;
        Focusable* focused() const noexcept;
        Focusable* item_at(hcpp::size_t index) const noexcept;

        bool focus_at(hcpp::size_t index) noexcept;
        bool focus_next() noexcept;
        bool focus_prev() noexcept;

        bool handle_key(char c) noexcept;
        void paint_ring(Canvas& canvas) const noexcept;

    private:
        Focusable* items_[capacity] = {};
        hcpp::size_t count_ = 0;
        hcpp::size_t focused_ = no_focus;
    };

}  // namespace hype::ui
