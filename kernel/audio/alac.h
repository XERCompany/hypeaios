#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    constexpr hcpp::u32 alac_magic = 0x616C6163;
    constexpr hcpp::u32 alac_frame_magic = 0x66726D61;

    struct AlacConfig {
        hcpp::u8 bit_depth;
        hcpp::u8 channels;
        hcpp::u32 sample_rate;
        hcpp::u32 max_frame_bytes;
        hcpp::u8 history_multiplier;
        hcpp::u8 rice_modifier;
        hcpp::u8 initial_history;
        hcpp::u32 frame_length;
        hcpp::u32 avg_bit_rate;
    };

    constexpr hcpp::u32 alac_cookie_size = 24;

    inline bool alac_config_valid(const AlacConfig &config) noexcept {
        if (config.channels == 0 || config.channels > 8) {
            return false;
        }
        if (config.bit_depth != 16 && config.bit_depth != 24) {
            return false;
        }
        return config.sample_rate >= 8000 && config.sample_rate <= 192000;
    }

    inline AlacConfig alac_default_config() noexcept {
        return {16, 2, 44100, 65536, 40, 4, 10, 4096, 0};
    }

    bool alac_parse_cookie(const void *data, hcpp::u32 size, AlacConfig &config) noexcept;

}  // namespace hype::audio
