#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audiopcicard.h"

namespace hype::audio {

    struct IsaAudioDevice {
        const char *name;
        AudioChip chip;
        hcpp::u16 base_port;
        hcpp::u8 irq;
        hcpp::u8 dma8;
        hcpp::u8 dma16;
    };

    const IsaAudioDevice *isau_devices() noexcept;
    hcpp::size_t isau_device_count() noexcept;
    AudioCard isau_card(const IsaAudioDevice &device) noexcept;

}  // namespace hype::audio
