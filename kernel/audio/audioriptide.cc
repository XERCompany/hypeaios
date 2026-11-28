#include "kernel/audio/audioriptide.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiow2.h"
#include "kernel/serial.hpp"

namespace hype::audio::riptide {
    namespace {

        RiptideController active{};
        bool present;

        hcpp::u32 in32(const RiptideController &c, hcpp::u16 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(c.iobase + reg));
        }

        void out32(const RiptideController &c, hcpp::u16 reg,
                   hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.iobase + reg), value);
        }

        void set_control(const RiptideController &c, hcpp::u32 bits) noexcept {
            out32(c, audio_control, in32(c, audio_control) | bits);
        }

        void clear_control(const RiptideController &c, hcpp::u32 bits) noexcept {
            out32(c, audio_control, in32(c, audio_control) & ~bits);
        }

        bool is_ready(const RiptideController &c) noexcept {
            return (in32(c, audio_status) & as_ready) != 0;
        }

        bool wait_ready(const RiptideController &c) noexcept {
            for (hcpp::u32 i = 0; i < cmdif_timeout; ++i) {
                if (is_ready(c)) {
                    return true;
                }
                w2::settle(0x40);
            }
            return false;
        }

        // Command-port access: index 0 uses registers at 0x10.., index 1
        // at 0x30.. (each cmdport is 0x20 wide with 5 pad dwords).
        hcpp::u16 port_reg(hcpp::u8 port, hcpp::u16 field) noexcept {
            // field is the offset of data1 inside the port (0, 4, 8).
            return static_cast<hcpp::u16>((port == 0 ? cmdport0_data1
                                                    : cmdport1_data1) +
                                          field);
        }

        // sendcmd() from ALSA riptide.c, bounded: writes cmd to a free
        // command port, optionally sends a parameter and waits for a
        // 64-bit response.  Returns false on timeout.
        bool sendcmd(RiptideController &c, hcpp::u32 flags, hcpp::u32 cmd,
                     hcpp::u32 parm, hcpp::u32 *ret) noexcept {
            if (ret != nullptr) {
                ret[0] = 0;
                ret[1] = 0;
            }
            if (!wait_ready(c)) {
                return false;
            }
            for (hcpp::u32 j = 0; j < cmdif_timeout; ++j) {
                const hcpp::u8 port = static_cast<hcpp::u8>(j % 2);
                const hcpp::u16 data1 = port_reg(port, 0x0);
                const hcpp::u16 data2 = port_reg(port, 0x4);
                const hcpp::u16 stat = port_reg(port, 0x8);
                if ((in32(c, stat) & stat_datf) != 0) {
                    in32(c, data1);  // free pending response data
                    in32(c, data2);
                }
                if ((in32(c, stat) & stat_cmde) != 0) {
                    if ((flags & flag_parm) != 0) {
                        out32(c, data2, parm);
                    }
                    out32(c, data1, cmd);
                    if ((flags & flag_resp) != 0 && ret != nullptr) {
                        hcpp::u32 wait = 0;
                        while ((in32(c, stat) & stat_datf) == 0 &&
                               wait < cmdif_timeout) {
                            ++wait;
                            w2::settle(0x40);
                        }
                        if (wait >= cmdif_timeout) {
                            return false;
                        }
                        ret[0] = in32(c, data1);
                        ret[1] = in32(c, data2);
                    }
                    return true;
                }
                w2::settle(0x80);
            }
            return false;
        }

    }  // namespace

    const pci::PciDevice *riptide_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_4310, device_4320, device_4330,
                                     device_4340};
        return w2::find_ids(vendor_conexant, ids, 4);
    }

    bool riptide_bind(const pci::PciDevice &device,
                      RiptideController &controller) noexcept {
        controller = {};
        if (!w2::enable(device, true, false)) {
            return false;
        }
        controller.iobase = w2::io_base(device, 0);
        return controller.iobase != 0;
    }

    bool riptide_reset(RiptideController &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        // riptide_reset(): toggle global reset, then wait for the ARM to
        // report ready with no global error flag.
        for (hcpp::u32 tries = 0; tries < reset_tries && !controller.is_reset;
             ++tries) {
            clear_control(controller, ac_aie | ac_empuirq);
            set_control(controller, ac_greset);
            w2::settle(w2::settle_spins >> 2);
            clear_control(controller, ac_greset);
            w2::settle(w2::settle_spins >> 2);
            for (hcpp::u32 i = 0; i < cmdif_timeout; ++i) {
                const hcpp::u32 status = in32(controller, audio_status);
                if ((status & as_ready) != 0 && (status & as_gerr) == 0) {
                    controller.is_reset = true;
                    break;
                }
                w2::settle(0x80);
            }
        }
        return controller.is_reset;
    }

    bool riptide_get_version(RiptideController &controller) noexcept {
        if (!controller.is_reset) {
            return false;
        }
        hcpp::u32 ret[2];
        if (!sendcmd(controller, flag_resp, cmd_getv, 0, ret)) {
            return false;
        }
        controller.fw.asic = static_cast<hcpp::u16>(ret[0] & 0xFFFF);
        controller.fw.codec = static_cast<hcpp::u16>((ret[0] >> 16) & 0xFFFF);
        controller.fw.auxdsp = static_cast<hcpp::u16>(ret[1] & 0xFFFF);
        controller.fw.prog = static_cast<hcpp::u16>((ret[1] >> 16) & 0xFFFF);
        return true;
    }

    bool riptide_firmware(RiptideController &controller, const void *image,
                          hcpp::u32 bytes) noexcept {
        if (!controller.is_reset || image == nullptr || bytes < 4) {
            return false;
        }
        // Minimal loader mirroring riptide.c's senddata(): SMEM sets the
        // download address, WMEM streams dwords nibble-swapped (the ARM
        // expects the "little-endian hex" order used by riptide.hex
        // records), then GOTO jumps to the entry point.  The scratch
        // blob is treated as a raw image whose first dword is the load
        // base / entry, followed by the image words; upstream ships the
        // equivalent as Intel-hex text in riptide.hex.
        const auto *words = static_cast<const hcpp::u32 *>(image);
        const hcpp::u32 count = bytes / 4;
        const hcpp::u32 base = words[0];
        if (!sendcmd(controller, flag_parm, cmd_smem, base, nullptr)) {
            return false;
        }
        for (hcpp::u32 i = 1; i < count; ++i) {
            const hcpp::u32 data = words[i];
            const hcpp::u32 swapped =
                ((data & 0x0F0F0F0Fu) << 4) | ((data & 0xF0F0F0F0u) >> 4);
            if (!sendcmd(controller, flag_parm, cmd_wmem | (2 << 8),
                         swapped, nullptr)) {
                return false;
            }
        }
        if (!sendcmd(controller, flag_parm, cmd_goto, base, nullptr)) {
            return false;
        }
        controller.fw_loaded = true;
        return true;
    }

    bool riptide_present() noexcept { return present; }

    bool riptide_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = riptide_find();
        if (device == nullptr) {
            serial::write_line("audiopci riptide=absent");
            return true;
        }
        if (!riptide_bind(*device, active) || !riptide_reset(active)) {
            serial::write_line("audiopci riptide=fail");
            return false;
        }
        if (!riptide_get_version(active)) {
            serial::write_line("audiopci riptide=fail");
            return false;
        }
        serial::write("audiopci riptide fwver=");
        w2::hex16(active.fw.asic);
        serial::write(".");
        w2::hex16(active.fw.codec);
        serial::write(".");
        w2::hex16(active.fw.auxdsp);
        serial::write(".");
        w2::hex16(active.fw.prog);
        serial::write("\r\n");
        void *image = w2::firmware_scratch();
        hcpp::u32 fw_bytes = 0;
        if (!w2::firmware_load("riptide", image,
                               w2::firmware_scratch_bytes, &fw_bytes)) {
            serial::write_line("audiopci riptide fw=missing");
            serial::write_line("audiopci riptide=parked");
            return true;
        }
        const bool loaded = riptide_firmware(active, image, fw_bytes);
        serial::write("audiopci riptide fw=");
        serial::write_line(loaded ? "ok" : "fail");
        if (!loaded) {
            serial::write_line("audiopci riptide=parked");
            return true;
        }
        present = true;
        serial::write_line("audiopci riptide=ok");
        return true;
    }

}  // namespace hype::audio::riptide
