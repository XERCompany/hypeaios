#pragma once

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio {

    enum class AudioBus : hcpp::u8 { Pci, Isa };

    enum class AudioChip : hcpp::u8 {
        Unknown,
        IntelHda,
        IntelAc97,
        EnsoniqEs1370,
        EnsoniqEs1371,
        VirtioSound,
        SoundBlaster16,
        AdLib,
        CreativeCa0106,
        CirrusCs46xx,
        GeodeCs5535Audio,
        CreativeCtxfi,
        EchoAudio,
        CreativeEmu10k1,
        Ice1712,
        Korg1212,
        DigigramLola,
        DigigramLx6464es
    };

    struct AudioCard {
        AudioBus bus;
        AudioChip chip;
        hcpp::u8 bus_number;
        hcpp::u8 slot;
        hcpp::u8 function;
        hcpp::u16 vendor_id;
        hcpp::u16 device_id;
        hcpp::u8 class_code;
        hcpp::u8 subclass;
        const char *name;
        hcpp::u32 bar0;
    };

    constexpr hcpp::size_t max_audio_cards = 8;

    void audio_cards_reset() noexcept;
    bool audio_card_register(const AudioCard &card) noexcept;
    const AudioCard *audio_cards() noexcept;
    hcpp::size_t audio_chip_count() noexcept;
    const char *audio_chip_name(AudioChip chip) noexcept;

}  // namespace hype::audio
