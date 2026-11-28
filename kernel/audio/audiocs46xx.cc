#include "kernel/audio/audiocs46xx.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::cs46xx {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;

        Cs46xxController active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        volatile hcpp::u32 &reg(const Cs46xxController &controller,
                                hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(controller.ba0 + offset);
        }

        bool is_cs46xx(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != vendor_cirrus) {
                return false;
            }
            return device.device_id == device_cs4610 ||
                   device.device_id == device_cs4614;
        }

        hcpp::uintptr_t bar_base(const pci::PciDevice &device, hcpp::u8 index) noexcept {
            const hcpp::u32 raw = pci::bar(device, index);
            if ((raw & 0x1) != 0 || raw == 0) {
                return 0;
            }
            hcpp::uintptr_t base = raw & ~hcpp::u32(0xF);
            if (memory::vmm_initialized() && memory::vmm_translate(base) != base) {
                return 0;
            }
            return base;
        }

        bool claim_bars(const pci::PciDevice &device,
                        Cs46xxController &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_memory_space |
                                    pci_cmd_bus_master);
            controller.ba0 = bar_base(device, 0);
            controller.ba1 = bar_base(device, 1);
            return controller.ba0 != 0;  // BA1 optional for probe
        }

        bool ensure_buffer(Cs46xxController &controller) noexcept {
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

        bool wait_dcv_clear(const Cs46xxController &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg(controller, ba0_acctl) & acctl_dcv) == 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        void write_hex16(hcpp::u16 value) noexcept {
            for (int shift = 12; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *cs46xx_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_cs46xx(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    bool cs46xx_bind(const pci::PciDevice &device,
                     Cs46xxController &controller) noexcept {
        if (!is_cs46xx(device)) {
            return false;
        }
        controller = {};
        controller.device_id = device.device_id;
        return claim_bars(device, controller);
    }

    // AC'97 register write — the documented 4-step handshake from
    // snd_cs46xx_codec_read/write: ACCAD=reg, ACCDA=data, ACCTL
    // pulse of DCV with CRW|VFRM|ESYN|RSTN, wait DCV clear.
    bool cs46xx_codec_write(Cs46xxController &controller, hcpp::u8 reg8,
                            hcpp::u16 value) noexcept {
        if (controller.ba0 == 0) {
            return false;
        }
        hcpp::u32 ctl = reg(controller, ba0_acctl);
        if ((ctl & acctl_vfrm) == 0) {
            reg(controller, ba0_acctl) = (ctl & ~acctl_esyn) | acctl_vfrm;
            reg(controller, ba0_acctl) = acctl_esyn | acctl_vfrm;
        }
        reg(controller, ba0_accad) = reg8;
        reg(controller, ba0_accda) = value;
        reg(controller, ba0_acctl) = acctl_crw | acctl_vfrm | acctl_esyn |
                                     acctl_rstn;
        reg(controller, ba0_acctl) = acctl_dcv | acctl_crw | acctl_vfrm |
                                     acctl_esyn | acctl_rstn;
        return wait_dcv_clear(controller);
    }

    bool cs46xx_reset(Cs46xxController &controller) noexcept {
        if (controller.ba0 == 0) {
            return false;
        }
        // Mask host interrupts while the serial port comes up.
        reg(controller, ba0_hicr) = 0;
        // Serial port 1 in AC'97 mode, port enabled.
        reg(controller, ba0_sermc1) = sermc1_ptc_ac97 | sermc1_mspe;
        settle(settle_spins);

        // Bring the codec link out of reset: RSTN + VFRM + ESYN.
        reg(controller, ba0_acctl) = 0;
        settle(settle_spins);
        reg(controller, ba0_acctl) = acctl_rstn | acctl_esyn | acctl_vfrm;
        settle(settle_spins << 1);

        // Codec reset register, then unmute master/PCM.
        cs46xx_codec_write(controller, ac97_reset, 0);
        settle(settle_spins);
        cs46xx_codec_write(controller, ac97_master_volume, 0x0000);
        cs46xx_codec_write(controller, ac97_pcmout_volume, 0x0808);

        // Read back the codec vendor id through the status path to
        // prove the link is live (per ACSAD/ACSDA status regs a
        // read command also returns data via ACSTS/ACCDA).
        reg(controller, ba0_accad) = ac97_vendor_id1;
        reg(controller, ba0_acctl) = acctl_dcv | acctl_vfrm | acctl_esyn |
                                    acctl_rstn;  // CRW clear => read
        if (wait_dcv_clear(controller) &&
            (reg(controller, ba0_acsts) & acsts_vsts) != 0) {
            controller.codec_vendor =
                static_cast<hcpp::u16>(reg(controller, ba0_acsda));
        }
        ensure_buffer(controller);
        // DMA engine needs the CWCDSP firmware image + SCB task tree
        // built in the BA1 window; not loaded in this wave — engine
        // stays offline.
        controller.reset_done = true;
        return true;
    }

    bool cs46xx_present() noexcept { return present; }

    bool cs46xx_probe() noexcept {
        const pci::PciDevice *device = cs46xx_find();
        if (device == nullptr) {
            serial::write_line("audiopci cs46xx=absent");
            return false;
        }
        if (!cs46xx_bind(*device, active) || !cs46xx_reset(active)) {
            serial::write_line("audiopci cs46xx=fail");
            return false;
        }
        present = true;
        serial::write("audiopci cs46xx=ok codec=");
        write_hex16(active.codec_vendor);
        serial::write("\r\n");
        audiopci_register({AudioChip::CirrusCs46xx, "Cirrus CS46xx",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.ba0, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::cs46xx
