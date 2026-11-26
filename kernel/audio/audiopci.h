#pragma once

#include "kernel/audio/audiopcicard.h"

namespace hype::audio {

    struct AudioChipList {
        AudioCard entries[max_audio_cards];
        hcpp::size_t count;
    };

    bool is_audio_device(const pci::PciDevice &device) noexcept;
    AudioChip classify_device(const pci::PciDevice &device) noexcept;
    const char *audio_device_name(const pci::PciDevice &device) noexcept;
    AudioChipList audio_probe() noexcept;

}  // namespace hype::audio
