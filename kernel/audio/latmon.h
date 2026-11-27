#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    void latmon_reset() noexcept;
    void latmon_fill(hcpp::u32 start_byte, hcpp::u32 end_byte, hcpp::u32 queued_bytes,
                     hcpp::u32 frame_bytes, hcpp::u32 rate, hcpp::u32 ring_bytes) noexcept;
    void latmon_poll(hcpp::u32 lpib_byte) noexcept;

    hcpp::u32 latmon_last_us() noexcept;
    hcpp::u32 latmon_max_us() noexcept;
    hcpp::u64 latmon_play_ticks() noexcept;
    hcpp::u64 latmon_fill_tick() noexcept;

}  // namespace hype::audio
