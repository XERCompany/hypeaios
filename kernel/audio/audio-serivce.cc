#include "kernel/audio/audio-serivce.h"

#include "kernel/audio/audiopci.h"
#include "kernel/serial.hpp"

namespace hype::audio {
    namespace {
        bool service_ready;
        bool probed;
    }

    bool initialize() noexcept {
        audio_cards_reset();
        const AudioChipList found = audio_probe();
        for (hcpp::size_t i = 0; i < found.count; ++i) {
            audio_card_register(found.entries[i]);
        }
        probed = true;
        service_ready = true;
        serial::write("audio service=ready probed=");
        serial::write_dec32(static_cast<hcpp::u32>(found.count));
        serial::write("\r\n");
        return true;
    }

    bool ready() noexcept { return service_ready; }

    const AudioCard *chips() noexcept {
        if (!probed) {
            initialize();
        }
        return audio_cards();
    }

    hcpp::size_t chip_count() noexcept {
        if (!probed) {
            initialize();
        }
        return audio_chip_count();
    }

    const AudioCard *chip(hcpp::size_t index) noexcept {
        const AudioCard *cards = chips();
        return index < audio_chip_count() ? &cards[index] : nullptr;
    }

    const AudioCard *primary_chip() noexcept {
        return chip_count() != 0 ? &chips()[0] : nullptr;
    }

}  // namespace hype::audio
