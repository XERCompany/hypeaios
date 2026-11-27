#include "kernel/audio/hasio.h"

#include "kernel/audio/audio-serivce.h"
#include "kernel/audio/audiopcicard.h"
#include "kernel/audio/hda.h"
#include "kernel/audio/latmon.h"
#include "kernel/audio/pullmix.h"
#include "kernel/audio/waveform.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"

namespace hype::audio {
    namespace {

        hda::HdaController stream_controller;
        hda::HdaPlayback stream_playback;
        HasioCallback stream_callback;
        HasioFormat stream_format;
        hcpp::u32 stream_handle = hasio_invalid_handle;
        hcpp::u32 next_handle = 1;
        hcpp::u32 period_frames;
        hcpp::u32 period_bytes;
        hcpp::u32 frame_bytes;
        hcpp::u16 ring_entries;
        hcpp::u8 *ring;
        hcpp::u16 last_play_index;
        bool stream_open;
        bool stream_running;

        hcpp::u16 pick_ring_entries(hcpp::u32 rate, hcpp::u32 frames) noexcept {
            if (rate == 0 || frames == 0) {
                return 4;
            }
            const hcpp::u32 budget_frames = (rate * hasio_latency_budget_us) / 1000000u;
            hcpp::u32 entries = budget_frames / frames;
            if (entries < 4) {
                entries = 4;
            }
            if (entries > hasio_max_ring_entries) {
                entries = hasio_max_ring_entries;
            }
            return static_cast<hcpp::u16>(entries);
        }

        hcpp::u32 stream_position() noexcept {
            if (!stream_open || stream_playback.stream_offset == 0 ||
                stream_playback.buffer_bytes == 0) {
                return 0;
            }
            const hcpp::u32 pos =
                hda::hda_playback_position(stream_controller, stream_playback);
            return pos % stream_playback.buffer_bytes;
        }

        void fill_period(hcpp::u16 index) noexcept {
            auto *buf = reinterpret_cast<hcpp::i16 *>(ring + index * period_bytes);
            if (stream_callback != nullptr) {
                stream_callback(buf, period_frames);
            } else {
                for (hcpp::u32 i = 0; i < period_bytes / sizeof(hcpp::i16); ++i) {
                    buf[i] = 0;
                }
            }
        }

        bool open_hda(hcpp::u16 format, hcpp::u32 bytes_per_period,
                      hcpp::u16 entries) noexcept {
            for (hcpp::size_t i = 0; i < audio_chip_count(); ++i) {
                if (audio_cards()[i].chip != AudioChip::IntelHda) {
                    continue;
                }
                if (!hda::hda_bind(i, stream_controller) ||
                    !hda::hda_reset(stream_controller)) {
                    return false;
                }
                hda::HdaCodec codecs[hda::max_codecs];
                const hcpp::u8 found =
                    hda::hda_scan_codecs(stream_controller, codecs, hda::max_codecs);
                if (found == 0) {
                    return false;
                }
                return hda::hda_stream_open(stream_controller, codecs[0].address,
                                             stream_playback, format, bytes_per_period,
                                             entries);
            }
            return false;
        }

        struct SineSource {
            WaveformTone tone;
            hcpp::i32 amplitude;
        };

        SineSource sine_a;
        SineSource sine_b;

        void sine_fill(hcpp::i16 *dst, hcpp::u32 frames, void *ctx) noexcept {
            auto *sine = static_cast<SineSource *>(ctx);
            for (hcpp::u32 f = 0; f < frames; ++f) {
                const hcpp::i32 sample =
                    (static_cast<hcpp::i32>(waveform_next(sine->tone)) * sine->amplitude) >>
                    15;
                dst[f * 2] = static_cast<hcpp::i16>(sample);
                dst[f * 2 + 1] = static_cast<hcpp::i16>(sample);
            }
        }

        void mix_period(hcpp::i16 *buf, hcpp::u32 frames) noexcept {
            pullmix_render(buf, frames, stream_format.channels);
        }

    }  // namespace

    bool hasio_ready() noexcept {
        return stream_running;
    }

    hcpp::u32 hasio_open_stream(HasioFormat fmt, hcpp::u32 frames) noexcept {
        if (stream_open || chips() == nullptr) {
            return hasio_invalid_handle;
        }
        if (frames < hasio_min_period_frames || frames > hasio_max_period_frames ||
            fmt.channels == 0 || fmt.channels > 8 || fmt.bits == 0 ||
            (fmt.bits % 8) != 0) {
            return hasio_invalid_handle;
        }
        const hcpp::u16 word = hda::hda_format_word(fmt.rate, fmt.channels, fmt.bits);
        if (word == hda::hda_format_invalid) {
            return hasio_invalid_handle;
        }
        frame_bytes = static_cast<hcpp::u32>(fmt.channels) * (fmt.bits / 8);
        period_bytes = frames * frame_bytes;
        ring_entries = pick_ring_entries(fmt.rate, frames);
        if (!open_hda(word, period_bytes, ring_entries)) {
            return hasio_invalid_handle;
        }
        ring = reinterpret_cast<hcpp::u8 *>(
            static_cast<hcpp::uintptr_t>(stream_playback.buffer_phys));
        stream_format = fmt;
        period_frames = frames;
        stream_callback = nullptr;
        last_play_index = 0;
        stream_open = true;
        stream_running = false;
        latmon_reset();
        stream_handle = next_handle++;
        return stream_handle;
    }

    bool hasio_set_callback(hcpp::u32 handle, HasioCallback callback) noexcept {
        if (!stream_open || handle != stream_handle) {
            return false;
        }
        stream_callback = callback;
        return true;
    }

    bool hasio_start(hcpp::u32 handle) noexcept {
        if (!stream_open || stream_running || handle != stream_handle) {
            return false;
        }
        for (hcpp::u16 i = 0; i < ring_entries; ++i) {
            fill_period(i);
        }
        last_play_index = 0;
        if (!hda::hda_stream_run(stream_controller, stream_playback)) {
            return false;
        }
        stream_running = true;
        return true;
    }

    bool hasio_stop(hcpp::u32 handle) noexcept {
        if (!stream_open || handle != stream_handle) {
            return false;
        }
        const bool halted = hda::hda_stream_halt(stream_controller, stream_playback);
        stream_running = false;
        return halted;
    }

    HasioLatency hasio_latency(hcpp::u32 handle) noexcept {
        HasioLatency latency{};
        if (!stream_open || handle != stream_handle || stream_format.rate == 0) {
            return latency;
        }
        latency.period_frames = period_frames;
        const hcpp::u64 period_us =
            (static_cast<hcpp::u64>(period_frames) * 1000000ull) / stream_format.rate;
        latency.ring_us = static_cast<hcpp::u32>(period_us * ring_entries);
        latency.est_us = static_cast<hcpp::u32>(period_us * (ring_entries - 1));
        return latency;
    }

    hcpp::u32 hasio_pump(hcpp::u32 handle) noexcept {
        if (!stream_open || !stream_running || handle != stream_handle) {
            return 0;
        }
        const hcpp::u32 pos = stream_position();
        const hcpp::u32 play_index = pos / period_bytes;
        hcpp::u32 done = (play_index + ring_entries - last_play_index) % ring_entries;
        if (done >= ring_entries) {
            done = ring_entries - 1;
        }
        latmon_poll(pos);
        for (hcpp::u32 i = 0; i < done; ++i) {
            const hcpp::u16 index =
                static_cast<hcpp::u16>((last_play_index + i) % ring_entries);
            fill_period(index);
            const hcpp::u32 start_byte = index * period_bytes;
            const hcpp::u32 end_byte =
                ((index + 1) % ring_entries) * period_bytes;
            hcpp::u32 queued =
                (start_byte + stream_playback.buffer_bytes - pos) %
                stream_playback.buffer_bytes;
            if (queued == 0) {
                queued = stream_playback.buffer_bytes;
            }
            latmon_fill(start_byte, end_byte, queued, frame_bytes, stream_format.rate,
                        stream_playback.buffer_bytes);
        }
        last_play_index = static_cast<hcpp::u16>(play_index);
        return done;
    }

    hcpp::u32 hasio_pump_active() noexcept {
        return hasio_pump(stream_handle);
    }

    hcpp::u32 hasio_ring_entries(hcpp::u32 handle) noexcept {
        return stream_open && handle == stream_handle ? ring_entries : 0;
    }

    bool hasio_service_start() noexcept {
        if (stream_running) {
            return true;
        }
        HasioFormat fmt{48000, 2, 16};
        const hcpp::u32 handle = hasio_open_stream(fmt, 128);
        if (handle == hasio_invalid_handle) {
            serial::write_line("hasio open=fail");
            return false;
        }
        serial::write_line("hasio open=ok");

        pullmix_reset();
        pullmix_set_output_rate(fmt.rate);
        sine_a.tone = waveform_tone(440, fmt.rate);
        sine_a.amplitude = 16000;
        sine_b.tone = waveform_tone(554, fmt.rate);
        sine_b.tone.phase = 16384;
        sine_b.amplitude = 16000;
        const hcpp::u32 src_a =
            pullmix_add_source(sine_fill, &sine_a, fmt.channels, fmt.rate);
        const hcpp::u32 src_b =
            pullmix_add_source(sine_fill, &sine_b, fmt.channels, fmt.rate);
        serial::write("pullmix src=");
        serial::write_dec32(pullmix_source_count());
        serial::write("\r\n");
        if (src_a == pullmix_invalid_id || src_b == pullmix_invalid_id) {
            serial::write_line("pullmix sum=fail");
            hasio_stop(handle);
            return false;
        }

        bool ok = true;
        hcpp::i16 scratch[128 * 2];
        const hcpp::u32 clip0 = pullmix_clip_events();
        pullmix_render(scratch, 128, fmt.channels);
        hcpp::u32 peak = 0;
        bool nonzero = false;
        for (hcpp::u32 i = 0; i < 128 * 2; ++i) {
            const hcpp::u32 mag =
                scratch[i] < 0 ? static_cast<hcpp::u32>(-scratch[i])
                               : static_cast<hcpp::u32>(scratch[i]);
            if (mag > peak) {
                peak = mag;
            }
            nonzero = nonzero || scratch[i] != 0;
        }
        const bool sum_ok = nonzero && peak > 16000 &&
                            pullmix_clip_events() == clip0 &&
                            pullmix_clip16(70000) == 32767 &&
                            pullmix_clip16(-70000) == -32768;
        serial::write("pullmix sum=");
        serial::write_line(sum_ok ? "ok" : "fail");
        ok = ok && sum_ok;

        if (!hasio_set_callback(handle, mix_period) || !hasio_start(handle)) {
            serial::write_line("hasio start=fail");
            return false;
        }
        serial::write_line("hasio start=ok");

        hcpp::u32 periods = 0;
        for (hcpp::u32 iter = 0; iter < 0x2000000 && periods < 32; ++iter) {
            periods += hasio_pump(handle);
            asm volatile("pause");
        }
        serial::write("hasio periods=");
        serial::write_dec32(periods);
        serial::write(" ring=");
        serial::write_dec32(hasio_ring_entries(handle));
        serial::write(" lpib=");
        serial::write_hex64(stream_position());
        serial::write(" raw=");
        serial::write_hex64(
            hda::hda_playback_position(stream_controller, stream_playback));
        serial::write("\r\n");
        ok = ok && periods >= 32;

        hcpp::u64 bench_tick = interrupts::pit_ticks();
        while (interrupts::pit_ticks() == bench_tick) {
            asm volatile("pause");
        }
        bench_tick = interrupts::pit_ticks();
        hcpp::u32 reps = 0;
        while (interrupts::pit_ticks() == bench_tick) {
            pullmix_render(scratch, 128, fmt.channels);
            ++reps;
        }
        const hcpp::u32 bench_us = reps != 0 ? 10000 / reps : 10000;
        serial::write("hasio bench=");
        serial::write_dec32(bench_us);
        serial::write("us\r\n");

        const HasioLatency latency = hasio_latency(handle);
        serial::write("hasio lat=");
        serial::write_dec32(latmon_max_us());
        serial::write("us est=");
        serial::write_dec32(latency.est_us);
        serial::write("us ticks=");
        serial::write_dec32(static_cast<hcpp::u32>(latmon_play_ticks()));
        serial::write("\r\n");
        const hcpp::u32 worst_us =
            latmon_max_us() > latency.est_us ? latmon_max_us() : latency.est_us;
        const bool lat_ok = worst_us < 10000;
        serial::write("hasio lat<10ms ");
        serial::write_line(lat_ok ? "ok" : "fail");
        ok = ok && lat_ok;
        return ok;
    }

    bool hasio_service_stop() noexcept {
        return hasio_stop(stream_handle);
    }

}  // namespace hype::audio
