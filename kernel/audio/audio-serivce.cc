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

        void write_hex8(hcpp::u8 value) noexcept {
            for (int shift = 4; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

        void verify_playback(hda::IzauController &controller,
                             const hda::IzauPlayback &playback) noexcept {
            hcpp::u32 lpib = 0;
            for (hcpp::u32 round = 0; round < 256 && lpib == 0; ++round) {
                for (hcpp::u32 spin = 0; spin < 0x80000; ++spin) {
                    asm volatile("pause");
                }
                lpib = hda::izau_playback_position(controller, playback);
            }
            serial::write("hda lpib=");
            write_hex32(lpib);
            serial::write("\r\n");
            serial::write("audio playback=");
            serial::write_line(lpib != 0 ? "ok" : "fail");
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
            hda::IzauPlayback playback{};
            const bool started = hda::izau_playback_start(controller, codecs[0].address, playback);
            serial::write("audio stream=");
            serial::write_line(started ? "run" : "fail");
            if (started) {
                serial::write("hda path pin=");
                write_hex8(playback.pin_node);
                serial::write(" dac=");
                write_hex8(playback.dac_node);
                serial::write("\r\n");
                verify_playback(controller, playback);
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
