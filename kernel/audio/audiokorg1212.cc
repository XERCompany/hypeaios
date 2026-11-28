#include "kernel/audio/audiokorg1212.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::korg1212 {
    namespace {

        constexpr hcpp::u32 ack_poll_limit = 0x400000;
        constexpr hcpp::u32 settle_spins = 0x20000;

        Korg1212Controller active{};
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        volatile hcpp::u32 &reg(const Korg1212Controller &controller,
                                hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(
                controller.mmio + offset);
        }

        bool claim_bar(const pci::PciDevice &device,
                       Korg1212Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function,
                                pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_memory_space |
                                    pci_cmd_bus_master);
            const hcpp::u32 raw = pci::bar(device, 0);
            if ((raw & 0x1) != 0 || raw == 0) {
                return false;
            }
            const hcpp::uintptr_t base = raw & ~hcpp::u32(0xF);
            if (memory::vmm_initialized() && memory::vmm_translate(base) != base) {
                return false;
            }
            controller.mmio = base;
            return true;
        }

        // snd_korg1212_Send1212Command(): fill all four mailboxes,
        // then ring the host->card doorbell. The card acks by
        // echoing the doorbell byte in mailbox3's low byte.
        bool send_command(Korg1212Controller &controller, hcpp::u8 doorbell,
                          hcpp::u32 mb0, hcpp::u32 mb1, hcpp::u32 mb2) noexcept {
            reg(controller, mailbox3_offset) = 0;
            reg(controller, mailbox2_offset) = mb2;
            reg(controller, mailbox1_offset) = mb1;
            reg(controller, mailbox0_offset) = mb0;
            reg(controller, out_doorbell_offset) = doorbell;
            for (hcpp::u32 i = 0; i < ack_poll_limit; ++i) {
                const hcpp::u32 mb3 = reg(controller, mailbox3_offset);
                if ((mb3 & doorbell_val_mask) ==
                    (doorbell & doorbell_val_mask)) {
                    controller.card_state =
                        static_cast<hcpp::u8>((mb3 >> 8) & 0xFF);
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        void write_hex32(hcpp::u32 value) noexcept {
            for (int shift = 28; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *korg1212_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_plx &&
                device.device_id == device_korg1212) {
                return &device;
            }
        }
        return nullptr;
    }

    bool korg1212_bind(const pci::PciDevice &device,
                       Korg1212Controller &controller) noexcept {
        if (device.vendor_id != vendor_plx ||
            device.device_id != device_korg1212) {
            return false;
        }
        controller = {};
        return claim_bar(device, controller);
    }

    bool korg1212_reset(Korg1212Controller &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        // The card mirrors its PCI ids into the shared window —
        // cheap proof the memory window is live.
        controller.id_mirror = reg(controller, dev_vend_id_offset);
        if (controller.id_mirror == 0 ||
            controller.id_mirror == 0xffffffff) {
            return false;
        }
        // Enable card->host interrupts at the PLX status register
        // (snd_korg1212_EnableCardInterrupts).
        reg(controller, status_reg_offset) =
            reg(controller, status_reg_offset) | pci_int_enable_bit;
        // Clear mailbox3 before commands so a stale ack cannot
        // spoof a reply.
        reg(controller, mailbox3_offset) = 0;
        // Reboot the card DSP from its on-board page-4 image —
        // K1212_DB_BootFromDSPPage4 acks via mailbox3.
        if (!send_command(controller, db_boot_from_dsp_page4, 0, 0, 0)) {
            // A card that is already running may not ack reboots;
            // treat a live id mirror + mailbox as "present" anyway.
            controller.card_state = state_uninitialized;
        }
        settle(settle_spins);
        controller.reset_done = true;
        // Host buffer configuration (DB_ConfigureBufferMemory) and
        // TriggerPlay are not issued — engine offline.
        return true;
    }

    bool korg1212_present() noexcept { return present; }

    bool korg1212_probe() noexcept {
        const pci::PciDevice *device = korg1212_find();
        if (device == nullptr) {
            serial::write_line("audiopci korg1212=absent");
            return false;
        }
        if (!korg1212_bind(*device, active) || !korg1212_reset(active)) {
            serial::write_line("audiopci korg1212=fail");
            return false;
        }
        present = true;
        serial::write("audiopci korg1212=ok id=");
        write_hex32(active.id_mirror);
        serial::write("\r\n");
        audiopci_register({AudioChip::Korg1212, "Korg 1212 I/O",
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.mmio, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::korg1212
