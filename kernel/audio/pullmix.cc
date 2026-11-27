#include "kernel/audio/pullmix.h"

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
        };

        PullSource sources[pullmix_max_sources];
        hcpp::u32 source_count;
        hcpp::u32 next_id = 1;
        hcpp::u32 clip_events;
        hcpp::i32 accum[pullmix_ring_frames * pullmix_max_channels];

        hcpp::u32 ring_tail_space(hcpp::u32 offset) noexcept {
            return pullmix_ring_frames - offset;
        }

        void source_top_up(PullSource &source, hcpp::u32 frames) noexcept {
            hcpp::u32 guard = 0;
            while (source.tail - source.head < frames && guard++ < 8) {
                const hcpp::u32 avail = source.tail - source.head;
                const hcpp::u32 free_frames = pullmix_ring_frames - avail;
                if (free_frames == 0) {
                    source.head = source.tail - pullmix_ring_frames + frames;
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

    }  // namespace

    void pullmix_reset() noexcept {
        for (hcpp::u32 i = 0; i < pullmix_max_sources; ++i) {
            sources[i] = {};
        }
        source_count = 0;
        next_id = 1;
        clip_events = 0;
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
        info.underruns = source.underruns;
        return true;
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
        for (hcpp::u32 s = 0; s < pullmix_max_sources; ++s) {
            PullSource &source = sources[s];
            if (!source.active) {
                continue;
            }
            source_top_up(source, frames);
            const hcpp::u32 avail = source.tail - source.head;
            const hcpp::u32 take = avail < frames ? avail : frames;
            if (take < frames) {
                ++source.underruns;
            }
            for (hcpp::u32 f = 0; f < take; ++f) {
                const hcpp::u32 offset = (source.head + f) % pullmix_ring_frames;
                const hcpp::i16 *frame = source.ring + offset * source.channels;
                for (hcpp::u32 c = 0; c < channels; ++c) {
                    const hcpp::u32 tap = c < source.channels ? c : source.channels - 1;
                    accum[f * channels + c] += frame[tap];
                }
            }
            source.head += take;
        }
        for (hcpp::u32 i = 0; i < samples; ++i) {
            if (accum[i] > 32767 || accum[i] < -32768) {
                ++clip_events;
            }
            out[i] = pullmix_clip16(accum[i]);
        }
        return frames;
    }

    hcpp::u32 pullmix_clip_events() noexcept {
        return clip_events;
    }

}  // namespace hype::audio
