#include "kernel/audio/audiolx6464es.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::lx6464es {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x80000;

        Lx6464esController active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        volatile hcpp::u32 &reg(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(base + offset);
        }

        bool is_lx6464es_subsystem(hcpp::u16 subvendor, hcpp::u16 subsystem) noexcept {
            if (subvendor != subvendor_digigram) {
                return false;
            }
            return subsystem == sub_lx6464es ||
                   subsystem == sub_lx6464es_cae ||
                   subsystem == sub_lx6464ese ||
                   subsystem == sub_lx6464ese_cae;
        }

        bool is_lx6464es(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != vendor_plx ||
                device.device_id != device_plx9056) {
                return false;
            }
            const hcpp::u32 sub =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_subsystem_id_reg);
            return is_lx6464es_subsystem(static_cast<hcpp::u16>(sub & 0xFFFF),
                                         static_cast<hcpp::u16>(sub >> 16));
        }

        hcpp::uintptr_t mem_bar(const pci::PciDevice &device, hcpp::u8 index) noexcept {
            const hcpp::u32 raw = pci::bar(device, index);
            if ((raw & 0x1) != 0 || raw == 0) {
                return 0;
            }
            const hcpp::uintptr_t base = raw & ~hcpp::u32(0xF);
            if (memory::vmm_initialized() && memory::vmm_translate(base) != base) {
                return 0;
            }
            return base;
        }

        bool claim_bars(const pci::PciDevice &device,
                        Lx6464esController &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space |
                                    pci_cmd_memory_space |
                                    pci_cmd_bus_master);
            // BAR1 = PLX local regs (usually a memory BAR on the
            // 9056), BAR2 = DSP register window.
            controller.plx = mem_bar(device, 1);
            controller.dsp = mem_bar(device, 2);
            return controller.plx != 0 && controller.dsp != 0;
        }

        // lx_init_xilinx_reset(): assert Xilinx reset in CHIPSC,
        // clear MBOX3, deassert, then wait for the card to post a
        // non-zero MBOX3 "reset done" flag.
        bool xilinx_reset(Lx6464esController &controller) noexcept {
            hcpp::u32 chipsc = reg(controller.plx, plx_chipsc);
            reg(controller.plx, plx_chipsc) =
                chipsc & ~chipsc_reset_xilinx;
            settle(settle_spins);
            reg(controller.plx, plx_mbox3) = 0;
            settle(settle_spins);
            chipsc |= chipsc_reset_xilinx;
            reg(controller.plx, plx_chipsc) = chipsc;
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if (reg(controller.plx, plx_mbox3) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        // lx_init_xilinx_test(): CSM must read 0; if not, remap
        // SPACE0 via PCICR and try once more.
        bool xilinx_test(Lx6464esController &controller) noexcept {
            reg(controller.dsp, dsp_csm) = 0;
            if (reg(controller.dsp, dsp_csm) == 0) {
                return true;
            }
            reg(controller.plx, plx_pcicr) = pcicr_space0_remap;
            return reg(controller.dsp, dsp_csm) == 0;
        }

        bool ensure_buffer(Lx6464esController &controller) noexcept {
            if (controller.buffer_phys != 0) {
                return true;
            }
            controller.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(4));
            if (controller.buffer_phys == 0) {
                return false;
            }
            controller.buffer_bytes = memory::page_size * 4;
            return true;
        }

    }  // namespace

    const pci::PciDevice *lx6464es_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_lx6464es(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    bool lx6464es_bind(const pci::PciDevice &device,
                       Lx6464esController &controller) noexcept {
        if (!is_lx6464es(device)) {
            return false;
        }
        controller = {};
        const hcpp::u32 sub =
            pci::config_read32(device.bus, device.slot, device.function,
                               pci_subsystem_id_reg);
        controller.subsystem = static_cast<hcpp::u16>(sub >> 16);
        return claim_bars(device, controller);
    }

    bool lx6464es_reset(Lx6464esController &controller) noexcept {
        if (controller.plx == 0 || controller.dsp == 0) {
            return false;
        }
        // Documented boot sequence: Xilinx reset then the
        // MicroBlaze access test. The RMH command channel
        // (CRM1-12 + CSM MC/MR handshake) and EtherSound config
        // (CONFES/CSES) come after; those stay offline this wave.
        const bool reset_ok = xilinx_reset(controller);
        const bool access_ok = xilinx_test(controller);
        controller.reset_done = reset_ok && access_ok;
        if (!controller.reset_done) {
            return false;
        }
        ensure_buffer(controller);
        return true;
    }

    bool lx6464es_present() noexcept { return present; }

    bool lx6464es_probe() noexcept {
        const pci::PciDevice *device = lx6464es_find();
        if (device == nullptr) {
            serial::write_line("audiopci lx6464es=absent");
            return false;
        }
        if (!lx6464es_bind(*device, active) || !lx6464es_reset(active)) {
            serial::write_line("audiopci lx6464es=fail");
            return false;
        }
        present = true;
        serial::write_line("audiopci lx6464es=ok");
        audiopci_register({AudioChip::DigigramLx6464es,
                           "Digigram LX6464ES",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.plx, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::lx6464es
