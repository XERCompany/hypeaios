#include "kernel/audio/audiovx222.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiow2.h"
#include "kernel/serial.hpp"

namespace hype::audio::vx222 {
    namespace {

        Vx222Controller active{};
        bool present;

        hcpp::u32 plx_in(const Vx222Controller &c, hcpp::u16 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(c.plx + reg));
        }

        void plx_out(const Vx222Controller &c, hcpp::u16 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.plx + reg), value);
        }

        void dsp_out(const Vx222Controller &c, hcpp::u16 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.dsp + reg), value);
        }

        bool wait_plx_bit(const Vx222Controller &c, hcpp::u16 reg,
                          hcpp::u32 mask, hcpp::u32 want) noexcept {
            for (hcpp::u32 i = 0; i < poll_tries; ++i) {
                if ((plx_in(c, reg) & mask) == want) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

    }  // namespace

    const pci::PciDevice *vx222_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_plx9030, device_plx9050};
        return w2::find_sub(vendor_plx, ids, 2, subvendor_digigram);
    }

    bool vx222_bind(const pci::PciDevice &device,
                    Vx222Controller &controller) noexcept {
        controller = {};
        controller.revision = device.device_id == device_plx9030
                                  ? Vx222Revision::New
                                  : Vx222Revision::Old;
        if (!w2::enable(device, true, false)) {
            return false;
        }
        // VX222: BAR1 = PLX local registers, BAR2 = DSP register file.
        controller.plx = w2::io_base(device, 1);
        controller.dsp = w2::io_base(device, 2);
        return controller.plx != 0 && controller.dsp != 0;
    }

    bool vx222_reset(Vx222Controller &controller) noexcept {
        if (controller.plx == 0 || controller.dsp == 0) {
            return false;
        }
        // vx_reset: halt the DSP, pulse its reset line through CNTRL,
        // then let the PLX run.  Mirrors vx222_hwdep.c / vx_core.
        hcpp::u32 cntrl = plx_in(controller, plx_chipsc);
        cntrl |= cntrl_hold_dsp;
        plx_out(controller, plx_chipsc, cntrl);
        cntrl |= cntrl_reset_dsp;
        plx_out(controller, plx_chipsc, cntrl);
        w2::settle(w2::settle_spins >> 2);
        cntrl &= ~cntrl_reset_dsp;
        plx_out(controller, plx_chipsc, cntrl);
        w2::settle(w2::settle_spins >> 2);

        // Clear DSP interrupt state and the PLX mailboxes.
        dsp_out(controller, dsp_csr, csr_reset);
        w2::settle(w2::settle_spins >> 3);
        dsp_out(controller, dsp_csr, 0);
        plx_out(controller, plx_mbox0, 0);
        plx_out(controller, plx_mbox1, 0);

        controller.xilinx_done =
            (plx_in(controller, plx_chipsc) & xilinx_init) != 0;
        controller.boot_here =
            (plx_in(controller, plx_mbox0) & mbox_boot_here) != 0;
        controller.reset_done = true;
        return true;
    }

    bool vx222_load_xilinx(Vx222Controller &controller, const void *image,
                           hcpp::u32 bytes) noexcept {
        if (!controller.reset_done || image == nullptr || bytes == 0) {
            return false;
        }
        // Serial bitstream clocked through CNTRL (vx_load_xilinx_binary):
        // assert PROG, wait for INIT to drop, then clock every bit on
        // DATA_CLK, MSB first, and verify DONE at the end.
        hcpp::u32 cntrl = plx_in(controller, plx_chipsc);
        cntrl &= ~(xilinx_data_clk | xilinx_data_in);
        cntrl |= xilinx_prog;
        plx_out(controller, plx_chipsc, cntrl);
        w2::settle(w2::settle_spins >> 3);
        cntrl &= ~xilinx_prog;
        plx_out(controller, plx_chipsc, cntrl);
        if (!wait_plx_bit(controller, plx_chipsc, xilinx_init, 0)) {
            return false;
        }
        if (!wait_plx_bit(controller, plx_chipsc, xilinx_init,
                          xilinx_init)) {
            return false;
        }

        const auto *data = static_cast<const hcpp::u8 *>(image);
        for (hcpp::u32 i = 0; i < bytes; ++i) {
            hcpp::u32 mask = 0x80;
            while (mask != 0) {
                cntrl &= ~(xilinx_data_clk | xilinx_data_in);
                if ((data[i] & mask) != 0) {
                    cntrl |= xilinx_data_in;
                }
                plx_out(controller, plx_chipsc, cntrl);
                cntrl |= xilinx_data_clk;
                plx_out(controller, plx_chipsc, cntrl);
                mask >>= 1;
            }
        }
        // A few trailing clocks complete the configuration.
        for (hcpp::u32 i = 0; i < 32; ++i) {
            cntrl &= ~xilinx_data_clk;
            plx_out(controller, plx_chipsc, cntrl);
            cntrl |= xilinx_data_clk;
            plx_out(controller, plx_chipsc, cntrl);
        }
        controller.xilinx_done =
            (plx_in(controller, plx_chipsc) & xilinx_init) != 0;
        if (!controller.xilinx_done) {
            return false;
        }
        // Release the DSP and flag boot-here so the core firmware loop
        // (uploaded next by the host path) can hand it work.
        plx_out(controller, plx_chipsc, cntrl & ~cntrl_hold_dsp);
        plx_out(controller, plx_mbox0, mbox_boot_here);
        controller.fw_loaded = true;
        return true;
    }

    bool vx222_present() noexcept { return present; }

    bool vx222_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = vx222_find();
        if (device == nullptr) {
            serial::write_line("audiopci vx222=absent");
            return true;
        }
        serial::write("audiopci vx222 plx=");
        w2::hex16(device->vendor_id);
        serial::write_char(':');
        w2::hex16(device->device_id);
        serial::write(" sub=");
        w2::hex16(w2::subsystem_vendor(*device));
        serial::write("\r\n");
        if (!vx222_bind(*device, active) || !vx222_reset(active)) {
            serial::write_line("audiopci vx222=fail");
            return false;
        }
        serial::write("audiopci vx222 rev=");
        serial::write(active.revision == Vx222Revision::New ? "new" : "old");
        serial::write(" xlx=");
        serial::write(active.xilinx_done ? "yes" : "no");
        serial::write(" boot=");
        serial::write_line(active.boot_here ? "yes" : "no");
        void *image = w2::firmware_scratch();
        hcpp::u32 fw_bytes = 0;
        if (!w2::firmware_load("vx222", image, w2::firmware_scratch_bytes,
                               &fw_bytes)) {
            serial::write_line("audiopci vx222 fw=missing");
            serial::write_line("audiopci vx222=parked");
            return true;
        }
        const bool loaded = vx222_load_xilinx(active, image, fw_bytes);
        serial::write("audiopci vx222 fw=");
        serial::write_line(loaded ? "ok" : "fail");
        if (!loaded) {
            serial::write_line("audiopci vx222=parked");
            return true;
        }
        present = true;
        serial::write_line("audiopci vx222=ok");
        return true;
    }

}  // namespace hype::audio::vx222
