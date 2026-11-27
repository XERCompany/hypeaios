#pragma once

// Internal shared state for the HypeAIGDXF subsystem. Not a public header —
// only the kernel/gdxf translation units include it.

#include "kernel/gdxf/gdxf.h"
#include "kernel/input/curstat.h"
#include "kernel/istatekeys.h"

namespace hype::gdxf {

    struct GdxfState {
        bool initialized = false;
        hcpp::graphics::Framebuffer scanout{};

        // back buffer (PMM-backed; kept out of .bss to stay under the
        // kernel.bin size cap)
        hcpp::u8* backbuf = nullptr;
        hcpp::u32 surf_w = 0;
        hcpp::u32 surf_h = 0;
        hcpp::size_t backbuf_pages = 0;
        bool surf_acquired = false;

        // present target in screen coords; fullscreen when windowed == false
        bool windowed = false;
        hype::ui::Rect present_dst{};

        // fixed-timestep accumulator loop
        hcpp::u32 tick_hz = gdxf_default_tick_rate;
        hcpp::u64 step_us = 1000000ull / gdxf_default_tick_rate;
        hcpp::u64 acc_us = 0;
        hcpp::u64 last_us = 0;
        hcpp::u64 frame_start_us = 0;
        hcpp::u64 frame_count = 0;
        hcpp::u32 frame_us_avg = 0;
        hcpp::u32 steps_due = 0;
        VSyncMode vsync = VSyncMode::TickLock;
        bool quit = false;
        bool presented = false;
        bool fps_overlay = false;

        // input snapshot (fed by the functionemitkeys bus + hypeptr)
        hype::input::KeyBitset keys_down{};
        hype::input::KeyBitset keys_edge_next{};
        hype::input::KeyBitset keys_edge_frame{};
        bool input_hooked = false;
        MouseState mouse{};
        bool mouse_ready = false;

        // sprite batch dirty accumulation
        hype::ui::Rect dirty[gdxf_max_dirty_rects];
        hcpp::u32 dirty_count = 0;
        bool batch_open = false;
    };

    extern GdxfState g_gdxf;

    void gdxf_input_hook() noexcept;
    void gdxf_input_pump() noexcept;
    bool gdxf_surface_resize(hcpp::u32 w, hcpp::u32 h) noexcept;
    void gdxf_dirty_add(hype::ui::Rect rect) noexcept;

}  // namespace hype::gdxf
