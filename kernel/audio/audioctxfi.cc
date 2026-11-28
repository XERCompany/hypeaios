#include "kernel/audio/audioctxfi.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::ctxfi {
    namespace {

        constexpr hcpp::u32 settle_spins = 0x80000;
        constexpr hcpp::u32 autoinit_spins = 0x400000;

        CtxfiController active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        volatile hcpp::u32 &reg(const CtxfiController &controller,
                                hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(
                controller.mmio + offset);
        }

        bool is_ctxfi(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != vendor_creative) {
                return false;
            }
            return device.device_id == device_20k1 ||
                   device.device_id == device_20k2;
        }

        hcpp::uintptr_t bar_base(const pci::PciDevice &device, hcpp::u8 index) noexcept {
            const hcpp::u32 raw = pci::bar(device, index);
            if ((raw & 0x1) != 0 || raw == 0) {
                return 0;
            }
            hcpp::uintptr_t base = raw & ~hcpp::u32(0xF);
            if ((raw & 0x6) == 0x4) {
                // 64-bit BAR: the register spaces we care about live
                // below 4 GiB on real boards; reject otherwise.
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

        bool claim_window(const pci::PciDevice &device,
                          CtxfiController &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_memory_space |
                                    pci_cmd_bus_master);
            // 20k2 keeps its register window in BAR2, 20k1 in BAR0
            // (ALSA hw_card_start for both chips).
            controller.mmio = bar_base(device, controller.is20k2 ? 2 : 0);
            return controller.mmio != 0;
        }

        bool pll_init_20k1(const CtxfiController &controller) noexcept {
            // hw_pll_init(): write PLLCTL, allow lock, verify.
            for (hcpp::u32 attempt = 0; attempt < 3; ++attempt) {
                reg(controller, r1_pllctl) = r1_pll_48k;
                settle(settle_spins << 1);
                if (reg(controller, r1_pllctl) == r1_pll_48k) {
                    return true;
                }
            }
            return false;
        }

        bool auto_init_20k1(const CtxfiController &controller) noexcept {
            hcpp::u32 gctl = reg(controller, r1_gctl);
            reg(controller, r1_gctl) = gctl & ~r1_gctl_eai;
            reg(controller, r1_gctl) = gctl | r1_gctl_eai;
            settle(settle_spins);
            for (hcpp::u32 i = 0; i < autoinit_spins; ++i) {
                if ((reg(controller, r1_gctl) & r1_gctl_aid) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool pll_init_20k2(const CtxfiController &controller) noexcept {
            reg(controller, r2_pll_enb) = r2_pll_enb_all;
            reg(controller, r2_pll_ctl) = r2_pll_step1;
            settle(settle_spins << 1);
            reg(controller, r2_pll_ctl) = r2_pll_step2;
            settle(settle_spins << 1);
            for (hcpp::u32 i = 0; i < 1000; ++i) {
                if ((reg(controller, r2_pll_stat) & r2_pllstat_pd) == 0) {
                    return true;
                }
                settle(settle_spins >> 3);
            }
            return false;
        }

        bool auto_init_20k2(const CtxfiController &controller) noexcept {
            const hcpp::u32 gctl = reg(controller, r2_gctl);
            reg(controller, r2_gctl) = gctl & ~r2_gctl_aie;
            reg(controller, r2_gctl) = gctl | r2_gctl_aie;
            settle(settle_spins);
            for (hcpp::u32 i = 0; i < autoinit_spins; ++i) {
                if ((reg(controller, r2_gctl) & r2_gctl_aid) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool reset_20k1(CtxfiController &controller) noexcept {
            if (!pll_init_20k1(controller) ||
                !auto_init_20k1(controller)) {
                return false;
            }
            // Enable audio-ring paths (hw_card_init).
            hcpp::u32 gctl = reg(controller, r1_gctl);
            gctl |= r1_gctl_eac | r1_gctl_dbp | r1_gctl_tbp |
                    r1_gctl_fbp | r1_gctl_et;
            reg(controller, r1_gctl) = gctl;
            settle(settle_spins);
            // Quiesce global + SRC pending interrupts.
            reg(controller, r1_gie) = 0;
            reg(controller, r1_srcip) = 0;
            settle(settle_spins);
            // Generic GPIO default used for unknown card ids.
            reg(controller, r1_gpioctl) = 0x01e6;
            return true;
        }

        bool reset_20k2(CtxfiController &controller) noexcept {
            // Switch out of UAA mode into 20k2 native mode first.
            reg(controller, r2_gctl) =
                reg(controller, r2_gctl) & ~r2_gctl_uaa;
            if (!pll_init_20k2(controller) ||
                !auto_init_20k2(controller)) {
                return false;
            }
            hcpp::u32 gctl = reg(controller, r2_gctl);
            gctl = (gctl | r2_gctl_dbp | r2_gctl_tbp | r2_gctl_fbp) &
                   ~r2_gctl_dpc;
            reg(controller, r2_gctl) = gctl;
            reg(controller, r2_gie) = 0;
            reg(controller, r2_src_ip) = 0;
            settle(settle_spins);
            reg(controller, r2_gpio_ctrl) = 0xd802;  // generic SB0880-ish
            reg(controller, r2_mixer_ar_enable) = 0x01;
            return true;
        }

        bool ensure_buffer(CtxfiController &controller) noexcept {
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

    const pci::PciDevice *ctxfi_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_ctxfi(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    bool ctxfi_bind(const pci::PciDevice &device,
                    CtxfiController &controller) noexcept {
        if (!is_ctxfi(device)) {
            return false;
        }
        controller = {};
        controller.device_id = device.device_id;
        controller.is20k2 = device.device_id == device_20k2;
        return claim_window(device, controller);
    }

    bool ctxfi_reset(CtxfiController &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        const bool ok = controller.is20k2 ? reset_20k2(controller)
                                        : reset_20k1(controller);
        if (!ok) {
            return false;
        }
        ensure_buffer(controller);
        // VMEM page-table + transport-ring programming (hw_trn_init)
        // and SRC/AMPTP resource init are not brought up yet —
        // engine stays offline.
        controller.reset_done = true;
        return true;
    }

    bool ctxfi_present() noexcept { return present; }

    bool ctxfi_probe() noexcept {
        const pci::PciDevice *device = ctxfi_find();
        if (device == nullptr) {
            serial::write_line("audiopci ctxfi=absent");
            return false;
        }
        if (!ctxfi_bind(*device, active) || !ctxfi_reset(active)) {
            serial::write_line("audiopci ctxfi=fail");
            return false;
        }
        present = true;
        serial::write_line("audiopci ctxfi=ok");
        audiopci_register({AudioChip::CreativeCtxfi,
                           active.is20k2 ? "Creative X-Fi EMU20k2"
                                         : "Creative X-Fi EMU20k1",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.mmio, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::ctxfi
