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

    const IsaAudioDevice *audioisa_devices() noexcept;
    hcpp::size_t audioisa_device_count() noexcept;
    AudioCard audioisa_card(const IsaAudioDevice &device) noexcept;
    bool isa_selftest() noexcept;

}  // namespace hype::audio
