#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::gdxf {

    // ---- types ----
    enum class PixelFormat : hcpp::u8 { XRGB32, RGB24 };
    enum class VSyncMode : hcpp::u8 { Uncapped, TickLock, Retrace };  // Retrace = future flip
    enum class TexFlags : hcpp::u16 { None = 0, Alpha = 1, FlipX = 2, FlipY = 4 };

    constexpr TexFlags operator|(TexFlags a, TexFlags b) noexcept {
        return static_cast<TexFlags>(static_cast<hcpp::u16>(a) |
                                     static_cast<hcpp::u16>(b));
    }
    constexpr bool gdxf_tex_flag(TexFlags flags, TexFlags bit) noexcept {
        return (static_cast<hcpp::u16>(flags) & static_cast<hcpp::u16>(bit)) != 0;
    }

    struct Surface {
        hcpp::u8* pixels;
        hcpp::u32 width, height, pitch_bytes;
        PixelFormat format;
    };  // borrow-only, valid until present

    struct FrameTiming {
        hcpp::u64 frame;
        hcpp::u64 t_us;
        hcpp::u32 delta_us;
        float dt;
        float alpha;
    };
    struct MouseState {
        hcpp::i32 x, y;
        hcpp::u8 buttons;
        hcpp::i8 wheel;
    };
    struct GamepadState {
        bool connected;
        hcpp::u16 buttons;
        hcpp::i16 lx, ly, rx, ry;
        hcpp::u8 lt, rt;
    };
    struct AudioFormat {
        hcpp::u32 rate;
        hcpp::u8 channels;
    };  // s16 interleaved
    struct GameCallbacks {
        void (*init)(void* ctx) noexcept;
        void (*update)(float dt, void* ctx) noexcept;  // fixed-dt sim step
        void (*render)(Surface&, float alpha, void* ctx) noexcept;
        void* ctx;
    };

    constexpr hcpp::u32 gdxf_max_textures = 64;
    constexpr hcpp::u32 gdxf_max_audio_streams = 4;
    constexpr hcpp::u32 gdxf_max_dirty_rects = 16;
    constexpr hcpp::u32 gdxf_max_sim_steps = 5;
    constexpr hcpp::u32 gdxf_invalid_tex = 0;
    constexpr hcpp::u32 gdxf_invalid_stream = 0;
    constexpr hcpp::u32 gdxf_default_tick_rate = 60;
    constexpr hcpp::u32 gdxf_frame_clamp_us = 250000;  // spiral-of-death clamp

    // ---- lifecycle / loop ----
    bool gdxf_initialize(hcpp::graphics::Framebuffer& scanout) noexcept;
    bool gdxf_initialized() noexcept;
    hcpp::u32 gdxf_run(const GameCallbacks&) noexcept;  // engine-owned loop, returns exit code
    bool gdxf_begin_frame(FrameTiming&) noexcept;       // pumps input, computes dt/alpha
    void gdxf_end_frame() noexcept;                     // rate-limit + present
    hcpp::u32 gdxf_update_steps() noexcept;             // fixed-dt sim steps due this frame
    void gdxf_request_quit() noexcept;
    void gdxf_clear_quit() noexcept;
    void gdxf_set_tick_rate(hcpp::u32 hz) noexcept;     // fixed dt, default 60
    void gdxf_set_vsync(VSyncMode) noexcept;

    // ---- surface / present ----
    bool gdxf_surface_acquire(Surface&) noexcept;  // back buffer borrow
    void gdxf_present(const hype::ui::Rect* dirty,
                      hcpp::u32 n) noexcept;       // blt today, flip later
    hcpp::u32 gdxf_surface_count() noexcept;       // chain depth (1 today, 2+ with flip)
    bool gdxf_fullscreen_direct() noexcept;        // "DirectFlip-equivalent" engaged?
    // present into a wm window region; `target` is the framebuffer the app is
    // currently painting into (the compositor surface) — nullptr blts to the
    // raw scanout (fullscreen-style poke).
    bool gdxf_bind_window(hype::ui::Rect dst,
                        hcpp::graphics::Framebuffer* target) noexcept;
    void gdxf_unbind_window() noexcept;                  // back to full-screen direct

    // ---- input (per-frame state snapshot; events stay on functionemitkeys) ----
    bool gdxf_key_down(hcpp::u16 key_code) noexcept;
    bool gdxf_key_pressed(hcpp::u16 key_code) noexcept;  // edge since last frame
    bool gdxf_mouse(MouseState&) noexcept;
    hcpp::u8 gdxf_gamepad_count() noexcept;  // 0 until virtio-input/gamepad
    bool gdxf_gamepad_state(hcpp::u8 index, GamepadState&) noexcept;

    // ---- time ----
    hcpp::u64 gdxf_ticks() noexcept;  // monotonic, backend = APIC-timer/TSC/PIT
    hcpp::u64 gdxf_ticks_per_second() noexcept;
    hcpp::u64 gdxf_time_us() noexcept;
    void gdxf_sleep_us(hcpp::u64 us) noexcept;  // hlt-sleep, not spin

    // ---- audio (over hasio/pullmix) ----
    hcpp::u32 gdxf_audio_open(const AudioFormat&, hcpp::u32 period_frames) noexcept;
    bool gdxf_audio_write(hcpp::u32 stream, const hcpp::i16* frames,
                          hcpp::u32 count) noexcept;
    hcpp::u32 gdxf_audio_queued_frames(hcpp::u32 stream) noexcept;
    void gdxf_audio_close(hcpp::u32 stream) noexcept;

    // ---- sprites / tiles (batched clipped blit into acquired surface) ----
    hcpp::u32 gdxf_tex_create(hcpp::u32 w, hcpp::u32 h,
                              const hcpp::u32* argb) noexcept;  // cap 64
    void gdxf_tex_destroy(hcpp::u32 tex) noexcept;
    void gdxf_sprite_batch_begin() noexcept;
    void gdxf_sprite_draw(hcpp::u32 tex, hcpp::i32 x, hcpp::i32 y, TexFlags) noexcept;
    void gdxf_sprite_draw_ex(hcpp::u32 tex, hype::ui::Rect src, hype::ui::Rect dst,
                             TexFlags) noexcept;
    void gdxf_sprite_batch_end() noexcept;  // one clipped pass, fills dirty list
    // tilemap: map cell 0 = empty; cell n blits atlas tile n-1 of tex
    // (atlas laid out left-to-right, top-to-bottom in tile_w x tile_h cells).
    void gdxf_tilemap_draw(hcpp::u32 tex, hcpp::u32 tile_w, hcpp::u32 tile_h,
                           const hcpp::u8* map, hcpp::u32 map_w, hcpp::u32 map_h,
                           hcpp::i32 cam_x, hcpp::i32 cam_y) noexcept;

    // ---- debug ----
    hcpp::u32 gdxf_frame_us() noexcept;  // rolling avg, for an fps overlay
    void gdxf_set_fps_overlay(bool) noexcept;

    bool gdxf_selftest() noexcept;

}  // namespace hype::gdxf
