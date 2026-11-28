#include "kernel/audio/audiomixart.h"

#include "kernel/audio/audiow2.h"
#include "kernel/serial.hpp"

namespace hype::audio::mixart {
    namespace {

        constexpr hcpp::u32 fw_capacity = w2::firmware_scratch_bytes;

        MixartController active{};
        bool present;

        hcpp::u32 bswap32(hcpp::u32 value) noexcept {
            return ((value & 0x000000FFu) << 24) |
                   ((value & 0x0000FF00u) << 8) |
                   ((value & 0x00FF0000u) >> 8) |
                   ((value & 0xFF000000u) >> 24);
        }

        volatile hcpp::u32 &mem32(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(base + offset);
        }

        hcpp::u32 read_be(hcpp::uintptr_t base, hcpp::u32 offset) noexcept {
            return bswap32(mem32(base, offset));
        }

        void write_be(hcpp::uintptr_t base, hcpp::u32 offset,
                      hcpp::u32 value) noexcept {
            mem32(base, offset) = bswap32(value);
        }

        bool wait_be(hcpp::uintptr_t base, hcpp::u32 offset, hcpp::u32 want,
                     hcpp::u32 spin_rounds) noexcept {
            for (hcpp::u32 round = 0; round < spin_rounds; ++round) {
                if (read_be(base, offset) == want) {
                    return true;
                }
                w2::settle(w2::settle_spins >> 2);
            }
            return false;
        }

    }  // namespace

    const pci::PciDevice *mixart_find() noexcept {
        constexpr hcpp::u16 motorola_ids[] = {device_mpc8240};
        if (const pci::PciDevice *device =
                w2::find_ids(vendor_motorola, motorola_ids, 1)) {
            return device;
        }
        constexpr hcpp::u16 digigram_ids[] = {device_mixart8};
        return w2::find_ids(vendor_digigram, digigram_ids, 1);
    }

    bool mixart_bind(const pci::PciDevice &device,
                     MixartController &controller) noexcept {
        controller = {};
        if (!w2::enable(device, false, true)) {
            return false;
        }
        controller.mem0 = w2::mem_base(device, 0);
        controller.regs = w2::mem_base(device, 1);
        return controller.mem0 != 0 && controller.regs != 0;
    }

    bool mixart_reset(MixartController &controller) noexcept {
        if (controller.regs == 0 || controller.mem0 == 0) {
            return false;
        }
        // Hard board reset: LSBit of the BA1 brutal-reset register.
        write_be(controller.regs, reg_brutal_reset, 1);
        w2::settle(w2::settle_spins);

        // Mask every host interrupt and drain pending doorbells, same
        // order ALSA uses on the first interrupt/mailbox init.
        mem32(controller.regs, reg_omimr) = omimr_mask_all;
        const hcpp::u32 doorbell = mem32(controller.regs, reg_odbr);
        mem32(controller.regs, reg_odbr) = doorbell;
        mem32(controller.regs, reg_omisr) = omisr_oidi;

        // Clear the mailbox resource-protection dwords.
        mem32(controller.mem0, msg_host_rsc_protection) = 0;
        mem32(controller.mem0, msg_agent_rsc_protection) = 0;
        write_be(controller.mem0, pr_flowtable_ptr, 0);

        // Sample the embedded firmware pseudo-registers (big-endian).
        controller.mxlx_status = read_be(controller.mem0, pr_mxlx_status);
        const hcpp::u32 elf_status = read_be(controller.mem0, pr_elf_status);
        serial::write("mixart mxlx=0x");
        w2::hex32(controller.mxlx_status);
        serial::write(" elf=0x");
        w2::hex32(elf_status);
        serial::write("\r\n");
        controller.reset_done = true;
        return true;
    }

    bool mixart_firmware(MixartController &controller, const void *image,
                         hcpp::u32 bytes) noexcept {
        if (!controller.reset_done || image == nullptr || bytes == 0 ||
            bytes > ba0_size - mxlx_base_address) {
            return false;
        }
        // Motherboard Xilinx upload: image is dropped at 0x600000 inside
        // BAR0 and the mailbox pseudoregisters are armed (all BE).
        write_be(controller.mem0, pr_mxlx_status, stat_xfer_wait);
        write_be(controller.mem0, pr_mxlx_base_addr, mxlx_base_address);
        write_be(controller.mem0, pr_mxlx_size, bytes);

        auto *dst = reinterpret_cast<volatile hcpp::u8 *>(controller.mem0 +
                                                        mxlx_base_address);
        const auto *src = static_cast<const hcpp::u8 *>(image);
        for (hcpp::u32 i = 0; i < bytes; ++i) {
            dst[i] = src[i];
        }
        write_be(controller.mem0, pr_mxlx_status, stat_xfer_done);
        // Bounded wait for the embedded bootstrap to consume the image.
        const bool ok = wait_be(controller.mem0, pr_mxlx_status,
                                stat_xfer_wait, 32);
        write_be(controller.mem0, pr_boardnumber, 0);
        controller.fw_loaded = ok;
        return ok;
    }

    bool mixart_present() noexcept { return present; }

    bool mixart_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = mixart_find();
        if (device == nullptr) {
            serial::write_line("audiopci mixart=absent");
            return true;
        }
        serial::write("audiopci mixart pci=");
        w2::hex16(device->vendor_id);
        serial::write_char(':');
        w2::hex16(device->device_id);
        serial::write("\r\n");
        if (!mixart_bind(*device, active) || !mixart_reset(active)) {
            serial::write_line("audiopci mixart=fail");
            return false;
        }
        // Firmware path: the board is dead without its Xilinx image and
        // ELF boot code; both are packed into the wave-2 fw hook.
        void *image = w2::firmware_scratch();
        hcpp::u32 fw_bytes = 0;
        if (!w2::firmware_load("mixart", image, fw_capacity, &fw_bytes)) {
            serial::write_line("audiopci mixart fw=missing");
            serial::write_line("audiopci mixart=parked");
            return true;
        }
        const bool loaded = mixart_firmware(active, image, fw_bytes);
        serial::write("audiopci mixart fw=");
        serial::write_line(loaded ? "ok" : "fail");
        if (!loaded) {
            serial::write_line("audiopci mixart=parked");
            return true;
        }
        present = true;
        serial::write_line("audiopci mixart=ok");
        return true;
    }

}  // namespace hype::audio::mixart
