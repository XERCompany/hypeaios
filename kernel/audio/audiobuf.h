#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    constexpr hcpp::u32 audio_ring_capacity = 8192;

    struct AudioRing {
        hcpp::u8 data[audio_ring_capacity];
        hcpp::u32 read_pos;
        hcpp::u32 write_pos;
        hcpp::u32 capacity;
    };

    inline void audio_ring_init(AudioRing &ring) noexcept {
        ring.read_pos = ring.write_pos = 0;
        ring.capacity = audio_ring_capacity;
    }

    inline hcpp::u32 audio_ring_used(const AudioRing &ring) noexcept {
        const hcpp::u32 w = ring.write_pos;
        const hcpp::u32 r = ring.read_pos;
        return w >= r ? w - r : ring.capacity - r + w;
    }

    inline hcpp::u32 audio_ring_space(const AudioRing &ring) noexcept {
        return ring.capacity - audio_ring_used(ring) - 1;
    }

    hcpp::u32 audio_ring_push(AudioRing &ring, const void *data, hcpp::u32 bytes) noexcept;
    hcpp::u32 audio_ring_pop(AudioRing &ring, void *data, hcpp::u32 bytes) noexcept;

}  // namespace hype::audio
