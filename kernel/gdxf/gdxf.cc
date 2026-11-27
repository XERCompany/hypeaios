#include "kernel/gdxf/gdxf.h"

#include "kernel/audio/hasio.h"
#include "kernel/functionemitkeys.h"
#include "kernel/gdxf/gdxfint.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::gdxf {

    GdxfState g_gdxf;

    namespace {

        void fill_surface(Surface& surf, hcpp::u32 argb) noexcept {
            for (hcpp::u32 y = 0; y < surf.height; ++y) {
                auto* px = reinterpret_cast<hcpp::u32*>(
                    surf.pixels +
                    static_cast<hcpp::size_t>(y) * surf.pitch_bytes);
                for (hcpp::u32 x = 0; x < surf.width; ++x) {
                    px[x] = argb;
                }
            }
        }

        struct SelftestRun {
            hcpp::u32 updates = 0;
            hcpp::u32 renders = 0;
            hcpp::u32 tex = gdxf_invalid_tex;
            bool key_seen = false;
            bool rel_seen = false;
            bool sprite_dirty = false;
        };

        void selftest_init(void* ctx) noexcept {
            (void)ctx;
            // synthetic key event exercises the functionemitkeys -> gdxf path
            input::emit_key_event(30, input::KeyState::Pressed, 0);
        }

        void selftest_update(float dt, void* ctx) noexcept {
            (void)dt;
            auto* run = static_cast<SelftestRun*>(ctx);
            if (run->updates == 0) {
                input::emit_key_event(30, input::KeyState::Released, 0);
            }
            if (run->updates == 1) {
                run->rel_seen = !gdxf_key_down(30);
            }
            ++run->updates;
            if (run->updates >= 3) {
                gdxf_request_quit();
            }
        }

        void selftest_render(Surface& surf, float alpha, void* ctx) noexcept {
            (void)alpha;
            auto* run = static_cast<SelftestRun*>(ctx);
            if (run->renders == 0) {
                // the press emitted in init was pumped into this frame's
                // edge snapshot by the first gdxf_begin_frame
                run->key_seen = gdxf_key_down(30) && gdxf_key_pressed(30);
            }
            fill_surface(surf, 0xFF14202E);
            gdxf_sprite_batch_begin();
            gdxf_sprite_draw(run->tex, -2, -2, TexFlags::FlipX);
            gdxf_sprite_draw_ex(run->tex, hype::ui::Rect{0, 0, 8, 8},
                                hype::ui::Rect{4, 4, 16, 16}, TexFlags::Alpha);
            gdxf_tilemap_draw(run->tex, 8, 8, nullptr, 0, 0, 0, 0);
            static const hcpp::u8 tmap[4] = {1, 1, 1, 1};
            gdxf_tilemap_draw(run->tex, 8, 8, tmap, 2, 2, 0, 24);
            gdxf_sprite_batch_end();
            run->sprite_dirty = g_gdxf.dirty_count != 0;
            ++run->renders;
        }

    }  // namespace

    bool gdxf_initialize(hcpp::graphics::Framebuffer& scanout) noexcept {
        if (g_gdxf.initialized) {
            return true;
        }
        if (!scanout.valid() || !memory::pmm_initialized()) {
            serial::write_line("gdxf init=fail");
            return false;
        }
        g_gdxf.scanout = scanout;
        g_gdxf.windowed = false;
        g_gdxf.present_dst = hype::ui::Rect{
            0, 0, static_cast<hcpp::i32>(scanout.width()),
            static_cast<hcpp::i32>(scanout.height())};
        if (!gdxf_surface_resize(scanout.width(), scanout.height())) {
            serial::write_line("gdxf init=fail");
            return false;
        }
        gdxf_input_hook();
        g_gdxf.last_us = gdxf_time_us();
        g_gdxf.initialized = true;
        serial::write("gdxf init=ok surf=");
        serial::write_dec32(g_gdxf.surf_w);
        serial::write_char('x');
        serial::write_dec32(g_gdxf.surf_h);
        serial::write("\r\n");
        return true;
    }

    bool gdxf_initialized() noexcept { return g_gdxf.initialized; }

    bool gdxf_begin_frame(FrameTiming& timing) noexcept {
        if (!g_gdxf.initialized || g_gdxf.quit) {
            return false;
        }
        const hcpp::u64 now = gdxf_time_us();
        hcpp::u64 delta = now >= g_gdxf.last_us ? now - g_gdxf.last_us : 0;
        if (delta == 0) {
            delta = 1;
        }
        if (delta > gdxf_frame_clamp_us) {
            delta = gdxf_frame_clamp_us;
        }
        g_gdxf.last_us = now;
        g_gdxf.frame_start_us = now;
        g_gdxf.frame_us_avg = g_gdxf.frame_us_avg == 0
                                  ? static_cast<hcpp::u32>(delta)
                                  : (g_gdxf.frame_us_avg * 7 +
                                     static_cast<hcpp::u32>(delta)) /
                                        8;
        g_gdxf.acc_us += delta;
        hcpp::u64 steps =
            g_gdxf.step_us != 0 ? g_gdxf.acc_us / g_gdxf.step_us : 0;
        if (steps > gdxf_max_sim_steps) {
            steps = gdxf_max_sim_steps;
            g_gdxf.acc_us = 0;  // drop backlog; the clamp already fired
        } else {
            g_gdxf.acc_us -= steps * g_gdxf.step_us;
        }
        g_gdxf.steps_due = static_cast<hcpp::u32>(steps);
        gdxf_input_pump();
        ++g_gdxf.frame_count;
        g_gdxf.presented = false;
        timing.frame = g_gdxf.frame_count;
        timing.t_us = now;
        timing.delta_us = static_cast<hcpp::u32>(delta);
        timing.dt = static_cast<float>(g_gdxf.step_us) / 1000000.0f;
        timing.alpha = g_gdxf.step_us != 0
                           ? static_cast<float>(g_gdxf.acc_us) /
                                 static_cast<float>(g_gdxf.step_us)
                           : 0.0f;
        return true;
    }

    void gdxf_end_frame() noexcept {
        if (!g_gdxf.initialized) {
            return;
        }
        if (!g_gdxf.presented) {
            gdxf_present(nullptr, 0);
        }
        if (g_gdxf.vsync != VSyncMode::Uncapped) {
            const hcpp::u64 now = gdxf_time_us();
            const hcpp::u64 target = g_gdxf.frame_start_us + g_gdxf.step_us;
            if (target > now) {
                const hcpp::u64 behind_ticks = (target - now) / 10000ull;
                if (behind_ticks != 0) {
                    interrupts::pit_wait_ticks(behind_ticks);
                }
            }
        }
        audio::hasio_pump_active();
    }

    hcpp::u32 gdxf_run(const GameCallbacks& cb) noexcept {
        if (!g_gdxf.initialized || cb.update == nullptr || cb.render == nullptr) {
            return 1;
        }
        g_gdxf.quit = false;
        if (cb.init != nullptr) {
            cb.init(cb.ctx);
        }
        for (;;) {
            FrameTiming timing{};
            if (!gdxf_begin_frame(timing)) {
                break;
            }
            for (hcpp::u32 s = 0; s < g_gdxf.steps_due; ++s) {
                cb.update(timing.dt, cb.ctx);
            }
            Surface surf{};
            if (gdxf_surface_acquire(surf)) {
                cb.render(surf, timing.alpha, cb.ctx);
            }
            gdxf_end_frame();
        }
        return 0;
    }

    hcpp::u32 gdxf_update_steps() noexcept { return g_gdxf.steps_due; }

    void gdxf_request_quit() noexcept { g_gdxf.quit = true; }

    void gdxf_clear_quit() noexcept { g_gdxf.quit = false; }

    void gdxf_set_tick_rate(hcpp::u32 hz) noexcept {
        if (hz < 20) {
            hz = 20;
        }
        if (hz > 240) {
            hz = 240;
        }
        g_gdxf.tick_hz = hz;
        g_gdxf.step_us = 1000000ull / hz;
    }

    void gdxf_set_vsync(VSyncMode mode) noexcept { g_gdxf.vsync = mode; }

    hcpp::u32 gdxf_frame_us() noexcept { return g_gdxf.frame_us_avg; }

    void gdxf_set_fps_overlay(bool enabled) noexcept {
        g_gdxf.fps_overlay = enabled;
    }

    bool gdxf_selftest() noexcept {
        if (!g_gdxf.initialized) {
            serial::write_line("gdxf selftest=skipped");
            return false;
        }
        bool ok = true;

        const hcpp::u32 tex = gdxf_tex_create(8, 8, nullptr);
        const bool tex_ok = tex != gdxf_invalid_tex;

        // fixed-step loop over gdxf_run; quits after 3 sim steps
        gdxf_bind_window(hype::ui::Rect{4, 700, 48, 48});
        static SelftestRun run;
        run = SelftestRun{};
        run.tex = tex;
        const GameCallbacks cb{selftest_init, selftest_update, selftest_render,
                               &run};
        const hcpp::u32 rc = gdxf_run(cb);
        const bool loop_ok = rc == 0 && run.updates >= 3 && run.renders >= 1;
        serial::write("gdxf loop=");
        serial::write_line(loop_ok ? "ok" : "fail");
        serial::write("gdxf sprite batch=");
        serial::write_line(tex_ok && run.sprite_dirty ? "ok" : "fail");
        serial::write("gdxf input=");
        serial::write_line(run.key_seen && run.rel_seen ? "ok" : "fail");
        serial::write("gdxf pad=");
        serial::write_dec32(gdxf_gamepad_count());
        serial::write("\r\n");
        ok = ok && loop_ok && tex_ok && run.sprite_dirty && run.key_seen &&
             run.rel_seen;

        const AudioFormat fmt{48000, 2};
        const hcpp::u32 stream = gdxf_audio_open(fmt, 256);
        if (stream == gdxf_invalid_stream) {
            serial::write_line("gdxf audio=absent");
        } else {
            hcpp::i16 frames[64 * 2];
            for (hcpp::u32 i = 0; i < 64 * 2; ++i) {
                frames[i] = static_cast<hcpp::i16>((i % 32) * 512);
            }
            const bool audio_ok =
                gdxf_audio_write(stream, frames, 64) &&
                gdxf_audio_queued_frames(stream) == 64;
            gdxf_audio_close(stream);
            serial::write("gdxf audio=");
            serial::write_line(audio_ok ? "ok" : "fail");
            ok = ok && audio_ok;
        }

        if (tex_ok) {
            gdxf_tex_destroy(tex);
        }
        gdxf_clear_quit();
        gdxf_unbind_window();
        serial::write("gdxf selftest=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

}  // namespace hype::gdxf
