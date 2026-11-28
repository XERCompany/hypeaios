#include "kernel/audio/audiocs5535audio.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::cs5535audio {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;
        constexpr hcpp::u32 period_bytes = 2048;

        Cs5535Controller active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void write8(const Cs5535Controller &controller, hcpp::u8 reg,
                    hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        void write32(const Cs5535Controller &controller, hcpp::u8 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u32 read32(const Cs5535Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        bool is_cs5535audio(const pci::PciDevice &device) noexcept {
            return (device.vendor_id == vendor_ns &&
                    device.device_id == device_cs5535_audio) ||
                   (device.vendor_id == vendor_amd &&
                    device.device_id == device_cs5536_audio);
        }

        bool claim_bar(const pci::PciDevice &device,
                       Cs5535Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space |
                                    pci_cmd_bus_master);
            const hcpp::u32 bar0 = pci::bar(device, 0);
            if ((bar0 & 0x1) == 0) {
                return false;
            }
            const hcpp::u32 iobase = bar0 & ~hcpp::u32(0x3);
            if (iobase == 0 || iobase > 0xFF00) {
                return false;
            }
            controller.iobase = static_cast<hcpp::u16>(iobase);
            return true;
        }

        // wait_till_cmd_acked(): CMD_NEW clears when the codec link
        // has consumed the command.
        bool wait_cmd_acked(const Cs5535Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((read32(controller, acc_codec_cntl) & cmd_new) == 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // Build a PMM PRD table for BM0: 8-byte descriptors
        // {address, byte_count|flags}. Mirrors the ALSA DMA setup.
        bool program_prd(Cs5535Controller &controller) noexcept {
            if (controller.prd_phys == 0) {
                controller.prd_phys =
                    static_cast<hcpp::u32>(memory::pmm_allocate_page());
                controller.buffer_phys =
                    static_cast<hcpp::u32>(
                        memory::pmm_allocate_pages(prd_count * period_bytes /
                                                   memory::page_size));
                if (controller.prd_phys == 0 || controller.buffer_phys == 0) {
                    return false;
                }
                controller.buffer_bytes = prd_count * period_bytes;
                auto *prd =
                    reinterpret_cast<volatile hcpp::u32 *>(controller.prd_phys);
                for (hcpp::u32 i = 0; i < prd_count; ++i) {
                    prd[i * 2] = controller.buffer_phys + i * period_bytes;
                    hcpp::u32 flags = 0;
                    if (i == prd_count - 1) {
                        flags = prd_eop | prd_jmp;  // wrap to prd[0]
                    }
                    prd[i * 2 + 1] = period_bytes | (flags << 16);
                }
            }
            write32(controller, acc_bm0_prd, controller.prd_phys);
            controller.engine_ready = true;
            // BM_CTL_EN is deliberately not written: no IRQ path yet.
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

    const pci::PciDevice *cs5535audio_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_cs5535audio(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    bool cs5535audio_bind(const pci::PciDevice &device,
                          Cs5535Controller &controller) noexcept {
        if (!is_cs5535audio(device)) {
            return false;
        }
        controller = {};
        return claim_bar(device, controller);
    }

    bool cs5535audio_codec_write(Cs5535Controller &controller, hcpp::u8 reg,
                                 hcpp::u16 value) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        hcpp::u32 regdata = static_cast<hcpp::u32>(reg) << 24;
        regdata |= value;
        regdata &= cmd_mask;
        regdata |= cmd_new;  // bit31 clear => write command
        write32(controller, acc_codec_cntl, regdata);
        return wait_cmd_acked(controller);
    }

    bool cs5535audio_codec_read(Cs5535Controller &controller, hcpp::u8 reg,
                                hcpp::u16 &value) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        hcpp::u32 regdata = static_cast<hcpp::u32>(reg) << 24;
        regdata |= codec_cntl_rd_cmd | cmd_new;
        write32(controller, acc_codec_cntl, regdata);
        if (!wait_cmd_acked(controller)) {
            return false;
        }
        for (hcpp::u32 i = 0; i < poll_limit; ++i) {
            const hcpp::u32 status = read32(controller, acc_codec_status);
            if ((status & sts_new) != 0 &&
                static_cast<hcpp::u8>(status >> 24) == reg) {
                value = static_cast<hcpp::u16>(status);
                return true;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool cs5535audio_reset(Cs5535Controller &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // Stop both busmaster channels, then warm-reset the link.
        write8(controller, acc_bm0_cmd, bm_ctl_dis);
        write8(controller, acc_bm1_cmd, bm_ctl_dis);
        write32(controller, acc_codec_cntl, codec_cntl_lnk_wrm_rst);
        settle(settle_spins << 1);

        cs5535audio_codec_write(controller, ac97_reset, 0);
        settle(settle_spins);
        cs5535audio_codec_write(controller, ac97_master_volume, 0x0000);
        cs5535audio_codec_write(controller, ac97_pcmout_volume, 0x0808);

        hcpp::u16 vendor = 0;
        if (cs5535audio_codec_read(controller, ac97_vendor_id1, vendor)) {
            controller.codec_vendor = vendor;
        }
        program_prd(controller);
        controller.reset_done = true;
        return true;
    }

    bool cs5535audio_present() noexcept { return present; }

    bool cs5535audio_probe() noexcept {
        const pci::PciDevice *device = cs5535audio_find();
        if (device == nullptr) {
            serial::write_line("audiopci cs5535audio=absent");
            return false;
        }
        if (!cs5535audio_bind(*device, active) || !cs5535audio_reset(active)) {
            serial::write_line("audiopci cs5535audio=fail");
            return false;
        }
        present = true;
        serial::write("audiopci cs5535audio=ok codec=");
        write_hex16(active.codec_vendor);
        serial::write("\r\n");
        audiopci_register({AudioChip::GeodeCs5535Audio,
                           device->vendor_id == vendor_amd
                               ? "AMD CS5536 Audio"
                               : "NS CS5535 Audio",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.iobase, false, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::cs5535audio
