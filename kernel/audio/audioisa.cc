#include "kernel/audio/audioisa.h"

#include "kernel/audio/opl2.h"
#include "kernel/audio/sb16.h"
#include "kernel/audio/waveform.h"
#include "kernel/serial.hpp"

namespace hype::audio {

    namespace {

        constexpr hcpp::u32 sb16_rate = 11025;
        constexpr hcpp::u32 sb16_tone_hz = 440;

        constexpr IsaAudioDevice audioisa_known_devices[] = {
            {"Sound Blaster 16", AudioChip::SoundBlaster16, 0x220, 5, 1, 5},
            {"AdLib OPL2", AudioChip::AdLib, 0x388, 0, 0, 0},
            {"Windows Sound System", AudioChip::Unknown, 0x530, 9, 0, 0},
        };

        alignas(4096) hcpp::u8 sb16_pcm[4096];

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        hcpp::u32 render_pcm8(hcpp::u8 *dst, hcpp::u32 frames, hcpp::u32 freq_hz,
                              hcpp::u32 rate) noexcept {
            WaveformTone tone = waveform_tone(freq_hz, rate);
            for (hcpp::u32 i = 0; i < frames; ++i) {
                dst[i] = static_cast<hcpp::u8>(128 + (waveform_next(tone) >> 8));
            }
            return frames;
        }

        bool sb16_selftest(const IsaAudioDevice &device) noexcept {
            if (!sb16::dsp_reset(device.base_port)) {
                serial::write_line("sb16=absent");
                return true;
            }
            serial::write_line("sb16 dsp=ok");
            hcpp::u8 major = 0;
            hcpp::u8 minor = 0;
            if (!sb16::dsp_version(device.base_port, major, minor)) {
                serial::write_line("sb16 ver=fail");
                return false;
            }
            serial::write("sb16 ver=ok ");
            serial::write_dec32(major);
            serial::write_char('.');
            serial::write_dec32(minor);
            serial::write("\r\n");
            sb16::dsp_speaker_on(device.base_port);
            const hcpp::u32 frames = render_pcm8(sb16_pcm, sizeof(sb16_pcm),
                                                 sb16_tone_hz, sb16_rate);
            const bool played = sb16::play_pcm8(
                device.base_port, device.dma8, static_cast<hcpp::u16>(sb16_rate),
                sb16_pcm, static_cast<hcpp::u16>(frames));
            serial::write("sb16 dma=");
            serial::write_line(played ? "ok" : "fail");
            audio_card_register(audioisa_card(device));
            return played;
        }

        bool opl2_selftest(const IsaAudioDevice &device) noexcept {
            hcpp::u16 base = device.base_port;
            if (!opl::opl_detect(base)) {
                base = opl::sb_base;
                if (!opl::opl_detect(base)) {
                    serial::write_line("opl2=absent");
                    return true;
                }
            }
            serial::write_line("opl2=ok");
            opl::opl_reset(base);
            constexpr hcpp::u32 melody[] = {523, 659, 784};
            for (hcpp::size_t i = 0; i < sizeof(melody) / sizeof(melody[0]); ++i) {
                opl::opl_note(base, 0, melody[i]);
                settle(0x400000);
            }
            opl::opl_note_off(base, 0);
            serial::write_line("opl2 note=ok");
            audio_card_register(audioisa_card(device));
            return true;
        }

    }  // namespace

    const IsaAudioDevice *audioisa_devices() noexcept { return audioisa_known_devices; }

    hcpp::size_t audioisa_device_count() noexcept {
        return sizeof(audioisa_known_devices) / sizeof(audioisa_known_devices[0]);
    }

    AudioCard audioisa_card(const IsaAudioDevice &device) noexcept {
        return {AudioBus::Isa, device.chip, 0, 0, 0, 0, 0, 0, 0, device.name, 0};
    }

    bool isa_selftest() noexcept {
        bool ok = true;
        const hcpp::size_t count = audioisa_device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const IsaAudioDevice &device = audioisa_known_devices[i];
            if (device.chip == AudioChip::SoundBlaster16) {
                ok = sb16_selftest(device) && ok;
            } else if (device.chip == AudioChip::AdLib) {
                ok = opl2_selftest(device) && ok;
            }
        }
        return ok;
    }

}  // namespace hype::audio
