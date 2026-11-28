#pragma once

// ScrollArea — clipped viewport + overlay scrollbar (design spec
// docs/research/hypeui-design.md §5 `scroll-area`). Wraps a single
// content widget taller than the viewport: children are clipped to the
// viewport rect, a scrollbar thumb rides the right edge, and wheel /
// key / thumb-drag all move the same scroll offset.
//
// States: normal / hover-thumb / grabbed (press-captured drag — the
// host keeps routing Moves until Up). The scrollbar only appears when
// the content is actually taller than the viewport; a collapsed
// (non-scrollable) area paints no chrome at all.

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    class Canvas;

    // Pointer event kinds routed to ScrollArea::handle_mouse.
    enum class ScrollMouse : hcpp::u8 { Move, Down, Up };

    class ScrollArea : public Widget {
    public:
        static constexpr hcpp::i32 bar_width = 10;
        static constexpr hcpp::i32 thumb_min = 24;
        static constexpr hcpp::i32 scroll_step = 40;  // wheel/key px

        ScrollArea() noexcept = default;

        // Adds the content as the widget's single child; the content
        // keeps its measured height and is positioned at (0, -scroll).
        bool set_content(Widget* content) noexcept;
        Widget* content() const noexcept;

        // Clamps into [0, max_scroll()]. Programmatic sets never grab.
        void set_scroll(hcpp::i32 offset) noexcept;
        hcpp::i32 scroll() const noexcept;
        hcpp::i32 max_scroll() const noexcept;
        hcpp::i32 content_height() const noexcept;
        bool scrollable() const noexcept;

        Rect viewport_rect() const noexcept;
        Rect track_rect() const noexcept;
        Rect thumb_rect() const noexcept;

        // Wheel-style scroll: positive delta moves the viewport down.
        // Returns true when the offset moved.
        bool scroll_by(hcpp::i32 delta) noexcept;
        bool hovered() const noexcept;
        bool grabbed() const noexcept;

        // Down on the thumb grabs; track clicks page. Returns true when
        // visual state changed.
        bool handle_mouse(Point point, ScrollMouse event) noexcept;
        bool handle_key(char ascii) noexcept override;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        hcpp::i32 clamped(hcpp::i32 offset) const noexcept;

        Widget* content_ = nullptr;
        hcpp::i32 offset_ = 0;
        hcpp::i32 content_h_ = 0;
        hcpp::i32 grab_off_ = 0;
        bool hovered_ = false;
        bool grabbed_ = false;
    };

}  // namespace hype::ui
