#include "kernel/audio/audio-serivce.h"

#include "kernel/audio/audiopci.h"
#include "kernel/audio/izau.h"
#include "kernel/serial.hpp"

namespace hype::audio {
    namespace {
        bool service_ready;
        bool probed;

        void write_hex32(hcpp::u32 value) noexcept {
            for (int shift = 28; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

        void bringup_hda_card(hcpp::size_t card_index) noexcept {
            hda::IzauController controller{};
            const bool bound = hda::izau_bind(card_index, controller);
            const bool reset = bound && hda::izau_reset(controller);
            serial::write("hda init=");
            serial::write_line(reset ? "ok" : "fail");
            if (!reset) {
                return;
            }
            hda::IzauCodec codecs[hda::max_codecs];
            const hcpp::u8 found = hda::izau_scan_codecs(controller, codecs, hda::max_codecs);
            serial::write("hda codecs=");
            serial::write_dec32(found);
            serial::write("\r\n");
            for (hcpp::u8 i = 0; i < found; ++i) {
                serial::write("hda codec vid=");
                write_hex32(codecs[i].vendor);
                serial::write("\r\n");
            }
        }
    }

    bool initialize() noexcept {
        audio_cards_reset();
        const AudioChipList found = audio_probe();
        for (hcpp::size_t i = 0; i < found.count; ++i) {
            audio_card_register(found.entries[i]);
        }
        for (hcpp::size_t i = 0; i < audio_chip_count(); ++i) {
            if (audio_cards()[i].chip == AudioChip::IntelHda) {
                bringup_hda_card(i);
            }
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
