#pragma once

// Slider — track + accent range + draggable thumb (issue #178, design
// spec docs/research/hypeui-design.md §3.9).
//
// States: normal / hover (thumb shadow lifts) / grabbed (press-captured
// drag — the host keeps routing Moves until Up) / focused (ring on
// thumb) / disabled / invalid (danger range). Value changes notify via
// on_change on every step while dragging; on_commit fires once on
// release or on a key commit — matching the shared state machine's
// "COMMIT = state write + parent notification" rule.

#include "hcpp/fonts/font.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/focus.h"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    // Pointer event kinds routed to Slider::handle_mouse. The host owns
    // hit-testing policy: send Down on button-press, Move on motion, Up
    // on release. While grabbed the slider keeps accepting Moves even
    // outside its rect (captured drag) — the host must keep routing.
    enum class SliderMouse : hcpp::u8 { Move, Down, Up };

    class Slider;
    using SliderCallback = void (*)(Slider& slider, hcpp::i32 value,
                                    void* context) noexcept;

    class Slider : public Widget, public Focusable {
    public:
        static constexpr hcpp::i32 track_height = 4;
        static constexpr hcpp::i32 thumb_diameter = 16;
        // Extra vertical slack around the thumb reserved for hit area /
        // value label when measuring.
        static constexpr hcpp::i32 default_height = 24;

        Slider() noexcept = default;
        Slider(hcpp::i32 vmin, hcpp::i32 vmax, hcpp::i32 value) noexcept;

        void set_range(hcpp::i32 vmin, hcpp::i32 vmax) noexcept;
        hcpp::i32 vmin() const noexcept;
        hcpp::i32 vmax() const noexcept;

        void set_step(hcpp::i32 step) noexcept;
        hcpp::i32 step() const noexcept;

        // Clamps into [vmin, vmax]; fires on_change when the value
        // actually moves. Programmatic sets never fire on_commit.
        void set_value(hcpp::i32 value) noexcept;
        hcpp::i32 value() const noexcept;

        void set_enabled(bool enabled) noexcept;
        bool enabled() const noexcept;
        void set_invalid(bool invalid) noexcept;
        bool invalid() const noexcept;
        void set_show_value(bool show) noexcept;
        bool show_value() const noexcept;

        bool hovered() const noexcept;
        bool grabbed() const noexcept;

        void set_font(const hcpp::fonts::FontFace* font) noexcept;
        const hcpp::fonts::FontFace* font() const noexcept;

        // Fired on each value step (drag or keys).
        void set_on_change(SliderCallback callback, void* context) noexcept;
        // Fired once per commit: pointer release after a drag, or a key
        // that moved the value.
        void set_on_commit(SliderCallback callback, void* context) noexcept;

        // Point-in-bounds hit test (includes the grown thumb zone).
        bool hit_test(Point point) const noexcept;
        // Drives hover/grab/drag. Returns true when visual state or the
        // value changed and the widget wants a repaint.
        bool handle_mouse(Point point, SliderMouse event) noexcept;
        // Snaps a pointer x to a clamped, stepped value.
        hcpp::i32 value_at(hcpp::i32 x) const noexcept;

        void set_focused(bool focused) noexcept override;
        bool focused() const noexcept override;
        bool on_key(char key) noexcept override;
        Rect focus_ring_rect() const noexcept override;

        Size measure(Size available) noexcept override;
        void paint(Canvas& canvas) noexcept override;

    private:
        const hcpp::fonts::FontFace* resolved_font() const noexcept;
        Rect track_rect() const noexcept;
        Rect thumb_rect() const noexcept;
        hcpp::i32 clamp_step(hcpp::i32 value) const noexcept;
        void apply_value(hcpp::i32 value) noexcept;
        void commit() noexcept;

        SliderCallback on_change_ = nullptr;
        SliderCallback on_commit_ = nullptr;
        void* change_ctx_ = nullptr;
        void* commit_ctx_ = nullptr;
        const hcpp::fonts::FontFace* font_ = nullptr;
        hcpp::i32 vmin_ = 0;
        hcpp::i32 vmax_ = 100;
        hcpp::i32 step_ = 1;
        hcpp::i32 value_ = 0;
        bool enabled_ = true;
        bool invalid_ = false;
        bool show_value_ = false;
        bool hovered_ = false;
        bool grabbed_ = false;
        bool focused_ = false;
    };

}  // namespace hype::ui
