#include "kernel/audio/audioice1712.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::ice1712 {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;

        Ice1712Controller active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void write8(const Ice1712Controller &controller, hcpp::u8 reg,
                    hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u8 read8(const Ice1712Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in8(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        void write16(const Ice1712Controller &controller, hcpp::u8 reg,
                     hcpp::u16 value) noexcept {
            hype_port_out16(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u16 read16(const Ice1712Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in16(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        // Indirect register access via INDEX/DATA (snd_ice1712_write).
        void indirect_write(const Ice1712Controller &controller, hcpp::u8 reg,
                            hcpp::u8 value) noexcept {
            write8(controller, reg_index, reg);
            write8(controller, reg_data, value);
        }

        bool wait_ac97_idle(const Ice1712Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((read8(controller, reg_ac97_cmd) &
                     (ac97_write | ac97_read)) == 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        hcpp::u16 io_bar(const pci::PciDevice &device, hcpp::u8 index) noexcept {
            const hcpp::u32 raw = pci::bar(device, index);
            if ((raw & 0x1) == 0) {
                return 0;
            }
            const hcpp::u32 port = raw & ~hcpp::u32(0x3);
            return port != 0 && port <= 0xFF00
                       ? static_cast<hcpp::u16>(port)
                       : static_cast<hcpp::u16>(0);
        }

        bool claim_bars(const pci::PciDevice &device,
                        Ice1712Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space |
                                    pci_cmd_bus_master);
            controller.iobase = io_bar(device, 0);
            controller.ddma_port = io_bar(device, 1);
            controller.dmapath_port = io_bar(device, 2);
            controller.profi_port = io_bar(device, 3);
            return controller.iobase != 0;
        }

        bool ensure_buffer(Ice1712Controller &controller) noexcept {
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

    const pci::PciDevice *ice1712_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_icensemble &&
                device.device_id == device_ice1712) {
                return &device;
            }
        }
        return nullptr;
    }

    bool ice1712_bind(const pci::PciDevice &device,
                      Ice1712Controller &controller) noexcept {
        if (device.vendor_id != vendor_icensemble ||
            device.device_id != device_ice1712) {
            return false;
        }
        controller = {};
        return claim_bars(device, controller);
    }

    bool ice1712_codec_write(Ice1712Controller &controller, hcpp::u8 reg,
                             hcpp::u16 value) noexcept {
        if (controller.iobase == 0 || !wait_ac97_idle(controller)) {
            return false;
        }
        write8(controller, reg_ac97_index, reg);
        write16(controller, reg_ac97_data, value);
        write8(controller, reg_ac97_cmd, ac97_write);
        return wait_ac97_idle(controller);
    }

    bool ice1712_codec_read(Ice1712Controller &controller, hcpp::u8 reg,
                            hcpp::u16 &value) noexcept {
        if (controller.iobase == 0 || !wait_ac97_idle(controller)) {
            return false;
        }
        write8(controller, reg_ac97_index, reg);
        write8(controller, reg_ac97_cmd, ac97_read);
        if (!wait_ac97_idle(controller)) {
            return false;
        }
        value = read16(controller, reg_ac97_data);
        return true;
    }

    bool ice1712_reset(Ice1712Controller &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // Soft reset into native mode (snd_ice1712_chip_init).
        write8(controller, reg_control, ctrl_reset | ctrl_native);
        settle(settle_spins);
        write8(controller, reg_control, ctrl_native);
        settle(settle_spins);

        // Consumer + professional paths out of powerdown.
        indirect_write(controller, ireg_consumer_powerdown, 0);
        indirect_write(controller, ireg_pro_powerdown, 0);

        // AC'97 warm reset, then wait for the codec ready bit.
        write8(controller, reg_ac97_cmd, ac97_warm);
        settle(settle_spins >> 2);
        write8(controller, reg_ac97_cmd, 0);
        for (hcpp::u32 i = 0; i < poll_limit; ++i) {
            if ((read8(controller, reg_ac97_cmd) & ac97_ready) != 0) {
                break;
            }
            asm volatile("pause");
        }

        ice1712_codec_write(controller, ac97_reg_master_volume, 0x0000);
        ice1712_codec_write(controller, ac97_reg_pcmout_volume, 0x0808);
        hcpp::u16 vendor = 0;
        if (ice1712_codec_read(controller, ac97_reg_vendor_id1, vendor)) {
            controller.codec_vendor = vendor;
        }
        // Mask all IRQ sources — no handler plumbing yet.
        write8(controller, reg_irqmask, 0xff);
        ensure_buffer(controller);
        // DS channel map + multi-track FIFOs stay unprogrammed —
        // engine offline.
        controller.reset_done = true;
        return true;
    }

    bool ice1712_present() noexcept { return present; }

    bool ice1712_probe() noexcept {
        const pci::PciDevice *device = ice1712_find();
        if (device == nullptr) {
            serial::write_line("audiopci ice1712=absent");
            return false;
        }
        if (!ice1712_bind(*device, active) || !ice1712_reset(active)) {
            serial::write_line("audiopci ice1712=fail");
            return false;
        }
        present = true;
        serial::write("audiopci ice1712=ok codec=");
        write_hex16(active.codec_vendor);
        serial::write("\r\n");
        audiopci_register({AudioChip::Ice1712, "ICEnsemble ICE1712",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.iobase, false, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::ice1712
