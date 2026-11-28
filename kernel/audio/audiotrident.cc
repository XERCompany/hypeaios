#include "kernel/audio/audiotrident.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiow2.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::trident {
    namespace {

        TridentController active{};
        bool present;

        // NX TLB: 4096 little-endian entries, 16 KiB aligned, identity
        // mapped (static BSS keeps the 16 KiB alignment for free).
        alignas(16384) hcpp::u32 tlb[tlb_entries];
        hcpp::u32 silent_page_phys;

        hcpp::u32 in32(const TridentController &c, hcpp::u16 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(c.iobase + reg));
        }

        hcpp::u16 in16(const TridentController &c, hcpp::u16 reg) noexcept {
            return hype_port_in16(static_cast<hcpp::u16>(c.iobase + reg));
        }

        void out32(const TridentController &c, hcpp::u16 reg,
                   hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void out8(const TridentController &c, hcpp::u16 reg,
                  hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void pci_config_write(const pci::PciDevice &device, hcpp::u16 reg,
                              hcpp::u32 value, hcpp::u8 bytes) noexcept {
            // Per-register 8/16/32-bit config writes (no write8/16 in
            // the pci namespace, so funnel through read-modify-write).
            const hcpp::u32 aligned = reg & ~hcpp::u16(0x3);
            const hcpp::u32 shift = (reg & 0x3) * 8;
            hcpp::u32 old = pci::config_read32(device.bus, device.slot,
                                               device.function, aligned);
            if (bytes == 4) {
                old = value;
            } else if (bytes == 2) {
                const hcpp::u32 mask = 0xFFFFu << shift;
                old = (old & ~mask) | ((value << shift) & mask);
            } else {
                const hcpp::u32 mask = 0xFFu << shift;
                old = (old & ~mask) | ((value << shift) & mask);
            }
            pci::config_write32(device.bus, device.slot, device.function,
                                aligned, old);
        }

        bool wait_reg(const TridentController &c, hcpp::u16 reg,
                      hcpp::u32 mask, hcpp::u32 want) noexcept {
            for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
                if ((in32(c, reg) & mask) == want) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_reg16(const TridentController &c, hcpp::u16 reg,
                        hcpp::u16 mask, hcpp::u16 want) noexcept {
            for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
                if ((in16(c, reg) & mask) == want) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // Selects a hardware voice (0-63) for the CH_* register window.
        void select_voice(const TridentController &c,
                          hcpp::u8 voice) noexcept {
            out8(c, t4d_lfo_gc_cir, voice);
        }

        bool dx_init(const pci::PciDevice &device,
                     TridentController &c) noexcept {
            // snd_trident_4d_dx_init: reset legacy config + wavetable
            // block through the chip's PCI config dword at 0x40.
            pci_config_write(device, 0x40, 0, 4);   // DDMA
            pci_config_write(device, 0x44, 0, 1);   // ports
            pci_config_write(device, 0x45, 0, 1);   // legacy DMA
            pci_config_write(device, 0x46, 4, 1);   // reset
            w2::settle(0x40000);
            pci_config_write(device, 0x46, 0, 1);   // release
            w2::settle(0x40000);

            // AC'97 warm reset then "DAC on" control value.
            out32(c, dx_acr2_ac97_com_stat, 0x00000001);
            w2::settle(0x40000);
            out32(c, dx_acr2_ac97_com_stat, 0x00000000);
            c.ac97_ctrl = 0x0000004A;
            out32(c, dx_acr2_ac97_com_stat, c.ac97_ctrl);
            return wait_reg(c, dx_acr2_ac97_com_stat, dx_ac97_ready,
                            dx_ac97_ready);
        }

        bool nx_init(const pci::PciDevice &device,
                     TridentController &c) noexcept {
            // snd_trident_4d_nx_init.
            pci_config_write(device, 0x40, 0, 4);
            pci_config_write(device, 0x44, 0, 1);
            pci_config_write(device, 0x45, 0, 1);
            pci_config_write(device, 0x46, 1, 1);   // reset
            w2::settle(0x40000);
            pci_config_write(device, 0x46, 0, 1);
            w2::settle(0x40000);

            out32(c, nx_acr0_ac97_com_stat, 0x00000001);
            w2::settle(0x40000);
            out32(c, nx_acr0_ac97_com_stat, 0x00000000);
            if (!wait_reg(c, nx_acr0_ac97_com_stat, nx_ac97_primary_ready,
                          nx_ac97_primary_ready)) {
                return false;
            }
            // DAC on.
            c.ac97_ctrl = 0x00000002;
            out32(c, nx_acr0_ac97_com_stat, c.ac97_ctrl);
            // Disable SoundBlaster IRQ, stop every voice.
            out32(c, t4d_miscint, nx_sb_irq_disable);
            return true;
        }

        void stop_all_voices(const TridentController &c) noexcept {
            out32(c, t4d_stop_a, 0xFFFFFFFF);
            out32(c, t4d_stop_b, 0xFFFFFFFF);
            out32(c, t4d_ainten_a, 0);
            out32(c, t4d_ainten_b, 0);
        }

        bool ac97_write(TridentController &c, hcpp::u8 reg,
                        hcpp::u16 data) noexcept {
            if (c.kind == TridentKind::Nx) {
                if (!wait_reg16(c, nx_acr1_ac97_w, nx_ac97_busy_write, 0)) {
                    return false;
                }
                out32(c, nx_acr1_ac97_w,
                      (static_cast<hcpp::u32>(data) << 16) |
                          nx_ac97_busy_write | reg);
                return true;
            }
            // DX + SiS layout share the busy bit position.
            if (!wait_reg16(c, dx_acr0_ac97_w, dx_ac97_busy_write, 0)) {
                return false;
            }
            out32(c, dx_acr0_ac97_w,
                  (static_cast<hcpp::u32>(data) << 16) |
                      dx_ac97_busy_write | reg);
            return true;
        }

        bool ac97_read(TridentController &c, hcpp::u8 reg,
                   hcpp::u16 &value) noexcept {
            if (c.kind == TridentKind::Nx) {
                out32(c, nx_acr2_ac97_r_primary, nx_ac97_busy_read | reg);
                for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
                    const hcpp::u32 data =
                        in32(c, nx_acr2_ac97_r_primary);
                    if ((data & 0x00000C00) == 0) {
                        value = static_cast<hcpp::u16>(data >> 16);
                        return true;
                    }
                    asm volatile("pause");
                }
                return false;
            }
            out32(c, dx_acr1_ac97_r, dx_ac97_busy_read | reg);
            for (hcpp::u32 i = 0; i < w2::poll_limit; ++i) {
                const hcpp::u32 data = in32(c, dx_acr1_ac97_r);
                if ((data & dx_ac97_busy_read) == 0) {
                    value = static_cast<hcpp::u16>(data >> 16);
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // 4DWave rate delta for 44100 Hz (snd_trident_convert_rate).
        constexpr hcpp::u32 delta_44100 = 0xEB3;

        bool ensure_buffer(TridentController &c) noexcept {
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

        bool setup_tlb(TridentController &c) noexcept {
            if (c.kind != TridentKind::Nx) {
                return true;
            }
            if (silent_page_phys == 0) {
                const hcpp::uintptr_t page = memory::pmm_allocate_page();
                if (page == 0) {
                    return false;
                }
                silent_page_phys = static_cast<hcpp::u32>(page);
                auto *silence =
                    reinterpret_cast<volatile hcpp::u32 *>(page);
                for (hcpp::u32 i = 0; i < memory::page_size / 4; ++i) {
                    silence[i] = 0;
                }
            }
            for (hcpp::u32 i = 0; i < tlb_entries; ++i) {
                tlb[i] = silent_page_phys & ~hcpp::u32(0xFFF);
            }
            for (hcpp::u32 i = 0; i < ring_pages; ++i) {
                tlb[i] = (c.buffer_phys + i * memory::page_size) &
                         ~hcpp::u32(0xFFF);
            }
            const hcpp::u32 tlb_phys =
                static_cast<hcpp::u32>(
                    reinterpret_cast<hcpp::uintptr_t>(&tlb[0]));
            out32(c, nx_tlbc, tlb_phys | 0x00000001);
            return true;
        }

        // Programs voice 0 for 16-bit stereo loop playback of the whole
        // ring buffer, matching snd_trident_write_voice_regs().
        void write_voice_regs(TridentController &c) noexcept {
            hcpp::u32 regs[5];
            const hcpp::u32 cso = 0;
            const hcpp::u32 eso = c.buffer_frames - 1;  // in samples
            const hcpp::u32 delta = delta_44100;
            // Loop-mode, signed 16-bit stereo (snd_trident_control_mode).
            const hcpp::u32 ctrl = 0x00000001 | 0x00000008 | 0x00000002 |
                                   0x00000004;
            const hcpp::u32 lba =
                c.kind == TridentKind::Nx ? 0 : c.buffer_phys;
            const hcpp::u32 fmc_rvol_cvol =
                (3u << 14) | (0x7Fu << 7) | 0x7F;

            regs[1] = lba;
            regs[4] = (1u << 31) |         // GVSel
                      ((0u & 0x7F) << 24) |  // Pan centre
                      ((ctrl & 0xF) << 12) |
                      ((0u & 0x3FC) << 14) |  // Vol = 0 attenuation
                      (0u & 0xFFF);        // EC
            regs[3] = fmc_rvol_cvol;
            if (c.kind == TridentKind::Nx) {
                regs[0] = (delta << 24) | (cso & 0x00FFFFFF);
                regs[2] = ((delta << 16) & 0xFF000000) |
                          (eso & 0x00FFFFFF);
                regs[3] |= (0u << 20) | (0u << 16);  // Alpha | FMS
            } else {
                regs[0] = (cso << 16) | ((0u & 0xFFF) << 4) | (0u & 0xF);
                regs[2] = (eso << 16) | (delta & 0xFFFF);
            }

            select_voice(c, c.voice);
            for (hcpp::u8 i = 0; i < 5; ++i) {
                out32(c, ch_start + i * 4, regs[i]);
            }
        }

    }  // namespace

    const pci::PciDevice *trident_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_trident &&
                (device.device_id == device_4dwave_dx ||
                 device.device_id == device_4dwave_nx)) {
                return &device;
            }
            if ((device.vendor_id == vendor_sis &&
                  device.device_id == device_sis7018) ||
                (device.vendor_id == vendor_ali &&
                  device.device_id == device_ali5451)) {
                return &device;
            }
        }
        return nullptr;
    }

    bool trident_bind(const pci::PciDevice &device,
                      TridentController &controller) noexcept {
        controller = {};
        controller.voice = 0;
        if (device.vendor_id == vendor_trident &&
            device.device_id == device_4dwave_nx) {
            controller.kind = TridentKind::Nx;
        } else if (device.vendor_id == vendor_sis) {
            controller.kind = TridentKind::Sis7018;
        } else if (device.vendor_id == vendor_ali) {
            controller.kind = TridentKind::Ali5451;
        } else {
            controller.kind = TridentKind::Dx;
        }
        if (!w2::enable(device, true, false)) {
            return false;
        }
        controller.iobase = w2::io_base(device, 0);
        return controller.iobase != 0;
    }

    bool trident_reset(TridentController &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        const pci::PciDevice *device = nullptr;
        const pci::PciDevice *devices = pci::devices();
        for (hcpp::size_t i = 0; i < pci::device_count(); ++i) {
            const pci::PciDevice &d = devices[i];
            if ((d.vendor_id == vendor_trident &&
                  (d.device_id == device_4dwave_dx ||
                   d.device_id == device_4dwave_nx)) ||
                (d.vendor_id == vendor_sis &&
                  d.device_id == device_sis7018) ||
                (d.vendor_id == vendor_ali &&
                  d.device_id == device_ali5451)) {
                device = &d;
                break;
            }
        }
        if (device == nullptr) {
            return false;
        }
        bool ready;
        switch (controller.kind) {
            case TridentKind::Nx:
                ready = nx_init(*device, controller);
                break;
            case TridentKind::Sis7018:
            case TridentKind::Ali5451:
                // SiS/ALi clones share the DX-era register file; the
                // full init path differs, so we only run the DX reset
                // and let codec verification decide.
                ready = dx_init(*device, controller);
                break;
            default:
                ready = dx_init(*device, controller);
                break;
        }
        if (!ready) {
            return false;
        }
        stop_all_voices(controller);
        controller.reset_done = true;
        return true;
    }

    bool trident_play(TridentController &controller) noexcept {
        if (!controller.reset_done || !ensure_buffer(controller)) {
            return false;
        }
        if (!setup_tlb(controller)) {
            return false;
        }
        auto *samples = reinterpret_cast<hcpp::i16 *>(
            static_cast<hcpp::uintptr_t>(controller.buffer_phys));
        waveform_render_sine(samples, controller.buffer_frames,
                             tone_freq_hz, pcm_rate, pcm_channels);
        write_voice_regs(controller);
        const hcpp::u32 mask = 1u << (controller.voice & 0x1F);
        out32(controller, t4d_start_a, mask);
        controller.running = true;
        return true;
    }

    void trident_stop(TridentController &controller) noexcept {
        if (controller.iobase != 0) {
            const hcpp::u32 mask = 1u << (controller.voice & 0x1F);
            out32(controller, t4d_stop_a, mask);
        }
        controller.running = false;
    }

    hcpp::u32 trident_pos(TridentController &controller) noexcept {
        if (!controller.running || controller.iobase == 0) {
            return 0;
        }
        select_voice(controller, controller.voice);
        if (controller.kind == TridentKind::Nx) {
            return in32(controller, ch_nx_delta_cso) & 0x00FFFFFF;
        }
        return in32(controller, ch_dx_cso_alpha_fms) >> 16;
    }

    bool trident_present() noexcept { return present; }

    bool trident_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = trident_find();
        if (device == nullptr) {
            serial::write_line("audiopci trident=absent");
            return true;
        }
        if (!trident_bind(*device, active) || !trident_reset(active)) {
            serial::write_line("audiopci trident=fail");
            return false;
        }
        const char *kind = active.kind == TridentKind::Dx     ? "dx"
                           : active.kind == TridentKind::Nx   ? "nx"
                           : active.kind == TridentKind::Sis7018 ? "sis"
                                                               : "ali";
        serial::write("audiopci trident kind=");
        serial::write_line(kind);
        hcpp::u16 codec_id = 0;
        if (ac97_read(active, 0x7C, codec_id)) {
            serial::write("audiopci trident codec=0x");
            w2::hex16(codec_id);
            serial::write("\r\n");
        }
        ac97_write(active, ac97_master, 0x0000);
        if (!trident_play(active)) {
            serial::write_line("audiopci trident dma=fail");
            return false;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 256 && pos == 0; ++round) {
            w2::settle(0x80000);
            pos = trident_pos(active);
        }
        serial::write("audiopci trident pos=");
        w2::hex32(pos);
        serial::write("\r\n");
        trident_stop(active);
        present = true;
        serial::write_line("audiopci trident=ok");
        return true;
    }

}  // namespace hype::audio::trident
