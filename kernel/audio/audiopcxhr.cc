#include "kernel/audio/audiopcxhr.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiow2.h"
#include "kernel/serial.hpp"

namespace hype::audio::pcxhr {
    namespace {

        PcxhrController active{};
        bool present;

        hcpp::u32 plx_in(const PcxhrController &c, hcpp::u16 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(c.plx + reg));
        }

        void plx_out(const PcxhrController &c, hcpp::u16 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(c.plx + reg), value);
        }

        void dsp_out(const PcxhrController &c, hcpp::u16 reg,
                     hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(c.dsp + reg), value);
        }

        void enable_irq(const PcxhrController &c, bool enable) noexcept {
            hcpp::u32 reg = plx_in(c, plx_irqcs);
            if (enable) {
                reg |= irqcs_enable_pciirq | irqcs_enable_pcidb;
            } else {
                reg &= ~(irqcs_enable_pciirq | irqcs_enable_pcidb);
            }
            plx_out(c, plx_irqcs, reg);
        }

    }  // namespace

    const pci::PciDevice *pcxhr_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_plx9656, device_plx9056};
        return w2::find_sub(vendor_plx, ids, 2, subvendor_digigram);
    }

    bool pcxhr_bind(const pci::PciDevice &device,
                    PcxhrController &controller) noexcept {
        controller = {};
        if (!w2::enable(device, true, false)) {
            return false;
        }
        // ALSA uses port[1] (PLX local) and port[2] (DSP HI08).
        controller.plx = w2::io_base(device, 1);
        controller.dsp = w2::io_base(device, 2);
        return controller.plx != 0 && controller.dsp != 0;
    }

    bool pcxhr_reset(PcxhrController &controller) noexcept {
        if (controller.plx == 0 || controller.dsp == 0) {
            return false;
        }
        // pcxhr_reset_dsp: disable IRQs, pulse the DSP reset byte,
        // then clear the mailbox.
        enable_irq(controller, false);
        dsp_out(controller, dsp_reset, 0);
        w2::settle(w2::settle_spins >> 3);  // ~2 ms
        dsp_out(controller, dsp_reset, 3);
        w2::settle(w2::settle_spins >> 3);
        plx_out(controller, plx_mbox0, 0);

        const hcpp::u32 chipsc = plx_in(controller, plx_chipsc);
        // REV01+ boards always report the second Xilinx populated.
        controller.xilinx_present = (chipsc & chipsc_gpi_useri) != 0;
        controller.boot_here =
            (plx_in(controller, plx_mbox0) & mbox0_boot_here) != 0;
        controller.reset_done = true;
        return true;
    }

    bool pcxhr_load_xilinx(PcxhrController &controller, const void *image,
                           hcpp::u32 bytes) noexcept {
        if (!controller.reset_done || image == nullptr || bytes == 0) {
            return false;
        }
        // Serial bitstream upload through the chip-select register:
        // each bit is clocked into the Xilinx via DATA_IN/DATA_CLK.
        hcpp::u32 chipsc = plx_in(controller, plx_chipsc);
        if (!controller.xilinx_present) {
            return false;
        }
        chipsc |= chipsc_reset_xilinx;
        plx_out(controller, plx_chipsc, chipsc);
        w2::settle(w2::settle_spins >> 3);

        const auto *data = static_cast<const hcpp::u8 *>(image);
        for (hcpp::u32 i = 0; i < bytes; ++i) {
            hcpp::u32 mask = 0x80;
            while (mask != 0) {
                chipsc &= ~(chipsc_data_clk | chipsc_data_in);
                if ((data[i] & mask) != 0) {
                    chipsc |= chipsc_data_in;
                }
                plx_out(controller, plx_chipsc, chipsc);
                chipsc |= chipsc_data_clk;
                plx_out(controller, plx_chipsc, chipsc);
                mask >>= 1;
            }
        }
        chipsc &= ~(chipsc_data_clk | chipsc_data_in);
        plx_out(controller, plx_chipsc, chipsc);
        w2::settle(w2::settle_spins >> 3);
        controller.fw_loaded = true;
        return true;
    }

    bool pcxhr_present() noexcept { return present; }

    bool pcxhr_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = pcxhr_find();
        if (device == nullptr) {
            serial::write_line("audiopci pcxhr=absent");
            return true;
        }
        serial::write("audiopci pcxhr plx=");
        w2::hex16(device->vendor_id);
        serial::write_char(':');
        w2::hex16(device->device_id);
        serial::write(" sub=");
        w2::hex16(w2::subsystem_vendor(*device));
        serial::write("\r\n");
        if (!pcxhr_bind(*device, active) || !pcxhr_reset(active)) {
            serial::write_line("audiopci pcxhr=fail");
            return false;
        }
        serial::write("audiopci pcxhr xlx=");
        serial::write(active.xilinx_present ? "yes" : "no");
        serial::write(" boot=");
        serial::write_line(active.boot_here ? "yes" : "no");
        void *image = w2::firmware_scratch();
        hcpp::u32 fw_bytes = 0;
        if (!w2::firmware_load("pcxhr", image,
                               w2::firmware_scratch_bytes, &fw_bytes)) {
            serial::write_line("audiopci pcxhr fw=missing");
            serial::write_line("audiopci pcxhr=parked");
            return true;
        }
        const bool loaded = pcxhr_load_xilinx(active, image, fw_bytes);
        serial::write("audiopci pcxhr fw=");
        serial::write_line(loaded ? "ok" : "fail");
        if (!loaded) {
            serial::write_line("audiopci pcxhr=parked");
            return true;
        }
        present = true;
        serial::write_line("audiopci pcxhr=ok");
        return true;
    }

}  // namespace hype::audio::pcxhr
