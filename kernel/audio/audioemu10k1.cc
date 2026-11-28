#include "kernel/audio/audioemu10k1.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::emu10k1 {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;

        Emu10k1Controller active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void write8(const Emu10k1Controller &controller, hcpp::u8 reg,
                    hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        void write16(const Emu10k1Controller &controller, hcpp::u8 reg,
                     hcpp::u16 value) noexcept {
            hype_port_out16(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        void write32(const Emu10k1Controller &controller, hcpp::u8 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u8 read8(const Emu10k1Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in8(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        hcpp::u32 read32(const Emu10k1Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        // Indirect register access: regptr = (reg << 16) | channel,
        // written to PTR, then DATA carries the value (ALSA io.c).
        void ptr_write(const Emu10k1Controller &controller, hcpp::u8 reg,
                       hcpp::u8 channel, hcpp::u32 value) noexcept {
            write32(controller, reg_ptr,
                    (static_cast<hcpp::u32>(reg) << 16) | channel);
            write32(controller, reg_data, value);
        }

        hcpp::u32 ptr_read(const Emu10k1Controller &controller, hcpp::u8 reg,
                           hcpp::u8 channel) noexcept {
            write32(controller, reg_ptr,
                    (static_cast<hcpp::u32>(reg) << 16) | channel);
            return read32(controller, reg_data);
        }

        bool wait_codec_ready(const Emu10k1Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((read8(controller, reg_ac97address) & ac97_ready) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool is_emu10k1(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != vendor_creative) {
                return false;
            }
            return device.device_id == device_live10k1 ||
                   device.device_id == device_audigy ||
                   device.device_id == device_audigy2;
        }

        bool claim_bar(const pci::PciDevice &device,
                       Emu10k1Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space |
                                    pci_cmd_bus_master);
            const hcpp::u32 bar0 = pci::bar(device, 0);
            if ((bar0 & 0x1) == 0) {
                return false;  // emu10k1 register window is I/O space
            }
            const hcpp::u32 iobase = bar0 & ~hcpp::u32(0x3);
            if (iobase == 0 || iobase > 0xFF00) {
                return false;
            }
            controller.iobase = static_cast<hcpp::u16>(iobase);
            return true;
        }

        bool ensure_buffer(Emu10k1Controller &controller) noexcept {
            if (controller.buffer_phys != 0) {
                return true;
            }
            controller.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(ring_pages));
            if (controller.buffer_phys == 0) {
                return false;
            }
            controller.buffer_bytes = memory::page_size * ring_pages;
            return true;
        }

        void write_hex16(hcpp::u16 value) noexcept {
            for (int shift = 12; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *emu10k1_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_emu10k1(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    bool emu10k1_bind(const pci::PciDevice &device,
                      Emu10k1Controller &controller) noexcept {
        if (!is_emu10k1(device)) {
            return false;
        }
        controller = {};
        controller.device_id = device.device_id;
        controller.audigy =
            device.device_id == device_audigy || device.device_id == device_audigy2;
        return claim_bar(device, controller);
    }

    bool emu10k1_codec_write(Emu10k1Controller &controller, hcpp::u8 reg,
                             hcpp::u16 value) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // ALSA io.c snd_emu10k1_ac97_write: index then 16-bit data.
        write8(controller, reg_ac97address, reg & 0x7f);
        write16(controller, reg_ac97data, value);
        return true;
    }

    bool emu10k1_reset(Emu10k1Controller &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // Disable audio and lock the sound/tank caches while the
        // register file is reconfigured (snd_emu10k1_init).
        write32(controller, reg_hcfg,
                hcfg_locksoundcache | hcfg_locktankcache | hcfg_mutebuttonenable);
        write32(controller, reg_inte, 0);

        // Reset all capture/FX buffer bookkeeping.
        ptr_write(controller, ireg_micbs, 0, adcbs_bufsize_none);
        ptr_write(controller, ireg_micba, 0, 0);
        ptr_write(controller, ireg_fxbs, 0, adcbs_bufsize_none);
        ptr_write(controller, ireg_fxba, 0, 0);
        ptr_write(controller, ireg_adcbs, 0, adcbs_bufsize_none);
        ptr_write(controller, ireg_adcba, 0, 0);
        // Disable channel loop/stop-on-loop interrupt sources.
        ptr_write(controller, ireg_cliel, 0, 0);
        ptr_write(controller, ireg_clieh, 0, 0);
        ptr_write(controller, ireg_solel, 0, 0);
        ptr_write(controller, ireg_soleh, 0, 0);
        // No host page table yet — DMA engine stays offline.
        ptr_write(controller, ireg_ptb, 0, 0);
        ptr_write(controller, ireg_tcb, 0, 0);
        ptr_write(controller, ireg_tcbs, 0, tcbs_bufsize_16k);

        // Cold-reset the AC'97 codec, then unmute master/PCM.
        emu10k1_codec_write(controller, ac97_reset, 0);
        settle(settle_spins);
        wait_codec_ready(controller);
        emu10k1_codec_write(controller, ac97_master_volume, 0x0000);
        emu10k1_codec_write(controller, ac97_pcmout_volume, 0x0808);

        // Codec vendor id readback is the cheapest "is the codec
        // link really alive" check available without DSP code.
        write8(controller, reg_ac97address, ac97_vendor_id1);
        controller.codec_vendor =
            hype_port_in16(static_cast<hcpp::u16>(controller.iobase +
                                                  reg_ac97data));

        // Re-enable CODEC outputs (AUDIOENABLE must be last).
        const hcpp::u32 hcfg = read32(controller, reg_hcfg);
        write32(controller, reg_hcfg, hcfg | hcfg_audioenable);
        settle(settle_spins);
        if ((read32(controller, reg_hcfg) & hcfg_audioenable) == 0) {
            return false;
        }
        // DMA prep: ring pages come from the PMM now so a future
        // PTB program has somewhere to point. The engine itself is
        // not armed: bringing it up needs the host page table (PTB),
        // MAPA/MAPB silent-page routing and the per-voice channel
        // program (CCCA/PSST/DSL/CPF/PTRX/CVCF/VTFT) — engine_ready
        // stays false and this block is the documented gap.
        ensure_buffer(controller);
        controller.reset_done =
            (ptr_read(controller, ireg_adcbs, 0) & 0x1f) == adcbs_bufsize_none;
        return controller.reset_done;
    }

    bool emu10k1_present() noexcept { return present; }

    bool emu10k1_probe() noexcept {
        const pci::PciDevice *device = emu10k1_find();
        if (device == nullptr) {
            serial::write_line("audiopci emu10k1=absent");
            return false;
        }
        if (!emu10k1_bind(*device, active) || !emu10k1_reset(active)) {
            serial::write_line("audiopci emu10k1=fail");
            return false;
        }
        present = true;
        serial::write("audiopci emu10k1=ok codec=");
        write_hex16(active.codec_vendor);
        serial::write("\r\n");
        audiopci_register({AudioChip::CreativeEmu10k1,
                           active.audigy ? "Creative Audigy" : "Creative SB Live!",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.iobase, false, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::emu10k1
