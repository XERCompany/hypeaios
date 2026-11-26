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

    inline constexpr IsaAudioDevice isau_known_devices[] = {
        {"Sound Blaster 16", AudioChip::SoundBlaster16, 0x220, 5, 1, 5},
        {"AdLib OPL2", AudioChip::AdLib, 0x388, 0, 0, 0},
        {"Windows Sound System", AudioChip::Unknown, 0x530, 9, 0, 0},
    };

    inline const IsaAudioDevice *isau_devices() noexcept { return isau_known_devices; }

    inline hcpp::size_t isau_device_count() noexcept {
        return sizeof(isau_known_devices) / sizeof(isau_known_devices[0]);
    }

    inline AudioCard isau_card(const IsaAudioDevice &device) noexcept {
        return {AudioBus::Isa, device.chip, 0, 0, 0, 0, 0, 0, 0, device.name, 0};
    }

}  // namespace hype::audio
