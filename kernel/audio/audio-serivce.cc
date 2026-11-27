#include "kernel/audio/audio-serivce.h"

#include "kernel/audio/audioac97.h"
#include "kernel/audio/audioes1370.h"
#include "kernel/audio/audiopci.h"
#include "kernel/audio/hda.h"
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

        void write_hex16(hcpp::u16 value) noexcept {
            for (int shift = 12; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

        void verify_playback(hda::HdaController &controller,
                             const hda::HdaPlayback &playback) noexcept {
            hcpp::u32 lpib = 0;
            for (hcpp::u32 round = 0; round < 256 && lpib == 0; ++round) {
                for (hcpp::u32 spin = 0; spin < 0x80000; ++spin) {
                    asm volatile("pause");
                }
                lpib = hda::hda_playback_position(controller, playback);
            }
            serial::write("hda lpib=");
            write_hex32(lpib);
            serial::write("\r\n");
            serial::write("audio playback=");
            serial::write_line(lpib != 0 ? "ok" : "fail");
        }

        bool verify_amp(hda::HdaController &controller, hcpp::u8 codec,
                        hcpp::u8 node) noexcept {
            if (node == 0) {
                return false;
            }
            hcpp::u8 gain = 0;
            bool muted = true;
            if (!hda::hda_volume_get(controller, codec, node, gain, muted)) {
                return false;
            }
            constexpr hcpp::u8 probe_gain = 0x30;
            hcpp::u8 check = 0;
            bool check_muted = true;
            const bool ok =
                hda::hda_volume_set(controller, codec, node, probe_gain, true) &&
                hda::hda_volume_set(controller, codec, node, probe_gain, false) &&
                hda::hda_volume_get(controller, codec, node, check, check_muted) &&
                check == probe_gain && !check_muted;
            hda::hda_volume_set(controller, codec, node, gain, muted);
            return ok;
        }

        void verify_capture(hda::HdaController &controller, hcpp::u8 codec) noexcept {
            hda::HdaCapture capture{};
            const bool started = hda::hda_capture_start(controller, codec, capture);
            serial::write("hda cap start=");
            serial::write_line(started ? "run" : "fail");
            if (!started) {
                serial::write_line("hda cap=fail");
                return;
            }
            serial::write("hda cap path pin=");
            write_hex8(capture.pin_node);
            serial::write(" adc=");
            write_hex8(capture.adc_node);
            serial::write("\r\n");
            hcpp::u32 lpib = 0;
            for (hcpp::u32 round = 0; round < 256 && lpib == 0; ++round) {
                for (hcpp::u32 spin = 0; spin < 0x80000; ++spin) {
                    asm volatile("pause");
                }
                hda::hda_poll_interrupts(controller);
                lpib = hda::hda_capture_position(controller, capture);
            }
            for (hcpp::u32 round = 0; round < 8; ++round) {
                for (hcpp::u32 spin = 0; spin < 0x40000; ++spin) {
                    asm volatile("pause");
                }
                hda::hda_poll_interrupts(controller);
            }
            serial::write("hda cap lpib=");
            write_hex32(lpib);
            serial::write(" ioc=");
            serial::write_dec32(controller.irq_count);
            serial::write("\r\n");
            hcpp::i16 samples[8]{};
            const hcpp::u32 frames =
                hda::hda_capture_read(controller, capture, samples, 8);
            serial::write("hda cap frames=");
            serial::write_dec32(frames);
            for (hcpp::u32 i = 0; i < 4; ++i) {
                serial::write(" s");
                serial::write_dec32(i);
                serial::write("=");
                write_hex16(static_cast<hcpp::u16>(samples[i]));
            }
            serial::write("\r\n");
            serial::write("hda cap=");
            serial::write_line(lpib != 0 ? "ok" : "fail");
        }

        void bringup_hda_card(hcpp::size_t card_index) noexcept {
            hda::HdaController controller{};
            const bool bound = hda::hda_bind(card_index, controller);
            const bool reset = bound && hda::hda_reset(controller);
            serial::write("hda init=");
            serial::write_line(reset ? "ok" : "fail");
            if (!reset) {
                return;
            }
            hda::HdaCodec codecs[hda::max_codecs];
            const hcpp::u8 found = hda::hda_scan_codecs(controller, codecs, hda::max_codecs);
            serial::write("hda codecs=");
            serial::write_dec32(found);
            serial::write("\r\n");
            for (hcpp::u8 i = 0; i < found; ++i) {
                serial::write("hda codec vid=");
                write_hex32(codecs[i].vendor);
                serial::write("\r\n");
            }
            if (found == 0) {
                serial::write_line("audio stream=skip");
                return;
            }
            hda::HdaPlayback playback{};
            const bool started = hda::hda_playback_start(controller, codecs[0].address, playback);
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
            hda_selftest2(controller, codecs[0].address, playback);
        }
    }

    bool hda_selftest2(hda::HdaController &controller, hcpp::u8 codec,
                       const hda::HdaPlayback &playback) noexcept {
        bool ok = true;
        hda::HdaTopology topology{};
        const bool topo_ok = hda::hda_topology_walk(controller, codec, topology);
        serial::write("hda topo=");
        serial::write_dec32(topology.widgets);
        serial::write(" fg=");
        serial::write_dec32(topology.function_groups);
        serial::write(" dac=");
        serial::write_dec32(topology.dac_widgets);
        serial::write(" adc=");
        serial::write_dec32(topology.adc_widgets);
        serial::write(" pin=");
        serial::write_dec32(topology.pin_widgets);
        serial::write("\r\n");
        ok = ok && topo_ok;
        const bool amp_ok = verify_amp(controller, codec, playback.dac_node) ||
                            verify_amp(controller, codec, playback.pin_node);
        serial::write("hda amp=");
        serial::write_line(amp_ok ? "ok" : "fail");
        ok = ok && amp_ok;
        const bool intctl_ok = hda::hda_interrupts_enable(controller, 0x1);
        serial::write("hda intctl=");
        serial::write_line(intctl_ok ? "ok" : "fail");
        ok = ok && intctl_ok;
        verify_capture(controller, codec);
        serial::write("hda ioc=");
        serial::write_dec32(controller.irq_count);
        serial::write("\r\n");
        return ok;
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
        ac97::ac97_selftest();
        es1370::es1370_selftest();
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
