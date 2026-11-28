#include "kernel/hxrt/apps/builtins.h"

#include "kernel/audio/waveform.h"
#include "kernel/gdxf/gdxf.h"
#include "kernel/serial.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/win/winman.h"

namespace hype::hxrt {

    namespace {

        using hype::gdxf::FrameTiming;
        using hype::gdxf::Surface;
        using hype::gdxf::TexFlags;
        using hype::ui::Rect;

        constexpr hcpp::u32 logo_w = 48;
        constexpr hcpp::u32 logo_h = 48;
        constexpr hcpp::u32 tone_frames = 2048;  // ~43 ms blip at 48 kHz
        constexpr hcpp::u16 key_space = 57;

        bool demo_started = false;
        Rect last_region{};
        hcpp::u32 logo_tex = hype::gdxf::gdxf_invalid_tex;
        hcpp::u32 audio_stream = hype::gdxf::gdxf_invalid_stream;
        float cur_x = 24, cur_y = 24;
        float vel_x = 120.0f, vel_y = 90.0f;
        float prev_x = 24, prev_y = 24;
        Rect prev_draw{};
        hcpp::u64 start_ticks = 0;
        hcpp::u64 sim_steps = 0;
        bool run_marked = false;
        bool fps_marked = false;
        hcpp::i16 tone[tone_frames * 2];
        bool tone_ready = false;

        void make_logo() noexcept {
            static hcpp::u32 px[logo_w * logo_h];
            const hcpp::i32 c = static_cast<hcpp::i32>(logo_w / 2);
            for (hcpp::u32 y = 0; y < logo_h; ++y) {
                for (hcpp::u32 x = 0; x < logo_w; ++x) {
                    const hcpp::i32 dx = static_cast<hcpp::i32>(x) - c;
                    const hcpp::i32 dy = static_cast<hcpp::i32>(y) - c;
                    const hcpp::i32 d2 = dx * dx + dy * dy;
                    hcpp::u32 argb = 0;
                    if (d2 <= c * c) {
                        const hcpp::u32 t = (y * 255) / (logo_h - 1);
                        hcpp::u32 r = 12 + t / 8;
                        hcpp::u32 g = 110 + t / 3;
                        hcpp::u32 b = 220;
                        if (d2 > (c - 5) * (c - 5)) {
                            r = 0;
                            g = 230;
                            b = 255;  // bright ring
                        }
                        if (dx * dx + (dy + 10) * (dy + 10) < 49) {
                            r = 240;
                            g = 250;
                            b = 255;  // specular dot
                        }
                        argb = 0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                    px[y * logo_w + x] = argb;
                }
            }
            logo_tex = hype::gdxf::gdxf_tex_create(logo_w, logo_h, px);
        }

        void make_tone() noexcept {
            hype::audio::WaveformTone wave =
                hype::audio::waveform_tone(660, 48000);
            for (hcpp::u32 f = 0; f < tone_frames; ++f) {
                // linear decay envelope so the blip does not click
                const hcpp::i32 env =
                    14000 - static_cast<hcpp::i32>((f * 14000) / tone_frames);
                const hcpp::i16 sample = static_cast<hcpp::i16>(
                    (static_cast<hcpp::i32>(hype::audio::waveform_next(wave)) *
                     env) >>
                    15);
                tone[f * 2] = sample;
                tone[f * 2 + 1] = sample;
            }
            tone_ready = true;
        }

        void fill_surface(Surface& surf, hcpp::u32 argb) noexcept {
            for (hcpp::u32 y = 0; y < surf.height; ++y) {
                auto* row = reinterpret_cast<hcpp::u32*>(
                    surf.pixels +
                    static_cast<hcpp::size_t>(y) * surf.pitch_bytes);
                for (hcpp::u32 x = 0; x < surf.width; ++x) {
                    row[x] = argb;
                }
            }
        }

        void demo_step(float dt) noexcept {
            const hcpp::i32 w = last_region.w;
            const hcpp::i32 h = last_region.h;
            cur_x += vel_x * dt;
            cur_y += vel_y * dt;
            const float max_x = static_cast<float>(w) - logo_w;
            const float max_y = static_cast<float>(h) - logo_h;
            if (cur_x < 0) {
                cur_x = 0;
                vel_x = -vel_x;
            }
            if (cur_x > max_x) {
                cur_x = max_x;
                vel_x = -vel_x;
            }
            if (cur_y < 0) {
                cur_y = 0;
                vel_y = -vel_y;
            }
            if (cur_y > max_y) {
                cur_y = max_y;
                vel_y = -vel_y;
            }
        }

        void demo_tone() noexcept {
            if (!tone_ready || audio_stream == hype::gdxf::gdxf_invalid_stream) {
                return;
            }
            // ring drained by the pullmix source; retry next press on overflow
            hype::gdxf::gdxf_audio_write(audio_stream, tone, tone_frames);
        }

        void gdxfdemo_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty() ||
                !hype::gdxf::gdxf_initialized()) {
                return;
            }
            if (!demo_started || ctx.region != last_region) {
                if (!hype::gdxf::gdxf_bind_window(ctx.region)) {
                    return;
                }
                last_region = ctx.region;
                prev_draw = Rect{};
            }
            if (!demo_started) {
                demo_started = true;
                hype::gdxf::gdxf_set_tick_rate(60);
                hype::gdxf::gdxf_set_vsync(hype::gdxf::VSyncMode::TickLock);
                hype::gdxf::gdxf_set_fps_overlay(true);
                make_logo();
                make_tone();
                audio_stream =
                    hype::gdxf::gdxf_audio_open(hype::gdxf::AudioFormat{48000, 2},
                                                256);
                start_ticks = hype::gdxf::gdxf_ticks();
                sim_steps = 0;
            }

            FrameTiming timing{};
            if (!hype::gdxf::gdxf_begin_frame(timing)) {
                return;
            }
            prev_x = cur_x;
            prev_y = cur_y;
            const hcpp::u32 steps = hype::gdxf::gdxf_update_steps();
            sim_steps += steps;
            for (hcpp::u32 s = 0; s < steps; ++s) {
                demo_step(timing.dt);
            }
            if (hype::gdxf::gdxf_key_pressed(key_space)) {
                demo_tone();
            }

            Rect dmg = ctx.region;
            Surface surf{};
            if (hype::gdxf::gdxf_surface_acquire(surf)) {
                fill_surface(surf, 0xFF0B1220);
                const hcpp::i32 ix = static_cast<hcpp::i32>(
                    prev_x + (cur_x - prev_x) * timing.alpha);
                const hcpp::i32 iy = static_cast<hcpp::i32>(
                    prev_y + (cur_y - prev_y) * timing.alpha);
                hype::gdxf::gdxf_sprite_batch_begin();
                hype::gdxf::gdxf_sprite_draw(logo_tex, ix, iy, TexFlags::Alpha);
                hype::gdxf::gdxf_sprite_batch_end();
                const Rect cur{ix, iy, static_cast<hcpp::i32>(logo_w),
                               static_cast<hcpp::i32>(logo_h)};
                if (prev_draw.empty()) {
                    // first frame: paint the whole viewport once
                    const Rect full{0, 0, static_cast<hcpp::i32>(surf.width),
                                    static_cast<hcpp::i32>(surf.height)};
                    hype::gdxf::gdxf_present(&full, 1);
                } else {
                    const Rect dirty = prev_draw.united(cur);
                    hype::gdxf::gdxf_present(&dirty, 1);
                    // repaint only where the sprite actually moved — keeps the
                    // compositor's damage small instead of the whole window
                    dmg = Rect{ctx.region.x + dirty.x, ctx.region.y + dirty.y,
                               dirty.w, dirty.h};
                }
                prev_draw = cur;
            }
            hype::gdxf::gdxf_end_frame();
            if (!run_marked) {
                run_marked = true;
                serial::write_line("gdxf run=ok");
            }
            const hcpp::u64 elapsed = hype::gdxf::gdxf_ticks() - start_ticks;
            if (!fps_marked && elapsed >= 200) {
                fps_marked = true;
                // fixed-timestep sim rate (present fps is PIT-quantized)
                serial::write("gdxf fps=~");
                serial::write_dec32(
                    static_cast<hcpp::u32>((sim_steps * 100) / elapsed));
                serial::write("\r\n");
            }
            win::wm_invalidate(dmg);  // chain the next frame
        }

        void gdxfdemo_close(AppCtx& ctx) noexcept {
            (void)ctx;
            if (audio_stream != hype::gdxf::gdxf_invalid_stream) {
                hype::gdxf::gdxf_audio_close(audio_stream);
                audio_stream = hype::gdxf::gdxf_invalid_stream;
            }
            if (logo_tex != hype::gdxf::gdxf_invalid_tex) {
                hype::gdxf::gdxf_tex_destroy(logo_tex);
                logo_tex = hype::gdxf::gdxf_invalid_tex;
            }
            demo_started = false;
        }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::User), 0,
            "GDXFDemo", "HYPEOS://U/apps/GDXFDemo.hxer", gdxfdemo_entry,
            gdxfdemo_close, nullptr};

    }  // namespace

    const HxerHeader* gdxfdemo_app() noexcept { return &desc; }

}  // namespace hype::hxrt
