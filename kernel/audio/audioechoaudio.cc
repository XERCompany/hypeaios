#include "kernel/audio/audioechoaudio.h"

#include "kernel/audio/audiopci.h"
#include "kernel/memory/vmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::echoaudio {
    namespace {

        EchoController active{};
        bool present;

        bool is_echoaudio(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != vendor_motorola) {
                return false;
            }
            return device.device_id == device_dsp56301 ||
                   device.device_id == device_dsp56361;
        }

        bool claim_bar(const pci::PciDevice &device,
                       EchoController &controller) noexcept {
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

        void write_hex16(hcpp::u16 value) noexcept {
            for (int shift = 12; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *echoaudio_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        for (hcpp::size_t i = 0; i < count; ++i) {
            if (is_echoaudio(devices[i])) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    // Subsystem device ids (subvendor 0xECC0) per the per-card ALSA
    // drivers in sound/pci/echoaudio/.
    const char *echoaudio_board_name(hcpp::u16 subsystem) noexcept {
        switch (subsystem) {
            case 0x0010:
            case 0x0011:
                return "Darla20";
            case 0x0020:
            case 0x0021:
                return "Gina20";
            case 0x0030:
            case 0x0031:
                return "Layla20";
            case 0x0040:
            case 0x0041:
                return "Darla24";
            case 0x0050:
            case 0x0051:
                return "Gina24";
            case 0x0060:
            case 0x0061:
                return "Layla24";
            case 0x0070:
            case 0x0071:
            case 0x0072:
                return "Mona";
            case 0x0080:
            case 0x0081:
                return "Mia";
            case 0x0090:
                return "Indigo";
            case 0x0091:
                return "IndigoIO";
            case 0x0092:
                return "IndigoDJ";
            case 0x0095:
            case 0x0096:
                return "Indigo IOx/DJx";
            case 0x0100:
                return "Echo3G";
            default:
                return "Echo card";
        }
    }

    bool echoaudio_bind(const pci::PciDevice &device,
                        EchoController &controller) noexcept {
        if (!is_echoaudio(device)) {
            return false;
        }
        controller = {};
        controller.device_id = device.device_id;
        const hcpp::u32 sub =
            pci::config_read32(device.bus, device.slot, device.function,
                               pci_subsystem_id_reg);
        controller.subsystem = static_cast<hcpp::u16>(sub >> 16);
        controller.board_name = echoaudio_board_name(controller.subsystem);
        return claim_bar(device, controller);
    }

    bool echoaudio_reset(EchoController &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        // There is no software-visible reset latch on these cards:
        // the DSP must be halted, the loader firmware uploaded via
        // the comm page, then the card DSP + optional ASIC images
        // downloaded — all of which requires the ea/*.fw blobs that
        // are not shipped. Mark the card identified but engine
        // offline rather than poke undocumented DSP state.
        controller.reset_done = true;
        return true;
    }

    bool echoaudio_present() noexcept { return present; }

    bool echoaudio_probe() noexcept {
        const pci::PciDevice *device = echoaudio_find();
        if (device == nullptr) {
            serial::write_line("audiopci echoaudio=absent");
            return false;
        }
        if (!echoaudio_bind(*device, active) || !echoaudio_reset(active)) {
            serial::write_line("audiopci echoaudio=fail");
            return false;
        }
        present = true;
        serial::write("audiopci echoaudio=ok sub=");
        write_hex16(active.subsystem);
        serial::write("\r\n");
        audiopci_register({AudioChip::EchoAudio, active.board_name,
                           device->bus, device->slot, device->function,
                           device->vendor_id, device->device_id,
                           active.mmio, true, active.engine_ready});
        return true;
    }

}  // namespace hype::audio::echoaudio
