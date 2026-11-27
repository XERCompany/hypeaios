#pragma once

#include "hcpp/types.hpp"

namespace hype::audio {

    struct HasioFormat {
        hcpp::u32 rate;
        hcpp::u8 channels;
        hcpp::u8 bits;
    };

    struct HasioLatency {
        hcpp::u32 period_frames;
        hcpp::u32 est_us;
        hcpp::u32 ring_us;
    };

    using HasioCallback = void (*)(hcpp::i16 *buf, hcpp::u32 frames) noexcept;

    constexpr hcpp::u32 hasio_invalid_handle = 0xFFFFFFFFu;
    constexpr hcpp::u32 hasio_min_period_frames = 32;
    constexpr hcpp::u32 hasio_max_period_frames = 512;
    constexpr hcpp::u16 hasio_max_ring_entries = 8;
    constexpr hcpp::u32 hasio_latency_budget_us = 8000;

    bool hasio_ready() noexcept;
    hcpp::u32 hasio_open_stream(HasioFormat fmt, hcpp::u32 period_frames) noexcept;
    bool hasio_set_callback(hcpp::u32 handle, HasioCallback callback) noexcept;
    bool hasio_start(hcpp::u32 handle) noexcept;
    bool hasio_stop(hcpp::u32 handle) noexcept;
    HasioLatency hasio_latency(hcpp::u32 handle) noexcept;
    hcpp::u32 hasio_pump(hcpp::u32 handle) noexcept;
    hcpp::u32 hasio_pump_active() noexcept;
    hcpp::u32 hasio_ring_entries(hcpp::u32 handle) noexcept;

    bool hasio_service_start() noexcept;
    bool hasio_service_stop() noexcept;

}  // namespace hype::audio
