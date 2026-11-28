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
        // Wave-1 PCI audio families (ALSA pci_device_id tables).
        {0x1102, 0x0007, AudioChip::CreativeCa0106, "Creative CA0106"},
        {0x1013, 0x6001, AudioChip::CirrusCs46xx, "Cirrus CS4610/11"},
        {0x1013, 0x6003, AudioChip::CirrusCs46xx, "Cirrus CS4614/22/24"},
        {0x100B, 0x002E, AudioChip::GeodeCs5535Audio, "NS CS5535 Audio"},
        {0x1022, 0x2093, AudioChip::GeodeCs5535Audio, "AMD CS5536 Audio"},
        {0x1102, 0x0005, AudioChip::CreativeCtxfi, "Creative X-Fi EMU20k1"},
        {0x1102, 0x000B, AudioChip::CreativeCtxfi, "Creative X-Fi EMU20k2"},
        {0x1057, 0x1801, AudioChip::EchoAudio, "Echo DSP56301 card"},
        {0x1057, 0x3410, AudioChip::EchoAudio, "Echo DSP56361 card"},
        {0x1102, 0x0002, AudioChip::CreativeEmu10k1, "Creative SB Live! EMU10k1"},
        {0x1102, 0x0004, AudioChip::CreativeEmu10k1, "Creative Audigy EMU10k2"},
        {0x1102, 0x0008, AudioChip::CreativeEmu10k1, "Creative Audigy2"},
        {0x1412, 0x1712, AudioChip::Ice1712, "ICEnsemble ICE1712"},
        {0x10B5, 0x906D, AudioChip::Korg1212, "Korg 1212 I/O"},
        {0x1369, 0x0001, AudioChip::DigigramLola, "Digigram LoLa"},
        // LX6464ES rides a PLX 9056 host bridge (10B5:9056) and is
        // only a Digigram board when the subsystem vendor is 0x1369;
        // classify_device() checks the subsystem pair before trusting
        // this shared PLX id.
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
