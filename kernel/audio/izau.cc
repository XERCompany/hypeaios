#include "kernel/audio/izau.h"

#include "kernel/audio/audiopcicard.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/pci/pci.h"

namespace hype::audio::hda {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x4000000;
        constexpr hcpp::u32 settle_spins = 0x200000;
        constexpr hcpp::u16 pci_command_reg = 0x04;
        constexpr hcpp::u32 pci_cmd_memory_space = 0x2;
        constexpr hcpp::u32 pci_cmd_bus_master = 0x4;

        volatile hcpp::u8 &reg8(IzauController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u8 *>(controller.mmio + offset);
        }

        volatile hcpp::u16 &reg16(IzauController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u16 *>(controller.mmio + offset);
        }

        volatile hcpp::u32 &reg32(IzauController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(controller.mmio + offset);
        }

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        bool wait_gctl(IzauController &controller, hcpp::u32 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg32(controller, reg_gctl) & gctl_reset) == expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_corbrp(IzauController &controller, hcpp::u16 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg16(controller, reg_corbrp) & corbrp_reset) == expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_rirb_response(IzauController &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg16(controller, reg_rirbwp) & 0xFF) != controller.rirb_wp) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_statests(IzauController &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg16(controller, reg_statests) & 0x7FFF) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        void zero_page(hcpp::uintptr_t address) noexcept {
            auto *bytes = reinterpret_cast<volatile hcpp::u64 *>(address);
            for (hcpp::size_t i = 0; i < memory::page_size / sizeof(hcpp::u64); ++i) {
                bytes[i] = 0;
            }
        }

        bool setup_ring(IzauController &controller, hcpp::u16 lbase, hcpp::u16 ubase, hcpp::u16 size_reg,
                        hcpp::u32 phys) noexcept {
            reg32(controller, lbase) = phys;
            reg32(controller, ubase) = 0;
            reg8(controller, size_reg) = ring_size_256;
            return (reg8(controller, size_reg) & 0x3) == ring_size_256;
        }

        bool setup_corb(IzauController &controller) noexcept {
            reg8(controller, reg_corbctl) = 0;
            if (!setup_ring(controller, reg_corblbase, reg_corbubase, reg_corbsize, controller.corb_phys)) {
                return false;
            }
            reg16(controller, reg_corbwp) = 0;
            reg16(controller, reg_corbrp) = corbrp_reset;
            if (!wait_corbrp(controller, corbrp_reset)) {
                return false;
            }
            reg16(controller, reg_corbrp) = 0;
            if (!wait_corbrp(controller, 0)) {
                return false;
            }
            reg8(controller, reg_corbctl) = corbctl_run;
            return (reg8(controller, reg_corbctl) & corbctl_run) != 0;
        }

        bool setup_rirb(IzauController &controller) noexcept {
            reg8(controller, reg_rirbctl) = 0;
            if (!setup_ring(controller, reg_rirblbase, reg_rirbubase, reg_rirbsize, controller.rirb_phys)) {
                return false;
            }
            reg16(controller, reg_rirbwp) = rirbwp_reset;
            reg16(controller, reg_rintcnt) = 1;
            controller.rirb_wp = 0;
            reg8(controller, reg_rirbctl) = rirbctl_run;
            return (reg8(controller, reg_rirbctl) & rirbctl_run) != 0;
        }

        hcpp::uintptr_t card_mmio_base(const AudioCard &card) noexcept {
            const hcpp::u32 low = pci::config_read32(card.bus_number, card.slot, card.function, 0x10);
            if ((low & 0x1) != 0) {
                return 0;
            }
            hcpp::uintptr_t base = low & ~hcpp::u32(0xF);
            if ((low & 0x6) == 0x4) {
                const hcpp::u32 high = pci::config_read32(card.bus_number, card.slot, card.function, 0x14);
                base |= static_cast<hcpp::uintptr_t>(high) << 32;
            }
            return base;
        }

    }  // namespace

    bool izau_bind(hcpp::size_t card_index, IzauController &controller) noexcept {
        if (card_index >= audio_chip_count()) {
            return false;
        }
        const AudioCard &card = audio_cards()[card_index];
        if (card.chip != AudioChip::IntelHda || card.bus != AudioBus::Pci) {
            return false;
        }
        const hcpp::u32 command = pci::config_read32(card.bus_number, card.slot, card.function, pci_command_reg);
        pci::config_write32(card.bus_number, card.slot, card.function, pci_command_reg,
                            (command & 0xFFFF) | pci_cmd_memory_space | pci_cmd_bus_master);
        const hcpp::uintptr_t base = card_mmio_base(card);
        if (base == 0 || base >= 0x100000000ull) {
            return false;
        }
        if (memory::vmm_initialized() && memory::vmm_translate(base) != base) {
            return false;
        }
        controller.mmio = base;
        controller.card_index = static_cast<hcpp::u8>(card_index);
        controller.codec_mask = 0;
        controller.reset_done = false;
        controller.corb_phys = 0;
        controller.rirb_phys = 0;
        controller.corb = nullptr;
        controller.rirb = nullptr;
        controller.rirb_wp = 0;
        return true;
    }

    bool izau_reset(IzauController &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        reg32(controller, reg_gctl) = reg32(controller, reg_gctl) & ~gctl_reset;
        if (!wait_gctl(controller, 0)) {
            return false;
        }
        settle(settle_spins);
        reg32(controller, reg_gctl) = reg32(controller, reg_gctl) | gctl_reset;
        if (!wait_gctl(controller, gctl_reset)) {
            return false;
        }
        wait_statests(controller);
        controller.codec_mask = static_cast<hcpp::u8>(reg16(controller, reg_statests) & 0x7FFF);
        reg16(controller, reg_statests) = 0x7FFF;
        reg16(controller, reg_wakeen) = 0x7FFF;
        controller.corb_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
        controller.rirb_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
        if (controller.corb_phys == 0 || controller.rirb_phys == 0) {
            return false;
        }
        zero_page(controller.corb_phys);
        zero_page(controller.rirb_phys);
        controller.corb = reinterpret_cast<volatile hcpp::u32 *>(controller.corb_phys);
        controller.rirb = reinterpret_cast<volatile hcpp::u64 *>(controller.rirb_phys);
        if (!setup_corb(controller) || !setup_rirb(controller)) {
            return false;
        }
        controller.reset_done = true;
        return true;
    }

    bool izau_codec_verb(IzauController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept {
        if (!controller.reset_done || controller.corb == nullptr || controller.rirb == nullptr) {
            return false;
        }
        const hcpp::u16 wp = reg16(controller, reg_corbwp) & 0xFF;
        const hcpp::u16 next = static_cast<hcpp::u16>((wp + 1) & (ring_entries - 1));
        controller.corb[next] = (static_cast<hcpp::u32>(codec & 0xF) << 28) |
                                (static_cast<hcpp::u32>(node) << 20) | ((verb & 0xFFF) << 8) | payload;
        asm volatile("" ::: "memory");
        reg16(controller, reg_corbwp) = next;
        if (!wait_rirb_response(controller)) {
            return false;
        }
        const hcpp::u16 rwp = reg16(controller, reg_rirbwp) & 0xFF;
        controller.rirb_wp = rwp;
        const hcpp::u64 entry = controller.rirb[rwp];
        response = static_cast<hcpp::u32>(entry);
        return true;
    }

    hcpp::u8 izau_scan_codecs(IzauController &controller, IzauCodec *codecs, hcpp::u8 max) noexcept {
        if (codecs == nullptr || max == 0 || !controller.reset_done) {
            return 0;
        }
        const hcpp::u16 mask = controller.codec_mask != 0 ? controller.codec_mask : 0x7FFF;
        hcpp::u8 count = 0;
        for (hcpp::u8 address = 0; address < max_codecs && count < max; ++address) {
            if ((mask & (1u << address)) == 0) {
                continue;
            }
            hcpp::u32 response = 0;
            if (!izau_codec_verb(controller, address, 0, verb_get_parameter, param_vendor_id, response)) {
                continue;
            }
            if (response == 0 || response == 0xFFFFFFFF) {
                continue;
            }
            codecs[count].address = address;
            codecs[count].vendor = response;
            ++count;
        }
        return count;
    }

}  // namespace hype::audio::hda
