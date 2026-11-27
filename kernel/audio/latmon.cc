#include "kernel/audio/latmon.h"

#include "kernel/interrupts/idt.h"

namespace hype::audio {
    namespace {

        struct LatmonState {
            hcpp::u64 fill_tick;
            hcpp::u32 start_byte;
            hcpp::u32 end_byte;
            hcpp::u32 ring_bytes;
            hcpp::u32 last_us;
            hcpp::u32 max_us;
            hcpp::u64 play_ticks;
            bool pending;
        };

        LatmonState state;

    }  // namespace

    void latmon_reset() noexcept {
        state = {};
    }

    void latmon_fill(hcpp::u32 start_byte, hcpp::u32 end_byte, hcpp::u32 queued_bytes,
                     hcpp::u32 frame_bytes, hcpp::u32 rate, hcpp::u32 ring_bytes) noexcept {
        state.fill_tick = interrupts::pit_ticks();
        state.start_byte = start_byte;
        state.end_byte = end_byte;
        state.ring_bytes = ring_bytes;
        state.pending = true;
        hcpp::u32 queued_us = 0;
        if (frame_bytes != 0 && rate != 0) {
            const hcpp::u64 frames = queued_bytes / frame_bytes;
            queued_us = static_cast<hcpp::u32>((frames * 1000000ull) / rate);
        }
        state.last_us = queued_us;
        if (queued_us > state.max_us) {
            state.max_us = queued_us;
        }
    }

    void latmon_poll(hcpp::u32 lpib_byte) noexcept {
        if (!state.pending || state.ring_bytes == 0) {
            return;
        }
        const hcpp::u32 played =
            (lpib_byte + state.ring_bytes - state.start_byte) % state.ring_bytes;
        const hcpp::u32 span = (state.end_byte + state.ring_bytes - state.start_byte) %
                               state.ring_bytes;
        if (played <= span) {
            state.play_ticks = interrupts::pit_ticks() - state.fill_tick;
            state.pending = false;
        }
    }

    hcpp::u32 latmon_last_us() noexcept {
        return state.last_us;
    }

    hcpp::u32 latmon_max_us() noexcept {
        return state.max_us;
    }

    hcpp::u64 latmon_play_ticks() noexcept {
        return state.play_ticks;
    }

    hcpp::u64 latmon_fill_tick() noexcept {
        return state.fill_tick;
    }

}  // namespace hype::audio
