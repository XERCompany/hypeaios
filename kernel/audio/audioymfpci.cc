#include "kernel/audio/audioymfpci.h"

#include "kernel/audio/audiow2.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::ymfpci {
    namespace {

        YmfpciController active{};
        bool present;

        volatile hcpp::u32 &reg32(const YmfpciController &c,
                                  hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(c.mmio + offset);
        }

        volatile hcpp::u16 &reg16(const YmfpciController &c,
                                  hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u16 *>(c.mmio + offset);
        }

        bool wait_bit16(const YmfpciController &c, hcpp::u32 offset,
                        hcpp::u16 mask, hcpp::u16 want) noexcept {
            for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
                if ((reg16(c, offset) & mask) == want) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // Uploads one microcode word stream into DSPINSTRAM: each 32-bit
        // word is written as two 16-bit halves, high first (matches
        // snd_ymfpci_load_microcode's write order).
        bool upload_words(const YmfpciController &c, const void *image,
                          hcpp::u32 bytes) noexcept {
            const auto *words = static_cast<const hcpp::u32 *>(image);
            const hcpp::u32 count = bytes / 4;
            for (hcpp::u32 i = 0; i < count; ++i) {
                const hcpp::u32 w = words[i];
                reg16(c, dsp_instram) = static_cast<hcpp::u16>(w >> 16);
                reg16(c, dsp_instram) = static_cast<hcpp::u16>(w & 0xFFFF);
            }
            return true;
        }

        bool ensure_buffer(YmfpciController &c) noexcept {
            if (c.buffer_phys != 0) {
                return true;
            }
            c.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(ring_pages));
            if (c.buffer_phys == 0) {
                return false;
            }
            c.buffer_bytes = memory::page_size * ring_pages;
            c.buffer_frames =
                c.buffer_bytes / (pcm_channels * sizeof(hcpp::i16));
            return true;
        }

    }  // namespace

    const pci::PciDevice *ymfpci_find() noexcept {
        return w2::find_ids(vendor_yamaha, device_ids, device_id_count);
    }

    bool ymfpci_bind(const pci::PciDevice &device,
                     YmfpciController &controller) noexcept {
        controller = {};
        if (!w2::enable(device, false, true)) {
            return false;
        }
        controller.mmio = w2::mem_base(device, 0);
        return controller.mmio != 0;
    }

    bool ymfpci_reset(YmfpciController &controller,
                      const pci::PciDevice &device) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        // snd_ymfpci_hw_start: AC-link cold reset via PCIR_DSXG_CTRL
        // (clear bits 0-1, set them, clear again) then power the codec.
        hcpp::u32 ctrl = pci::config_read32(device.bus, device.slot,
                                            device.function,
                                            pcir_dsxg_ctrl);
        pci::config_write32(device.bus, device.slot, device.function,
                            pcir_dsxg_ctrl, ctrl & ~0x03);
        pci::config_write32(device.bus, device.slot, device.function,
                            pcir_dsxg_ctrl, ctrl | 0x03);
        w2::settle(w2::settle_spins >> 2);
        pci::config_write32(device.bus, device.slot, device.function,
                            pcir_dsxg_ctrl, ctrl & ~0x03);
        w2::settle(w2::settle_spins >> 2);

        reg32(controller, mode) = 0;
        reg32(controller, config) &= ~config_dsp_on;
        w2::settle(w2::settle_spins >> 3);
        controller.reset_done = true;
        return true;
    }

    bool ymfpci_codec_ready(YmfpciController &controller) noexcept {
        if (!controller.reset_done) {
            return false;
        }
        // Codec readiness = bit15 of STATUS_ADR clear while polling.
        controller.codec_ready =
            wait_bit16(controller, status_adr, status_ready_mask, 0);
        return controller.codec_ready;
    }

    bool ymfpci_ac97_read(const YmfpciController &controller, hcpp::u16 reg,
                          hcpp::u16 &value) noexcept {
        // Read-back through the secondary codec status ports:
        // write register address to AC97_CMD_ADR, then read data from
        // STATUS_DATA once STATUS_ADR reports the register index.
        reg16(controller, ac97_cmd_adr) = reg;
        if (!wait_bit16(controller, status_adr, status_ready_mask, 0)) {
            return false;
        }
        for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
            if ((reg16(controller, status_adr) & 0x007F) == (reg >> 1)) {
                value = reg16(controller, status_data);
                return true;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool ymfpci_ac97_write(YmfpciController &controller, hcpp::u16 reg,
                           hcpp::u16 value) noexcept {
        if (!wait_bit16(controller, status_adr, status_ready_mask, 0)) {
            return false;
        }
        reg16(controller, ac97_cmd_data) = value;
        reg16(controller, ac97_cmd_adr) = reg | 0x0100;  // write bit
        return true;
    }

    bool ymfpci_load_firmware(YmfpciController &controller, const void *ctrl,
                              hcpp::u32 ctrl_bytes, const void *dsp,
                              hcpp::u32 dsp_bytes) noexcept {
        if (!controller.reset_done || ctrl == nullptr || dsp == nullptr ||
            ctrl_bytes < 4 || dsp_bytes < 4) {
            return false;
        }
        // snd_ymfpci_load_microcode: halt the DSP, upload control code
        // then DSP code into DSPINSTRAM, re-enable and run.
        reg32(controller, mode) = 0;
        reg32(controller, config) &= ~config_dsp_on;
        w2::settle(w2::settle_spins >> 3);
        upload_words(controller, ctrl, ctrl_bytes);
        upload_words(controller, dsp, dsp_bytes);
        reg32(controller, config) |= config_dsp_on;
        reg32(controller, mode) = mode_dsp_run;
        controller.fw_loaded = true;
        return true;
    }

    bool ymfpci_play(YmfpciController &controller) noexcept {
        if (!controller.reset_done || !controller.fw_loaded ||
            !ensure_buffer(controller)) {
            return false;
        }
        auto *samples = reinterpret_cast<hcpp::i16 *>(
            static_cast<hcpp::uintptr_t>(controller.buffer_phys));
        waveform_render_sine(samples, controller.buffer_frames,
                             tone_freq_hz, pcm_rate, pcm_channels);

        // Program the bank-0 playback slot (PLAY_CTRL_BASE points at
        // bank structs {format, base, loop_end, start, delta, ...} in
        // on-chip RAM).  format: 16-bit little-endian, stereo flag in
        // bit16; delta = (rate<<16)/375 <<5 for 48k == standard.
        volatile hcpp::u32 *bank = reinterpret_cast<volatile hcpp::u32 *>(
            controller.mmio + play_ctrl_base);
        const hcpp::u32 format = 0x00010000;          // stereo s16
        const hcpp::u32 delta =
            ((pcm_rate << 16) / 375) << 5;
        bank[0] = format;
        bank[1] = controller.buffer_phys;
        bank[2] = controller.buffer_phys + controller.buffer_bytes;
        bank[3] = controller.buffer_phys;             // loop start
        bank[4] = 0;
        bank[5] = delta;
        bank[6] = 0x3FFF3FFF;                         // gains L/R
        controller.dsp_running = true;
        return true;
    }

    void ymfpci_stop(YmfpciController &controller) noexcept {
        if (controller.mmio != 0 && controller.dsp_running) {
            reg32(controller, mode) = 0;
        }
        controller.dsp_running = false;
    }

    hcpp::u32 ymfpci_pos(const YmfpciController &controller) noexcept {
        if (!controller.dsp_running || controller.mmio == 0) {
            return 0;
        }
        // Bank-0 current position field.
        volatile const hcpp::u32 *bank =
            reinterpret_cast<volatile const hcpp::u32 *>(
                controller.mmio + play_ctrl_base);
        return bank[3];
    }

    bool ymfpci_present() noexcept { return present; }

    bool ymfpci_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = ymfpci_find();
        if (device == nullptr) {
            serial::write_line("audiopci ymfpci=absent");
            return true;
        }
        if (!ymfpci_bind(*device, active) ||
            !ymfpci_reset(active, *device)) {
            serial::write_line("audiopci ymfpci=fail");
            return false;
        }
        if (!ymfpci_codec_ready(active)) {
            serial::write_line("audiopci ymfpci codec=timeout");
            serial::write_line("audiopci ymfpci=fail");
            return false;
        }
        hcpp::u16 id0 = 0, id1 = 0;
        const bool id_ok = ymfpci_ac97_read(active, ac97_codec_id0, id0) &&
                           ymfpci_ac97_read(active, ac97_codec_id1, id1);
        serial::write("audiopci ymfpci codec=0x");
        w2::hex16(id_ok ? id0 : 0);
        w2::hex16(id_ok ? id1 : 0);
        serial::write("\r\n");
        // Unmute master + native DAC volume.
        ymfpci_ac97_write(active, ac97_master, 0x0000);
        ymfpci_ac97_write(active, ac97_nativedac_out, 0x3FFF3FFF & 0xFFFF);

        void *ctrl = w2::firmware_scratch();
        hcpp::u32 ctrl_bytes = 0;
        if (!w2::firmware_load("ymfpci", ctrl,
                               w2::firmware_scratch_bytes / 2,
                               &ctrl_bytes)) {
            serial::write_line("audiopci ymfpci fw=missing");
            serial::write_line("audiopci ymfpci=parked");
            return true;
        }
        // Second half of the scratch for the DSP image.
        auto *dsp = static_cast<hcpp::u8 *>(ctrl) +
                    w2::firmware_scratch_bytes / 2;
        hcpp::u32 dsp_bytes = 0;
        if (!w2::firmware_load("ymfpci_dsp", dsp,
                               w2::firmware_scratch_bytes / 2,
                               &dsp_bytes)) {
            serial::write_line("audiopci ymfpci fw=missing");
            serial::write_line("audiopci ymfpci=parked");
            return true;
        }
        if (!ymfpci_load_firmware(active, ctrl, ctrl_bytes, dsp,
                                  dsp_bytes)) {
            serial::write_line("audiopci ymfpci fw=fail");
            serial::write_line("audiopci ymfpci=parked");
            return true;
        }
        serial::write_line("audiopci ymfpci fw=ok");
        if (!ymfpci_play(active)) {
            serial::write_line("audiopci ymfpci dma=fail");
            return false;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 128 && pos == 0; ++round) {
            w2::settle(0x80000);
            pos = ymfpci_pos(active) - active.buffer_phys;
        }
        serial::write("audiopci ymfpci pos=");
        w2::hex32(pos);
        serial::write("\r\n");
        ymfpci_stop(active);
        present = true;
        serial::write_line("audiopci ymfpci=ok");
        return true;
    }

}  // namespace hype::audio::ymfpci
