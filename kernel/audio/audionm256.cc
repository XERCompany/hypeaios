#include "kernel/audio/audionm256.h"

#include "kernel/audio/audiow2.h"
#include "kernel/audio/waveform.h"
#include "kernel/serial.hpp"

namespace hype::audio::nm256 {
    namespace {

        Nm256Controller active{};
        bool present;

        volatile hcpp::u8 &reg8(const Nm256Controller &c, hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u8 *>(c.cport + off);
        }

        volatile hcpp::u16 &reg16(const Nm256Controller &c, hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u16 *>(c.cport + off);
        }

        volatile hcpp::u32 &reg32(const Nm256Controller &c, hcpp::u32 off) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(c.cport + off);
        }

        bool ac97_ready(const Nm256Controller &c) noexcept {
            // ~10 polls of the mixer-ready bit, like ALSA's 10x100us loop.
            for (hcpp::u32 round = 0; round < 64; ++round) {
                if ((reg16(c, c.mixer_status_offset) & c.mixer_status_mask) == 0) {
                    return true;
                }
                w2::settle(0x4000);
            }
            return false;
        }

        bool ac97_write(Nm256Controller &c, hcpp::u8 reg, hcpp::u16 value) noexcept {
            // The NM256 mixer is write-only; writes are retried because the
            // codec sometimes drops them (mirrors snd_nm256_ac97_write).
            for (hcpp::u32 tries = 0; tries < 2; ++tries) {
                if (!ac97_ready(c)) {
                    return false;
                }
                reg16(c, mixer_offset + reg) = value;
                w2::settle(0x20000);
                if (ac97_ready(c)) {
                    return true;
                }
            }
            return false;
        }

        // Returns the signed buffer-end pointer when the BIOS signature is
        // valid, otherwise buffer_end - 0x1400.
        hcpp::u32 peek_signature(const Nm256Controller &c) noexcept {
            const hcpp::uintptr_t tail = c.vram + c.buffer_end - 0x400;
            const hcpp::u32 sig = *reinterpret_cast<volatile hcpp::u32 *>(tail);
            if ((sig & signature_mask) == signature) {
                const hcpp::u32 pointer =
                    *reinterpret_cast<volatile hcpp::u32 *>(tail + 4);
                if (pointer != 0xFFFFFFFF && pointer >= c.buffer_size &&
                    pointer <= c.buffer_end) {
                    return pointer;
                }
            }
            return c.buffer_end - 0x1400;
        }

    }  // namespace

    const pci::PciDevice *nm256_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_256av, device_256zx,
                                     device_256xl_plus};
        return w2::find_ids(vendor_neomagic, ids, 3);
    }

    bool nm256_bind(const pci::PciDevice &device,
                    Nm256Controller &controller) noexcept {
        controller = {};
        if (!w2::enable(device, false, true)) {
            return false;
        }
        controller.vram = w2::mem_base(device, 0);
        controller.cport = w2::mem_base(device, 1);
        if (controller.vram == 0 || controller.cport == 0) {
            return false;
        }
        controller.is_zx = device.device_id != device_256av;
        if (!controller.is_zx) {
            controller.mixer_status_offset = mixer_status_offset;
            controller.mixer_status_mask = mixer_ready_mask;
            controller.buffer_end = 2560u * 1024u;
            // AC'97 presence flag on the AV part.
            const hcpp::u16 pval = reg16(controller, mixer_presence);
            controller.ac97_present =
                (pval & presence_mask) == presence_value;
        } else {
            controller.mixer_status_offset = mixer_status_offset_zx;
            controller.mixer_status_mask = mixer_ready_mask_zx;
            controller.buffer_end =
                reg8(controller, 0xA0B) != 0 ? 6144u * 1024u : 4096u * 1024u;
            controller.ac97_present = true;
        }
        return true;
    }

    bool nm256_reset(Nm256Controller &controller) noexcept {
        if (controller.cport == 0) {
            return false;
        }
        // snd_nm256_init_chip: full hardware reset.
        reg8(controller, 0x0) = 0x11;
        reg16(controller, 0x214) = 0;
        w2::settle(w2::settle_spins >> 2);

        // Ring-buffer tail inside video RAM (signature-assisted).
        controller.buffer_size = play_buf_bytes + rec_buf_bytes +
                                 max_playback_coef_size +
                                 max_record_coef_size;
        controller.buffer_end = peek_signature(controller);
        controller.buffer_start = controller.buffer_end - controller.buffer_size;

        // Stop both engines and mute, then bring AC'97 outputs up.
        reg8(controller, playback_enable_reg) = 0;
        reg8(controller, record_enable_reg) = 0;
        reg16(controller, audio_mute_reg) =
            audio_mute_left | audio_mute_right;
        if (controller.ac97_present) {
            ac97_write(controller, ac97_master, 0x0000);
        }
        controller.reset_done = true;
        return true;
    }

    bool nm256_coefficients(Nm256Controller &controller, const void *blob,
                            hcpp::u32 bytes) noexcept {
        if (!controller.reset_done || blob == nullptr || bytes == 0) {
            return false;
        }
        const hcpp::u32 cap =
            max_playback_coef_size + max_record_coef_size;
        if (bytes > cap) {
            bytes = cap;
        }
        const hcpp::u32 coeff_buf = controller.buffer_start +
                                    play_buf_bytes + rec_buf_bytes;
        auto *dst =
            reinterpret_cast<volatile hcpp::u8 *>(controller.vram + coeff_buf);
        const auto *src = static_cast<const hcpp::u8 *>(blob);
        for (hcpp::u32 i = 0; i < bytes; ++i) {
            dst[i] = src[i];
        }
        controller.coef_loaded = true;
        return true;
    }

    bool nm256_play(Nm256Controller &controller) noexcept {
        if (!controller.reset_done) {
            return false;
        }
        const hcpp::u32 buf = controller.buffer_start;
        auto *samples = reinterpret_cast<hcpp::i16 *>(controller.vram + buf);
        const hcpp::u32 frames =
            play_buf_bytes / (pcm_channels * sizeof(hcpp::i16));
        waveform_render_sine(samples, frames, tone_freq_hz, pcm_rate,
                             pcm_channels);

        // 44.1 kHz -> rate index 6 in ALSA's samplerates[] table.
        const hcpp::u32 rate_index = 6;
        reg8(controller, playback_reg_offset + rate_reg_offset) =
            static_cast<hcpp::u8>((rate_index << 4) | rate_bits_16 |
                                  rate_stereo);
        const hcpp::u8 shift = 2;  // 16-bit stereo
        reg32(controller, pbuffer_start) = buf;
        reg32(controller, pbuffer_end) =
            buf + play_buf_bytes - (1u << shift);
        reg32(controller, pbuffer_currp) = buf;
        reg32(controller, pbuffer_wmark) = buf + play_buf_bytes / 2;
        reg8(controller, playback_enable_reg) =
            playback_enable_flag | playback_freerun;
        reg16(controller, audio_mute_reg) = 0;
        controller.running =
            (reg8(controller, playback_enable_reg) & playback_enable_flag) != 0;
        return controller.running;
    }

    void nm256_stop(Nm256Controller &controller) noexcept {
        if (controller.cport != 0) {
            reg16(controller, audio_mute_reg) =
                audio_mute_left | audio_mute_right;
            reg8(controller, playback_enable_reg) = 0;
            reg8(controller, record_enable_reg) = 0;
        }
        controller.running = false;
    }

    hcpp::u32 nm256_pos(const Nm256Controller &controller) noexcept {
        if (!controller.running || controller.cport == 0) {
            return 0;
        }
        const hcpp::u32 curr = reg32(controller, pbuffer_currp);
        return curr >= controller.buffer_start
                   ? curr - controller.buffer_start
                   : 0;
    }

    bool nm256_present() noexcept { return present; }

    bool nm256_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = nm256_find();
        if (device == nullptr) {
            serial::write_line("audiopci nm256=absent");
            return true;
        }
        if (!nm256_bind(*device, active) || !nm256_reset(active)) {
            serial::write_line("audiopci nm256=fail");
            return false;
        }
        serial::write("audiopci nm256 ac97=");
        serial::write_line(active.ac97_present ? "yes" : "no");
        // Optional coefficient blob (functions as the card firmware).
        hcpp::u32 fw_bytes = 0;
        if (w2::firmware_load("nm256", w2::firmware_scratch(),
                              w2::firmware_scratch_bytes, &fw_bytes)) {
            const bool coef =
                nm256_coefficients(active, w2::firmware_scratch(), fw_bytes);
            serial::write("audiopci nm256 coef=");
            serial::write_line(coef ? "loaded" : "fail");
        } else {
            serial::write_line("audiopci nm256 coef=absent");
        }
        const bool started = nm256_play(active);
        serial::write("audiopci nm256 dma=");
        serial::write_line(started ? "run" : "fail");
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 64 && pos == 0; ++round) {
            w2::settle(0x80000);
            pos = nm256_pos(active);
        }
        serial::write("audiopci nm256 pos=");
        w2::hex32(pos);
        serial::write("\r\n");
        nm256_stop(active);
        present = true;
        serial::write_line("audiopci nm256=ok");
        return true;
    }

}  // namespace hype::audio::nm256
