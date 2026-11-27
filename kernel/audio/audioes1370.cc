#include "kernel/audio/audioes1370.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::es1370 {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x4000000;
        constexpr hcpp::u32 settle_spins = 0x200000;

        Es1370Controller active{};
        bool probed;
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void write16(const Es1370Controller &controller, hcpp::u8 reg,
                     hcpp::u16 value) noexcept {
            hype_port_out16(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        void write32(const Es1370Controller &controller, hcpp::u8 reg,
                     hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(controller.iobase + reg), value);
        }

        hcpp::u32 read32(const Es1370Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(controller.iobase + reg));
        }

        void select_page(const Es1370Controller &controller, hcpp::u8 page) noexcept {
            write32(controller, reg_mempage, page);
        }

        void paged_write32(const Es1370Controller &controller, hcpp::u8 page,
                           hcpp::u8 offset, hcpp::u32 value) noexcept {
            select_page(controller, page);
            write32(controller, offset, value);
        }

        hcpp::u32 paged_read32(const Es1370Controller &controller, hcpp::u8 page,
                               hcpp::u8 offset) noexcept {
            select_page(controller, page);
            return read32(controller, offset);
        }

        bool wait_codec_idle(const Es1370Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((read32(controller, reg_status) & stat_cstat) == 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool is_es1370_device(const pci::PciDevice &device) noexcept {
            if (device.vendor_id == vendor_ensoniq) {
                return true;
            }
            return device.class_code == class_multimedia &&
                   device.subclass == subclass_audio && device.vendor_id != 0x8086;
        }

        bool claim_bar(const pci::PciDevice &device, Es1370Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function, pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function, pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space | pci_cmd_bus_master);
            const hcpp::u32 bar0 = pci::bar(device, 0);
            if ((bar0 & 0x1) == 0) {
                return false;
            }
            const hcpp::u32 iobase = bar0 & ~hcpp::u32(0x3);
            if (iobase == 0 || iobase > 0xFF00) {
                return false;
            }
            controller.iobase = static_cast<hcpp::u16>(iobase);
            return true;
        }

        bool ensure_buffer(Es1370Controller &controller) noexcept {
            if (controller.buffer_phys != 0) {
                return true;
            }
            controller.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(ring_pages));
            if (controller.buffer_phys == 0) {
                return false;
            }
            controller.buffer_bytes = memory::page_size * ring_pages;
            controller.buffer_dwords = controller.buffer_bytes / sizeof(hcpp::u32);
            controller.buffer_frames =
                controller.buffer_bytes / (pcm_channels * sizeof(hcpp::i16));
            return true;
        }

        hcpp::u32 frame_position(Es1370Controller &controller) noexcept {
            const hcpp::u32 frame_cnt =
                paged_read32(controller, page_dac, page_dac1_frame_cnt);
            return frame_cnt >> 16;
        }

        void write_hex32(hcpp::u32 value) noexcept {
            for (int shift = 28; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *es1370_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        const pci::PciDevice *generic = nullptr;
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_ensoniq && device.device_id == device_es1370) {
                return &device;
            }
            if (generic == nullptr && is_es1370_device(device)) {
                generic = &device;
            }
        }
        return generic;
    }

    bool es1370_bind(const pci::PciDevice &device, Es1370Controller &controller) noexcept {
        if (!is_es1370_device(device)) {
            return false;
        }
        controller = {};
        controller.card_index = 0xFF;
        return claim_bar(device, controller);
    }

    bool es1370_reset(Es1370Controller &controller) noexcept {
        if (controller.iobase == 0) {
            return false;
        }
        write32(controller, reg_control, ctrl_adc_stop);
        settle(settle_spins >> 2);
        write32(controller, reg_serial, 0);
        write32(controller, reg_mempage, 0);
        for (hcpp::u8 reg = 0; reg < codec_regs; ++reg) {
            controller.codec_shadow[reg] = 0;
        }
        const hcpp::u32 control = ctrl_serr_dis | ctrl_cdc_en |
                                  (dac1_rate_44100 << ctrl_sh_wtsrsel);
        write32(controller, reg_control, control);
        settle(settle_spins >> 2);
        const hcpp::u32 read_back = read32(controller, reg_control);
        if ((read_back & (ctrl_serr_dis | ctrl_cdc_en | ctrl_wtsrsel)) != control) {
            return false;
        }
        es1370_codec_write(controller, ak_master_lch, 0x1F);
        es1370_codec_write(controller, ak_master_rch, 0x1F);
        controller.reset_done = true;
        return true;
    }

    bool es1370_codec_write(Es1370Controller &controller, hcpp::u8 reg,
                            hcpp::u8 value) noexcept {
        if (controller.iobase == 0 || reg >= codec_regs) {
            return false;
        }
        if (!wait_codec_idle(controller)) {
            return false;
        }
        write16(controller, reg_codec,
                static_cast<hcpp::u16>((reg << 8) | value));
        controller.codec_shadow[reg] = value;
        return wait_codec_idle(controller);
    }

    hcpp::u8 es1370_codec_read(const Es1370Controller &controller, hcpp::u8 reg) noexcept {
        if (reg >= codec_regs) {
            return 0xFF;
        }
        return controller.codec_shadow[reg];
    }

    bool es1370_play(Es1370Controller &controller, hcpp::u32 frames) noexcept {
        if (!controller.reset_done || !ensure_buffer(controller)) {
            return false;
        }
        write32(controller, reg_control,
                (read32(controller, reg_control) & ~ctrl_dac1_en));
        write32(controller, reg_serial, read32(controller, reg_serial) | sctrl_p1pause);
        const hcpp::u32 render = frames == 0 || frames > controller.buffer_frames
                                     ? controller.buffer_frames
                                     : frames;
        auto *samples = reinterpret_cast<hcpp::i16 *>(controller.buffer_phys);
        waveform_render_sine(samples, render, tone_freq_hz, pcm_rate, pcm_channels);
        paged_write32(controller, page_dac, page_dac1_frame_addr, controller.buffer_phys);
        paged_write32(controller, page_dac, page_dac1_frame_cnt,
                      controller.buffer_dwords - 1);
        write32(controller, reg_dac1_scount, controller.buffer_frames - 1);
        write32(controller, reg_serial, sctrl_fmt_s16s);
        write32(controller, reg_control,
                ctrl_serr_dis | ctrl_cdc_en |
                    (dac1_rate_44100 << ctrl_sh_wtsrsel) | ctrl_dac1_en);
        controller.running =
            (read32(controller, reg_control) & ctrl_dac1_en) != 0;
        return controller.running;
    }

    void es1370_stop(Es1370Controller &controller) noexcept {
        if (controller.iobase != 0) {
            write32(controller, reg_control,
                    read32(controller, reg_control) & ~ctrl_dac1_en);
            write32(controller, reg_serial,
                    read32(controller, reg_serial) | sctrl_p1pause);
        }
        controller.running = false;
    }

    hcpp::u32 es1370_pos(Es1370Controller &controller) noexcept {
        if (!controller.running || controller.iobase == 0) {
            return 0;
        }
        return frame_position(controller);
    }

    bool es1370_volume(Es1370Controller &controller, hcpp::u8 percent) noexcept {
        if (!controller.reset_done || percent > 100) {
            return false;
        }
        hcpp::u8 att = static_cast<hcpp::u8>((100 - percent) * 31 / 100);
        if (att > 31) {
            att = 31;
        }
        const hcpp::u8 value =
            percent == 0 ? static_cast<hcpp::u8>(att | ak_mute) : att;
        const bool ok = es1370_codec_write(controller, ak_master_lch, value) &&
                        es1370_codec_write(controller, ak_master_rch, value);
        if (ok) {
            controller.volume = percent;
        }
        return ok && es1370_codec_read(controller, ak_master_lch) == value &&
               es1370_codec_read(controller, ak_master_rch) == value;
    }

    bool es1370_present() noexcept { return present; }

    bool es1370_ready() noexcept { return probed; }

    bool es1370_selftest() noexcept {
        probed = true;
        present = false;
        const pci::PciDevice *device = es1370_find();
        if (device == nullptr) {
            serial::write_line("es1370=absent");
            return true;
        }
        if (!es1370_bind(*device, active) || !es1370_reset(active)) {
            serial::write_line("es1370 dev=fail");
            return false;
        }
        present = true;
        serial::write_line("es1370 dev=ok");
        const bool vol_ok = es1370_volume(active, 75);
        serial::write("es1370 vol=");
        serial::write_line(vol_ok ? "ok" : "fail");
        if (!es1370_play(active, 0)) {
            serial::write_line("es1370 dac=fail");
            return false;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 256 && pos == 0; ++round) {
            settle(0x80000);
            pos = frame_position(active);
        }
        serial::write("es1370 pos=");
        write_hex32(pos);
        serial::write("\r\n");
        serial::write("es1370 dac=");
        serial::write_line(pos != 0 ? "ok" : "fail");
        es1370_stop(active);
        return pos != 0 && vol_ok;
    }

}  // namespace hype::audio::es1370
