#include "kernel/audio/audioca0106.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::ca0106 {
    namespace {

        constexpr hcpp::u32 settle_spins = 0x20000;
        constexpr hcpp::u32 period_bytes = 1024;

        Ca0106Controller active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void write32(const Ca0106Controller &controller, hcpp::u8 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u32 read32(const Ca0106Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        // Indexed register access (ALSA ca0106_main.c):
        // PTR takes (reg << 16) | channel, DATA carries the value.
        void ptr_write(const Ca0106Controller &controller, hcpp::u8 reg,
                       hcpp::u8 channel, hcpp::u32 value) noexcept {
            write32(controller, reg_ptr,
                    (static_cast<hcpp::u32>(reg) << 16) | channel);
            write32(controller, reg_data, value);
        }

        bool claim_bar(const pci::PciDevice &device,
                       Ca0106Controller &controller) noexcept {
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

        // Playback period-pointer list: 8-byte entries
        // {dma_address, size << 16} living in a PMM page, plus the
        // audio buffer itself. Mirrors snd_ca0106_pcm_prepare.
        bool program_engine(Ca0106Controller &controller) noexcept {
            if (controller.list_phys == 0) {
                controller.list_phys =
                    static_cast<hcpp::u32>(memory::pmm_allocate_page());
                controller.buffer_phys =
                    static_cast<hcpp::u32>(memory::pmm_allocate_pages(2));
                if (controller.list_phys == 0 || controller.buffer_phys == 0) {
                    return false;
                }
                controller.buffer_bytes = period_bytes * list_periods;
                auto *list = reinterpret_cast<volatile hcpp::u32 *>(controller.list_phys);
                for (hcpp::u32 i = 0; i < list_periods; ++i) {
                    list[i * 2] = controller.buffer_phys + i * period_bytes;
                    list[i * 2 + 1] = period_bytes << 16;
                }
            }
            const hcpp::u8 channel = 0;
            ptr_write(controller, ireg_playback_list_addr, channel,
                      controller.list_phys);
            ptr_write(controller, ireg_playback_list_size, channel,
                      (list_periods - 1) << 19);
            ptr_write(controller, ireg_playback_list_ptr, channel, 0);
            ptr_write(controller, ireg_playback_dma_addr, channel,
                      controller.buffer_phys);
            ptr_write(controller, ireg_playback_period_size, channel,
                      period_bytes << 16);
            ptr_write(controller, ireg_playback_pointer, channel, 0);
            ptr_write(controller, 0x07, channel, 0);
            ptr_write(controller, ireg_playback_fifo_offset, channel, 0);
            ptr_write(controller, ireg_playback_mute, 0, 0);  // unmute
            controller.engine_ready = true;
            return true;
        }

    }  // namespace

    const pci::PciDevice *ca0106_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_creative &&
                device.device_id == device_ca0106) {
                return &device;
            }
        }
        return nullptr;
    }

    bool ca0106_bind(const pci::PciDevice &device,
                     Ca0106Controller &controller) noexcept {
        if (device.vendor_id != vendor_creative ||
            device.device_id != device_ca0106) {
            return false;
        }
        controller = {};
        return claim_bar(device, controller);
    }

    bool ca0106_reset(Ca0106Controller &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // Stop the chip: mask everything, drop audio output.
        ptr_write(controller, ireg_basic_interrupt, 0, 0);
        write32(controller, reg_inte, 0);
        ptr_write(controller, ireg_extended_int_mask, 0, 0);
        write32(controller, reg_hcfg, 0);
        settle(settle_spins);

        // Init sequence from snd_ca0106_chip_init().
        ptr_write(controller, ireg_spdif_select1, 0, 0xf);
        ptr_write(controller, ireg_spdif_select2, 0, 0x000f0000);
        ptr_write(controller, ireg_capture_control, 0, 0x40c81000);
        ptr_write(controller, ireg_capture_control, 1, 0xffffffff);
        ptr_write(controller, ireg_capture_control, 2, 0x30300000);
        ptr_write(controller, ireg_capture_control, 3, 0x00700000);
        ptr_write(controller, ireg_playback_routing1, 0, 0x32765410);
        ptr_write(controller, ireg_playback_routing2, 0, 0x76767676);
        ptr_write(controller, ireg_capture_routing1, 0, 0x32765410);
        ptr_write(controller, ireg_capture_routing2, 0, 0x76767676);
        for (hcpp::u8 ch = 0; ch < play_channels; ++ch) {
            ptr_write(controller, ireg_capture_volume1, ch, 0x30303030);
            ptr_write(controller, ireg_capture_volume2, ch, 0x30303030);
        }
        // Default capture source: Mic/Line/TAD/Aux per i2s path.
        ptr_write(controller, ireg_capture_source, 0, 0x333300e4);
        // GPIO: analog outputs (default gpio_type 0 board).
        write32(controller, reg_gpio, 0);
        write32(controller, reg_gpio, gpio_analog_default);
        // AC'97 2.0 link + audio output enable.
        write32(controller, reg_hcfg, hcfg_ac97 | hcfg_audioenable);
        settle(settle_spins);
        if ((read32(controller, reg_hcfg) &
             (hcfg_ac97 | hcfg_audioenable)) !=
            (hcfg_ac97 | hcfg_audioenable)) {
            return false;
        }
        program_engine(controller);
        controller.reset_done = true;
        return true;
    }

    bool ca0106_present() noexcept { return present; }

    bool ca0106_probe() noexcept {
        const pci::PciDevice *device = ca0106_find();
        if (device == nullptr) {
            serial::write_line("audiopci ca0106=absent");
            return false;
        }
        if (!ca0106_bind(*device, active) || !ca0106_reset(active)) {
            serial::write_line("audiopci ca0106=fail");
            return false;
        }
        present = true;
        serial::write_line("audiopci ca0106=ok");
        audiopci_register({AudioChip::CreativeCa0106, "Creative CA0106",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.iobase, false, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::ca0106
