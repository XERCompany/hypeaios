#include "kernel/audio/pullmix.h"

#include "kernel/audio/audioapi.h"
#include "kernel/audio/waveform.h"
#include "kernel/serial.hpp"

namespace hype::audio {
    namespace {

        struct PullSource {
            bool active;
            hcpp::u32 id;
            PullmixFill fill;
            void *ctx;
            hcpp::u8 channels;
            hcpp::u32 rate;
            hcpp::i16 ring[pullmix_ring_frames * pullmix_max_channels];
            hcpp::u32 head;
            hcpp::u32 tail;
            hcpp::u32 underruns;
            hcpp::u32 overflows;
            hcpp::u32 src_pos;
            hcpp::u16 gain_q8;
            hcpp::i16 pan;
            bool muted;
            bool solo;
        };

        PullSource sources[pullmix_max_sources];
        hcpp::u32 source_count;
        hcpp::u32 next_id = 1;
        hcpp::u32 clip_events;
        hcpp::u32 output_rate = pullmix_default_rate;
        hcpp::u16 master_gain = pullmix_gain_unity_q8;
        hcpp::i32 accum[pullmix_ring_frames * pullmix_max_channels];
        hcpp::i16 mixbuf[pullmix_ring_frames * pullmix_max_channels];

        hcpp::u32 ring_tail_space(hcpp::u32 offset) noexcept {
            return pullmix_ring_frames - offset;
        }

        PullSource *source_by_id(hcpp::u32 id) noexcept {
            for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
                if (sources[i].active && sources[i].id == id) {
                    return &sources[i];
                }
            }
            return nullptr;
        }

        void source_top_up(PullSource &source, hcpp::u32 frames) noexcept {
            hcpp::u32 guard = 0;
            while (source.tail - source.head < frames && guard++ < 8) {
                const hcpp::u32 avail = source.tail - source.head;
                const hcpp::u32 free_frames = pullmix_ring_frames - avail;
                if (free_frames == 0) {
                    source.head = source.tail - pullmix_ring_frames + frames;
                    ++source.overflows;
                    continue;
                }
                const hcpp::u32 offset = source.tail % pullmix_ring_frames;
                hcpp::u32 chunk = ring_tail_space(offset);
                const hcpp::u32 want = frames - avail;
                if (chunk > want) {
                    chunk = want;
                }
                if (chunk > free_frames) {
                    chunk = free_frames;
                }
                if (chunk == 0) {
                    break;
                }
                source.fill(source.ring + offset * source.channels, chunk, source.ctx);
                source.tail += chunk;
            }
        }

        hcpp::u32 source_frames(PullSource &source, hcpp::u32 frames) noexcept {
            const hcpp::u32 sc = source.channels;
            if (output_rate == 0 || source.rate == output_rate) {
                source.src_pos = 0;
                source_top_up(source, frames);
                const hcpp::u32 avail = source.tail - source.head;
                const hcpp::u32 take = avail < frames ? avail : frames;
                for (hcpp::u32 f = 0; f < take; ++f) {
                    const hcpp::u32 offset = (source.head + f) % pullmix_ring_frames;
                    const hcpp::i16 *frame = source.ring + offset * sc;
                    for (hcpp::u32 c = 0; c < sc; ++c) {
                        mixbuf[f * sc + c] = frame[c];
                    }
                }
                source.head += take;
                return take;
            }
            const hcpp::u32 step_q16 = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(source.rate) << 16) / output_rate);
            hcpp::u32 want_in = static_cast<hcpp::u32>(
                ((static_cast<hcpp::u64>(frames) * step_q16) >> 16)) + 2;
            if (want_in > pullmix_ring_frames) {
                want_in = pullmix_ring_frames;
            }
            source_top_up(source, want_in);
            const hcpp::u32 avail = source.tail - source.head;
            hcpp::u32 pos = source.src_pos;
            hcpp::u32 take = 0;
            for (hcpp::u32 f = 0; f < frames; ++f) {
                const hcpp::u32 index = pos >> 16;
                if (index + 1 >= avail) {
                    break;
                }
                const hcpp::u32 frac = pos & 0xFFFF;
                const hcpp::i16 *frame_a =
                    source.ring + ((source.head + index) % pullmix_ring_frames) * sc;
                const hcpp::i16 *frame_b =
                    source.ring + ((source.head + index + 1) % pullmix_ring_frames) * sc;
                for (hcpp::u32 c = 0; c < sc; ++c) {
                    mixbuf[f * sc + c] = pullmix_clip16(
                        frame_a[c] + static_cast<hcpp::i32>(
                                         (static_cast<hcpp::i64>(frame_b[c] - frame_a[c]) *
                                          frac) >>
                                         16));
                }
                pos += step_q16;
                take = f + 1;
            }
            hcpp::u32 consumed = pos >> 16;
            if (consumed > avail) {
                consumed = avail;
            }
            source.head += consumed;
            source.src_pos = pos - (consumed << 16);
            return take;
        }

        struct TestSine {
            WaveformTone tone;
            hcpp::i32 amplitude;
            hcpp::u8 channels;
        };

        TestSine test_sines[pullmix_max_sources];

        void test_sine_fill(hcpp::i16 *dst, hcpp::u32 frames, void *ctx) noexcept {
            auto *sine = static_cast<TestSine *>(ctx);
            for (hcpp::u32 f = 0; f < frames; ++f) {
                const hcpp::i16 sample = static_cast<hcpp::i16>(
                    (static_cast<hcpp::i32>(waveform_next(sine->tone)) * sine->amplitude) >>
                    15);
                for (hcpp::u8 c = 0; c < sine->channels; ++c) {
                    dst[f * sine->channels + c] = sample;
                }
            }
        }

        hcpp::u32 mix_peak(const hcpp::i16 *samples, hcpp::u32 count) noexcept {
            hcpp::u32 peak = 0;
            for (hcpp::u32 i = 0; i < count; ++i) {
                const hcpp::u32 mag =
                    samples[i] < 0 ? static_cast<hcpp::u32>(-samples[i])
                                   : static_cast<hcpp::u32>(samples[i]);
                if (mag > peak) {
                    peak = mag;
                }
            }
            return peak;
        }

        hcpp::u32 add_test_sine(hcpp::u32 slot, hcpp::u32 freq, hcpp::u32 rate,
                                hcpp::i32 amplitude, hcpp::u8 channels) noexcept {
            test_sines[slot].tone = waveform_tone(freq, rate);
            test_sines[slot].amplitude = amplitude;
            test_sines[slot].channels = channels;
            return pullmix_add_source(test_sine_fill, &test_sines[slot], channels, rate);
        }

    }  // namespace

    void pullmix_reset() noexcept {
        for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
            sources[i] = {};
        }
        source_count = 0;
        next_id = 1;
        clip_events = 0;
        output_rate = pullmix_default_rate;
        master_gain = pullmix_gain_unity_q8;
    }

    hcpp::u32 pullmix_add_source(PullmixFill fill, void *ctx, hcpp::u8 channels,
                               hcpp::u32 rate) noexcept {
        if (fill == nullptr || channels == 0 || channels > pullmix_max_channels) {
            return pullmix_invalid_id;
        }
        for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
            if (sources[i].active) {
                continue;
            }
            sources[i] = {};
            sources[i].active = true;
            sources[i].id = next_id++;
            sources[i].fill = fill;
            sources[i].ctx = ctx;
            sources[i].channels = channels;
            sources[i].rate = rate;
            sources[i].gain_q8 = pullmix_gain_unity_q8;
            ++source_count;
            return sources[i].id;
        }
        return pullmix_invalid_id;
    }

    bool pullmix_remove_source(hcpp::u32 id) noexcept {
        for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
            if (sources[i].active && sources[i].id == id) {
                sources[i] = {};
                --source_count;
                return true;
            }
        }
        return false;
    }

    hcpp::u32 pullmix_source_count() noexcept {
        return source_count;
    }

    bool pullmix_source_info(hcpp::u32 index, PullmixSourceInfo &info) noexcept {
        if (index >= pullmix_max_sources || !sources[index].active) {
            return false;
        }
        const PullSource &source = sources[index];
        info.id = source.id;
        info.rate = source.rate;
        info.channels = source.channels;
        info.active = source.active;
        info.muted = source.muted;
        info.solo = source.solo;
        info.pan = source.pan;
        info.gain_q8 = source.gain_q8;
        info.underruns = source.underruns;
        info.overflows = source.overflows;
        return true;
    }

    void pullmix_set_output_rate(hcpp::u32 rate) noexcept {
        output_rate = rate;
        for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
            sources[i].src_pos = 0;
        }
    }

    hcpp::u32 pullmix_output_rate() noexcept {
        return output_rate;
    }

    bool pullmix_set_source_gain(hcpp::u32 id, hcpp::u16 gain_q8) noexcept {
        PullSource *source = source_by_id(id);
        if (source == nullptr) {
            return false;
        }
        source->gain_q8 = gain_q8;
        return true;
    }

    bool pullmix_set_source_pan(hcpp::u32 id, hcpp::i16 pan) noexcept {
        PullSource *source = source_by_id(id);
        if (source == nullptr) {
            return false;
        }
        if (pan < pullmix_pan_left) {
            pan = pullmix_pan_left;
        }
        if (pan > pullmix_pan_right) {
            pan = pullmix_pan_right;
        }
        source->pan = pan;
        return true;
    }

    bool pullmix_set_source_muted(hcpp::u32 id, bool muted) noexcept {
        PullSource *source = source_by_id(id);
        if (source == nullptr) {
            return false;
        }
        source->muted = muted;
        return true;
    }

    bool pullmix_set_source_solo(hcpp::u32 id, bool solo) noexcept {
        PullSource *source = source_by_id(id);
        if (source == nullptr) {
            return false;
        }
        source->solo = solo;
        return true;
    }

    void pullmix_set_master_gain(hcpp::u16 gain_q8) noexcept {
        master_gain = gain_q8;
    }

    hcpp::u16 pullmix_master_gain() noexcept {
        return master_gain;
    }

    hcpp::u32 pullmix_render(hcpp::i16 *out, hcpp::u32 frames, hcpp::u8 channels) noexcept {
        if (out == nullptr || frames == 0 || channels == 0 || frames > pullmix_ring_frames ||
            channels > pullmix_max_channels) {
            return 0;
        }
        const hcpp::u32 samples = frames * channels;
        for (hcpp::u32 i = 0; i < samples; ++i) {
            accum[i] = 0;
        }
        bool any_solo = false;
        for (hcpp::u32 s = 0; s < pullmix_max_sources; ++s) {
            if (sources[s].active && sources[s].solo) {
                any_solo = true;
                break;
            }
        }
        for (hcpp::u32 s = 0; s < pullmix_max_sources; ++s) {
            PullSource &source = sources[s];
            if (!source.active || source.muted || (any_solo && !source.solo)) {
                continue;
            }
            const hcpp::u32 sc = source.channels;
            const hcpp::u32 take = source_frames(source, frames);
            if (take < frames) {
                ++source.underruns;
            }
            hcpp::i32 chan_gain[pullmix_max_channels];
            if (channels == 2) {
                const hcpp::i32 pan_left = 256 - (source.pan > 0 ? source.pan : 0);
                const hcpp::i32 pan_right = 256 + (source.pan < 0 ? source.pan : 0);
                chan_gain[0] = (source.gain_q8 * pan_left) >> 8;
                chan_gain[1] = (source.gain_q8 * pan_right) >> 8;
            } else {
                chan_gain[0] = source.gain_q8;
            }
            for (hcpp::u32 f = 0; f < take; ++f) {
                for (hcpp::u32 c = 0; c < channels; ++c) {
                    const hcpp::u32 tap = c < sc ? c : sc - 1;
                    accum[f * channels + c] +=
                        (mixbuf[f * sc + tap] * chan_gain[c]) >> 8;
                }
            }
        }
        for (hcpp::u32 i = 0; i < samples; ++i) {
            const hcpp::i32 value = (accum[i] * master_gain) >> 8;
            if (value > 32767 || value < -32768) {
                ++clip_events;
            }
            out[i] = pullmix_clip16(value);
        }
        return frames;
    }

    hcpp::u32 pullmix_clip_events() noexcept {
        return clip_events;
    }

    bool pullmix_selftest() noexcept {
        bool ok = true;

        static const hcpp::u32 cover_rates[] = {8000, 16000, 22050, 32000,
                                                44100, 48000, 96000};
        static hcpp::i16 src_in[256];
        static hcpp::i16 src_out[1600];
        hcpp::u32 covered = 0;
        for (hcpp::u32 r = 0; r < sizeof(cover_rates) / sizeof(cover_rates[0]); ++r) {
            const hcpp::u32 in_rate = cover_rates[r];
            waveform_render_sine(src_in, 256, 440, in_rate, 1);
            const hcpp::u32 expected = src_output_frames(256, in_rate, 48000);
            const hcpp::u32 produced =
                src_convert(src_in, 256, in_rate, src_out, 1600, 48000);
            bool rate_ok = produced == expected && produced != 0 && src_out[0] == src_in[0];
            const hcpp::u32 in_peak = mix_peak(src_in, 256);
            const hcpp::i32 bound = static_cast<hcpp::i32>(in_peak) / 8 + 128;
            for (hcpp::u32 i = 1; i < produced && rate_ok; ++i) {
                const hcpp::i32 step =
                    static_cast<hcpp::i32>(src_out[i]) - src_out[i - 1];
                if (step > bound || step < -bound ||
                    src_out[i] > static_cast<hcpp::i32>(in_peak) + 16 ||
                    src_out[i] < -static_cast<hcpp::i32>(in_peak) - 16) {
                    rate_ok = false;
                }
            }
            if (rate_ok) {
                ++covered;
            }
        }
        serial::write("src rates=");
        serial::write_dec32(covered);
        serial::write("\r\n");
        ok = ok && covered == sizeof(cover_rates) / sizeof(cover_rates[0]);

        static hcpp::i16 out[pullmix_ring_frames * 2];

        pullmix_reset();
        pullmix_set_output_rate(48000);
        const hcpp::u32 id_44k = add_test_sine(0, 330, 44100, 12000, 2);
        pullmix_render(out, 128, 2);
        PullmixSourceInfo info{};
        pullmix_source_info(0, info);
        const hcpp::u32 mix_peak44 = mix_peak(out, 128 * 2);
        const bool mix_src_ok = id_44k != pullmix_invalid_id && mix_peak44 != 0 &&
                                info.underruns == 0 && pullmix_clip_events() == 0;
        serial::write("mix src=44.1->48 ");
        serial::write_line(mix_src_ok ? "ok" : "fail");
        ok = ok && mix_src_ok;

        pullmix_reset();
        pullmix_set_output_rate(48000);
        const hcpp::u32 id_pan = add_test_sine(0, 330, 48000, 12000, 2);
        pullmix_set_source_pan(id_pan, pullmix_pan_right);
        pullmix_render(out, 64, 2);
        bool pan_ok = id_pan != pullmix_invalid_id;
        hcpp::u32 right_peak = 0;
        for (hcpp::u32 f = 0; f < 64 && pan_ok; ++f) {
            if (out[f * 2] != 0) {
                pan_ok = false;
            }
            const hcpp::u32 mag =
                out[f * 2 + 1] < 0 ? static_cast<hcpp::u32>(-out[f * 2 + 1])
                                   : static_cast<hcpp::u32>(out[f * 2 + 1]);
            if (mag > right_peak) {
                right_peak = mag;
            }
        }
        pan_ok = pan_ok && right_peak != 0;
        serial::write("mix pan=");
        serial::write_line(pan_ok ? "ok" : "fail");
        ok = ok && pan_ok;

        pullmix_reset();
        pullmix_set_output_rate(48000);
        const hcpp::u32 id_mute = add_test_sine(0, 330, 48000, 12000, 2);
        pullmix_set_source_muted(id_mute, true);
        pullmix_render(out, 64, 2);
        const bool mute_ok =
            id_mute != pullmix_invalid_id && mix_peak(out, 64 * 2) == 0;
        serial::write("mix mute=");
        serial::write_line(mute_ok ? "ok" : "fail");
        ok = ok && mute_ok;

        pullmix_reset();
        pullmix_set_output_rate(48000);
        const hcpp::u32 id_quiet = add_test_sine(0, 330, 48000, 5000, 2);
        const hcpp::u32 id_loud = add_test_sine(1, 660, 48000, 30000, 2);
        pullmix_set_source_solo(id_quiet, true);
        pullmix_render(out, 64, 2);
        const hcpp::u32 solo_peak = mix_peak(out, 64 * 2);
        const bool solo_ok = id_quiet != pullmix_invalid_id &&
                             id_loud != pullmix_invalid_id && solo_peak != 0 &&
                             solo_peak < 8000 && pullmix_clip_events() == 0;
        serial::write("mix solo=");
        serial::write_line(solo_ok ? "ok" : "fail");
        ok = ok && solo_ok;

        pullmix_reset();
        pullmix_set_output_rate(48000);
        add_test_sine(0, 330, 48000, 16000, 2);
        pullmix_set_master_gain(128);
        pullmix_render(out, 64, 2);
        const hcpp::u32 half_peak = mix_peak(out, 64 * 2);
        pullmix_set_master_gain(pullmix_gain_unity_q8);
        pullmix_render(out, 64, 2);
        const hcpp::u32 full_peak = mix_peak(out, 64 * 2);
        const bool master_ok = half_peak != 0 && full_peak > half_peak &&
                               half_peak <= full_peak / 2 + 256 &&
                               pullmix_master_gain() == pullmix_gain_unity_q8;
        serial::write("mix master=");
        serial::write_line(master_ok ? "ok" : "fail");
        ok = ok && master_ok;

        pullmix_reset();
        pullmix_set_output_rate(48000);
        const hcpp::u32 id_fast = add_test_sine(0, 330, 96000, 12000, 2);
        pullmix_render(out, 128, 2);
        PullmixSourceInfo fast_info{};
        pullmix_source_info(0, fast_info);
        const bool counters_ok = id_fast != pullmix_invalid_id &&
                                 fast_info.id == id_fast &&
                                 fast_info.overflows == 0;
        pullmix_render(out, pullmix_ring_frames, 2);
        pullmix_source_info(0, fast_info);
        const bool underrun_ok = counters_ok && fast_info.underruns != 0;
        serial::write("mix counters=");
        serial::write_line(underrun_ok ? "ok" : "fail");
        ok = ok && underrun_ok;

        pullmix_reset();
        return ok;
    }

}  // namespace hype::audio
