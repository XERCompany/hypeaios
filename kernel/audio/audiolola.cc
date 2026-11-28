#include "kernel/audio/audiolola.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::lola {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;

        LolaController active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        hcpp::u8 read8(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u8 *>(base + offset);
        }

        hcpp::u16 read16(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u16 *>(base + offset);
        }

        hcpp::u32 read32(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(base + offset);
        }

        void write8(hcpp::uintptr_t base, hcpp::u32 offset, hcpp::u8 value) noexcept {
            *reinterpret_cast<volatile hcpp::u8 *>(base + offset) = value;
        }

        void write16(hcpp::uintptr_t base, hcpp::u32 offset, hcpp::u16 value) noexcept {
            *reinterpret_cast<volatile hcpp::u16 *>(base + offset) = value;
        }

        void write32(hcpp::uintptr_t base, hcpp::u32 offset, hcpp::u32 value) noexcept {
            *reinterpret_cast<volatile hcpp::u32 *>(base + offset) = value;
        }

        hcpp::uintptr_t bar_base(const pci::PciDevice &device, hcpp::u8 index) noexcept {
            const hcpp::u32 raw = pci::bar(device, index);
            if ((raw & 0x1) != 0 || raw == 0) {
                return 0;
            }
            hcpp::uintptr_t base = raw & ~hcpp::u32(0xF);
            if ((raw & 0x6) == 0x4) {
                const hcpp::u32 high =
                    pci::config_read32(device.bus, device.slot,
                                       device.function, 0x10 + index * 4 + 4);
                if (high != 0) {
                    return 0;
                }
            }
            if (memory::vmm_initialized() && memory::vmm_translate(base) != base) {
                return 0;
            }
            return base;
        }

        bool claim_bars(const pci::PciDevice &device,
                        LolaController &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_memory_space |
                                    pci_cmd_bus_master);
            controller.bar0 = bar_base(device, 0);
            controller.bar1 = bar_base(device, 1);
            return controller.bar0 != 0;
        }

        bool wait_gctl(const LolaController &controller,
                       hcpp::u32 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((read32(controller.bar0, bar0_gctl) & gctl_reset) ==
                    expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // setup_corb_rirb() from lola.c: one page, CORB at offset 0,
        // RIRB at offset 2048, both 256-entry rings.
        bool setup_corb_rirb(LolaController &controller) noexcept {
            controller.rb_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_page());
            if (controller.rb_phys == 0) {
                return false;
            }
            controller.corb =
                reinterpret_cast<volatile hcpp::u32 *>(controller.rb_phys);
            controller.rirb =
                reinterpret_cast<volatile hcpp::u32 *>(controller.rb_phys +
                                                       rirb_offset);
            // Stop both ring DMAs first.
            write8(controller.bar0, bar0_rirbctl, 0);
            write8(controller.bar0, bar0_corbctl, 0);
            settle(settle_spins);

            // CORB: base, 256-entry size, wp=0, rp reset, DMA on.
            write32(controller.bar0, bar0_corblbase, controller.rb_phys);
            write32(controller.bar0, bar0_corbubase, 0);
            write8(controller.bar0, bar0_corbsize, ring_size_256);
            write16(controller.bar0, bar0_corbwp, 0);
            write16(controller.bar0, bar0_corbrp, rbrwp_clr);
            write8(controller.bar0, bar0_corbctl, rbctl_dma_en);
            const hcpp::u8 corb_sts =
                read8(controller.bar0, bar0_corbsts) & 0x1;
            if (corb_sts != 0) {
                write8(controller.bar0, bar0_corbsts, corb_sts);
            }
            controller.corb_wp = 0;

            // RIRB: base, 256-entry size, wp reset, N=1, DMA + irq.
            write32(controller.bar0, bar0_rirblbase,
                    controller.rb_phys + rirb_offset);
            write32(controller.bar0, bar0_rirbubase, 0);
            write8(controller.bar0, bar0_rirbsize, ring_size_256);
            write16(controller.bar0, bar0_rirbwp, rbrwp_clr);
            write16(controller.bar0, bar0_rintcnt, 1);
            write8(controller.bar0, bar0_rirbctl,
                   rbctl_dma_en | rbctl_irq_en);
            const hcpp::u8 rirb_sts =
                read8(controller.bar0, bar0_rirbsts) & 0x5;
            if (rirb_sts != 0) {
                write8(controller.bar0, bar0_rirbsts, rirb_sts);
            }
            controller.rirb_rp = 0;
            return true;
        }

        // corb_send_verb + rirb_get_response polling variant —
        // drives one VENDOR_ID parameter read on AFG node 0.
        bool query_vendor(LolaController &controller) noexcept {
            if (controller.corb == nullptr) {
                return false;
            }
            const hcpp::u16 wp =
                static_cast<hcpp::u16>((controller.corb_wp + 1) % corb_entries);
            controller.corb[wp * 2] =
                (0u << 20) | (verb_get_parameter << 8) | par_vendor_id;
            controller.corb[wp * 2 + 1] = 0;
            controller.corb_wp = wp;
            write16(controller.bar0, bar0_corbwp, wp);
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                const hcpp::u16 rp =
                    read16(controller.bar0, bar0_rirbwp) & 0xFF;
                if (rp != controller.rirb_rp) {
                    controller.rirb_rp =
                        static_cast<hcpp::u16>((controller.rirb_rp + 1) %
                                               corb_entries);
                    controller.codec_vendor =
                        controller.rirb[controller.rirb_rp * 2];
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool ensure_buffer(LolaController &controller) noexcept {
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

        void write_hex32(hcpp::u32 value) noexcept {
            for (int shift = 28; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *lola_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_digigram &&
                device.device_id == device_lola) {
                return &device;
            }
        }
        return nullptr;
    }

    bool lola_bind(const pci::PciDevice &device,
                   LolaController &controller) noexcept {
        if (device.vendor_id != vendor_digigram ||
            device.device_id != device_lola) {
            return false;
        }
        controller = {};
        return claim_bars(device, controller);
    }

    bool lola_reset(LolaController &controller) noexcept {
        if (controller.bar0 == 0) {
            return false;
        }
        // reset_controller(): GCTL stays low while reset; writing
        // the RESET bit starts the controller, which then reports
        // GCTL=1 once it is out of reset.
        if ((read32(controller.bar0, bar0_gctl) & gctl_reset) != 0) {
            if (controller.bar1 != 0) {
                write16(controller.bar1, bar1_board_mode, 0);
            }
        } else {
            write32(controller.bar0, bar0_gctl, gctl_reset);
            if (!wait_gctl(controller, gctl_reset)) {
                return false;
            }
        }
        write32(controller.bar0, bar0_intctl, 0);   // no IRQs yet
        if (!setup_corb_rirb(controller)) {
            return false;
        }
        // First verb: VENDOR_ID on the AFG node — proves the verb
        // path end to end (expect 0x1369xxxx).
        query_vendor(controller);
        if (controller.bar1 != 0) {
            controller.board_version = read32(controller.bar1, bar1_dever);
        }
        ensure_buffer(controller);
        // DSD stream descriptors + widget-tree walk are the
        // documented gap — engine stays offline.
        controller.reset_done = true;
        return true;
    }

    bool lola_present() noexcept { return present; }

    bool lola_probe() noexcept {
        const pci::PciDevice *device = lola_find();
        if (device == nullptr) {
            serial::write_line("audiopci lola=absent");
            return false;
        }
        if (!lola_bind(*device, active) || !lola_reset(active)) {
            serial::write_line("audiopci lola=fail");
            return false;
        }
        present = true;
        serial::write("audiopci lola=ok codec=");
        write_hex32(active.codec_vendor);
        serial::write("\r\n");
        audiopci_register({AudioChip::DigigramLola, "Digigram LoLa",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.bar0, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::lola
