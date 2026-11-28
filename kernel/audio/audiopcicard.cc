#include "kernel/audio/audiopcicard.h"

namespace hype::audio {
    namespace {
        AudioCard registered[max_audio_cards];
        hcpp::size_t registered_count;
    }

    void audio_cards_reset() noexcept { registered_count = 0; }

    bool audio_card_register(const AudioCard &card) noexcept {
        if (registered_count >= max_audio_cards) {
            return false;
        }
        registered[registered_count++] = card;
        return true;
    }

    const AudioCard *audio_cards() noexcept { return registered; }

    hcpp::size_t audio_chip_count() noexcept { return registered_count; }

    const char *audio_chip_name(AudioChip chip) noexcept {
        switch (chip) {
            case AudioChip::IntelHda:
                return "Intel HDA";
            case AudioChip::IntelAc97:
                return "Intel 82801AA AC97";
            case AudioChip::EnsoniqEs1370:
                return "Ensoniq ES1370";
            case AudioChip::EnsoniqEs1371:
                return "Ensoniq ES1371";
            case AudioChip::VirtioSound:
                return "VirtIO Sound";
            case AudioChip::SoundBlaster16:
                return "Sound Blaster 16";
            case AudioChip::AdLib:
                return "AdLib OPL2";
            case AudioChip::CreativeCa0106:
                return "Creative CA0106";
            case AudioChip::CirrusCs46xx:
                return "Cirrus CS46xx";
            case AudioChip::GeodeCs5535Audio:
                return "Geode CS5535/CS5536 Audio";
            case AudioChip::CreativeCtxfi:
                return "Creative X-Fi";
            case AudioChip::EchoAudio:
                return "Echo Digital Audio";
            case AudioChip::CreativeEmu10k1:
                return "Creative EMU10k1/EMU10k2";
            case AudioChip::Ice1712:
                return "ICEnsemble ICE1712";
            case AudioChip::Korg1212:
                return "Korg 1212 I/O";
            case AudioChip::DigigramLola:
                return "Digigram LoLa";
            case AudioChip::DigigramLx6464es:
                return "Digigram LX6464ES";
            default:
                return "unknown audio";
        }
    }

}  // namespace hype::audio
