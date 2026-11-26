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
            default:
                return "unknown audio";
        }
    }

}  // namespace hype::audio
