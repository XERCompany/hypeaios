#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audiopcicard.h"

namespace hype::audio {

    struct AudioIdMap {
        hcpp::u16 vendor_id;
        hcpp::u16 device_id;
        AudioChip chip;
        const char *name;
    };

    inline constexpr AudioIdMap audio_id_maps[] = {
        {0x8086, 0x2668, AudioChip::IntelHda, "Intel HDA"},
        {0x8086, 0x293E, AudioChip::IntelHda, "Intel HDA"},
        {0x8086, 0x2415, AudioChip::IntelAc97, "Intel 82801AA AC97"},
        {0x1274, 0x5000, AudioChip::EnsoniqEs1370, "Ensoniq ES1370"},
        {0x1274, 0x1371, AudioChip::EnsoniqEs1371, "Ensoniq ES1371"},
    };

    inline constexpr hcpp::u16 audio_vendor_virtio = 0x1AF4;
    inline constexpr hcpp::u16 audio_virtio_sound_device = 0x1059;
    inline constexpr hcpp::u8 audio_class_multimedia = 0x04;
    inline constexpr hcpp::u8 audio_subclass_hda = 0x03;
    inline constexpr hcpp::u8 audio_subclass_ac97 = 0x01;

    inline const AudioIdMap *audio_id_lookup(hcpp::u16 vendor_id, hcpp::u16 device_id) noexcept {
        for (const AudioIdMap &entry : audio_id_maps) {
            if (entry.vendor_id == vendor_id && entry.device_id == device_id) {
                return &entry;
            }
        }
        return nullptr;
    }

}  // namespace hype::audio
