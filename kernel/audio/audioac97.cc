#include "kernel/audio/audioac97.h"

#include "kernel/arch/x86/io.h"
#include "kernel/audio/audiopcicard.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::ac97 {
    namespace {

        constexpr hcpp::u32 poll_limit = 0x4000000;
        constexpr hcpp::u32 settle_spins = 0x200000;
        constexpr hcpp::u32 bd_bytes = memory::page_size;
        constexpr hcpp::u32 bd_samples = bd_bytes / sizeof(hcpp::i16);
        constexpr hcpp::u32 bd_frames = bd_samples / pcm_channels;

        Ac97Controller active{};
        bool probed;
        bool present;

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        void nam_write(const Ac97Controller &controller, hcpp::u16 reg,
                       hcpp::u16 value) noexcept {
            hype_port_out16(static_cast<hcpp::u16>(controller.nambar + reg), value);
        }

        hcpp::u16 nam_read(const Ac97Controller &controller, hcpp::u16 reg) noexcept {
            return hype_port_in16(static_cast<hcpp::u16>(controller.nambar + reg));
        }

        void nabm_write8(const Ac97Controller &controller, hcpp::u8 reg,
                         hcpp::u8 value) noexcept {
            hype_port_out8(static_cast<hcpp::u16>(controller.nabmbar + reg), value);
        }

        hcpp::u8 nabm_read8(const Ac97Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in8(static_cast<hcpp::u16>(controller.nabmbar + reg));
        }

        hcpp::u16 nabm_read16(const Ac97Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in16(static_cast<hcpp::u16>(controller.nabmbar + reg));
        }

        void nabm_write32(const Ac97Controller &controller, hcpp::u8 reg,
                          hcpp::u32 value) noexcept {
            hype_port_out32(static_cast<hcpp::u16>(controller.nabmbar + reg), value);
        }

        hcpp::u32 nabm_read32(const Ac97Controller &controller, hcpp::u8 reg) noexcept {
            return hype_port_in32(static_cast<hcpp::u16>(controller.nabmbar + reg));
        }

        bool wait_codec_ready(const Ac97Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((nabm_read32(controller, nabm_glob_sta) & sta_primary_ready) != 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_channel_reset(const Ac97Controller &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((nabm_read8(controller, nabm_po_cr) & cr_rr) == 0) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool claim_bars(const pci::PciDevice &device, Ac97Controller &controller) noexcept {
            const hcpp::u32 command =
                pci::config_read32(device.bus, device.slot, device.function, pci_command_reg);
            pci::config_write32(device.bus, device.slot, device.function, pci_command_reg,
                                (command & 0xFFFF) | pci_cmd_io_space | pci_cmd_bus_master);
            const hcpp::u32 bar0 = pci::bar(device, 0);
            const hcpp::u32 bar1 = pci::bar(device, 1);
            if ((bar0 & 0x1) == 0 || (bar1 & 0x1) == 0) {
                return false;
            }
            const hcpp::u32 nambar = bar0 & ~hcpp::u32(0x3);
            const hcpp::u32 nabmbar = bar1 & ~hcpp::u32(0x3);
            if (nambar == 0 || nabmbar == 0 || nambar > 0xFFFF || nabmbar > 0xFFFF) {
                return false;
            }
            controller.nambar = static_cast<hcpp::u16>(nambar);
            controller.nabmbar = static_cast<hcpp::u16>(nabmbar);
            return true;
        }

        bool volume_reg(const Ac97Controller &controller, hcpp::u16 reg,
                        hcpp::u8 percent) noexcept {
            hcpp::u16 att = static_cast<hcpp::u16>((100 - percent) * 31 / 100);
            if (att > 31) {
                att = 31;
            }
            const hcpp::u16 value = percent == 0
                ? vol_mute
                : static_cast<hcpp::u16>(att | (att << 8));
            nam_write(controller, reg, value);
            return (nam_read(controller, reg) & vol_mask) == (value & vol_mask);
        }

        bool ensure_buffers(Ac97Controller &controller) noexcept {
            if (controller.bd != nullptr) {
                return true;
            }
            controller.bd_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
            controller.buffer_phys =
                static_cast<hcpp::u32>(memory::pmm_allocate_pages(bd_entries));
            if (controller.bd_phys == 0 || controller.buffer_phys == 0) {
                return false;
            }
            controller.bd =
                reinterpret_cast<volatile Ac97Descriptor *>(controller.bd_phys);
            for (hcpp::u8 i = 0; i < bd_entries; ++i) {
                controller.bd[i].address = controller.buffer_phys + i * bd_bytes;
                controller.bd[i].samples = static_cast<hcpp::u16>(bd_samples);
                controller.bd[i].flags = bd_flag_ioc | bd_flag_bup;
            }
            controller.buffer_bytes = bd_bytes * bd_entries;
            controller.buffer_frames = controller.buffer_bytes /
                                       (pcm_channels * sizeof(hcpp::i16));
            return true;
        }

        bool is_ac97_device(const pci::PciDevice &device) noexcept {
            if (device.vendor_id == vendor_intel && device.device_id == device_82801aa) {
                return true;
            }
            return device.class_code == class_multimedia && device.subclass == subclass_ac97;
        }

        hcpp::u8 read_civ(const Ac97Controller &controller) noexcept {
            return nabm_read8(controller, nabm_po_civ) & 0x1F;
        }

        void write_hex32(hcpp::u32 value) noexcept {
            for (int shift = 28; shift >= 0; shift -= 4) {
                const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
                serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                              : static_cast<char>('a' + digit - 10));
            }
        }

    }  // namespace

    const pci::PciDevice *ac97_find() noexcept {
        const pci::PciDevice *devices = pci::devices();
        const hcpp::size_t count = pci::device_count();
        const pci::PciDevice *generic = nullptr;
        for (hcpp::size_t i = 0; i < count; ++i) {
            const pci::PciDevice &device = devices[i];
            if (device.vendor_id == vendor_intel && device.device_id == device_82801aa) {
                return &device;
            }
            if (generic == nullptr && is_ac97_device(device)) {
                generic = &device;
            }
        }
        return generic;
    }

    bool ac97_bind(const pci::PciDevice &device, Ac97Controller &controller) noexcept {
        if (!is_ac97_device(device)) {
            return false;
        }
        controller = {};
        controller.card_index = 0xFF;
        return claim_bars(device, controller);
    }

    bool ac97_bind_card(hcpp::size_t card_index, Ac97Controller &controller) noexcept {
        if (card_index >= audio_chip_count()) {
            return false;
        }
        const AudioCard &card = audio_cards()[card_index];
        if (card.chip != AudioChip::IntelAc97 || card.bus != AudioBus::Pci) {
            return false;
        }
        const pci::PciDevice device{card.bus_number, card.slot,     card.function,
                                    card.vendor_id,  card.device_id, card.class_code,
                                    card.subclass,   0,              0,
                                    0};
        controller = {};
        controller.card_index = static_cast<hcpp::u8>(card_index);
        return claim_bars(device, controller);
    }

    bool ac97_reset(Ac97Controller &controller) noexcept {
        if (controller.nambar == 0 || controller.nabmbar == 0) {
            return false;
        }
        nabm_write32(controller, nabm_glob_cnt, 0);
        settle(settle_spins);
        nabm_write32(controller, nabm_glob_cnt, cnt_cold_reset);
        settle(settle_spins);
        if (!wait_codec_ready(controller)) {
            nabm_write32(controller, nabm_glob_cnt, cnt_cold_reset | cnt_warm_reset);
            settle(settle_spins);
            if (!wait_codec_ready(controller)) {
                return false;
            }
        }
        nam_write(controller, nam_reset, 0xFFFF);
        settle(settle_spins >> 2);
        nam_write(controller, nam_powerdown, 0);
        nam_write(controller, nam_front_dac_rate, pcm_rate);
        const hcpp::u16 vid1 = nam_read(controller, nam_vendor_id1);
        const hcpp::u16 vid2 = nam_read(controller, nam_vendor_id2);
        controller.codec_id = (static_cast<hcpp::u32>(vid1) << 16) | vid2;
        if (vid1 == 0xFFFF || vid1 == 0x0000) {
            return false;
        }
        controller.reset_done = true;
        return true;
    }

    bool ac97_play(Ac97Controller &controller, hcpp::u32 frames) noexcept {
        if (!controller.reset_done || !ensure_buffers(controller)) {
            return false;
        }
        nabm_write8(controller, nabm_po_cr, cr_rr);
        if (!wait_channel_reset(controller)) {
            return false;
        }
        const hcpp::u32 render = frames == 0 || frames > controller.buffer_frames
                                     ? controller.buffer_frames
                                     : frames;
        auto *samples = reinterpret_cast<hcpp::i16 *>(controller.buffer_phys);
        waveform_render_sine(samples, render, tone_freq_hz, pcm_rate, pcm_channels);
        nabm_write32(controller, nabm_po_bdbar, controller.bd_phys);
        nabm_write8(controller, nabm_po_lvi, bd_entries - 1);
        nabm_write8(controller, nabm_po_cr, cr_rpbm);
        controller.running = (nabm_read8(controller, nabm_po_cr) & cr_rpbm) != 0;
        return controller.running;
    }

    void ac97_stop(Ac97Controller &controller) noexcept {
        if (controller.nabmbar != 0) {
            nabm_write8(controller, nabm_po_cr, 0);
        }
        controller.running = false;
    }

    hcpp::u32 ac97_pos(const Ac97Controller &controller) noexcept {
        if (!controller.running || controller.nabmbar == 0) {
            return 0;
        }
        const hcpp::u8 civ = read_civ(controller);
        const hcpp::u16 picb = nabm_read16(controller, nabm_po_picb);
        const hcpp::u32 done_in_buffer =
            picb > bd_samples ? 0 : (bd_samples - picb) / pcm_channels;
        return civ * bd_frames + done_in_buffer;
    }

    bool ac97_set_master_volume(Ac97Controller &controller, hcpp::u8 percent) noexcept {
        if (!controller.reset_done || percent > 100) {
            return false;
        }
        return volume_reg(controller, nam_master_volume, percent);
    }

    bool ac97_set_pcm_volume(Ac97Controller &controller, hcpp::u8 percent) noexcept {
        if (!controller.reset_done || percent > 100) {
            return false;
        }
        return volume_reg(controller, nam_pcm_volume, percent);
    }

    bool ac97_present() noexcept { return present; }

    bool ac97_ready() noexcept { return probed; }

    bool ac97_selftest() noexcept {
        probed = true;
        present = false;
        const pci::PciDevice *device = ac97_find();
        if (device == nullptr) {
            serial::write_line("ac97=absent");
            return true;
        }
        if (!ac97_bind(*device, active) || !ac97_reset(active)) {
            serial::write_line("ac97 dev=fail");
            return false;
        }
        present = true;
        serial::write_line("ac97 dev=ok");
        serial::write("ac97 codec=");
        write_hex32(active.codec_id);
        serial::write("\r\n");
        const bool vol_ok = ac97_set_master_volume(active, 75) &&
                            ac97_set_pcm_volume(active, 75);
        serial::write("ac97 vol=");
        serial::write_line(vol_ok ? "ok" : "fail");
        if (!ac97_play(active, 0)) {
            serial::write_line("ac97 civ=0");
            return false;
        }
        hcpp::u8 civ = 0;
        for (hcpp::u32 round = 0; round < 256 && civ == 0; ++round) {
            settle(0x80000);
            civ = read_civ(active);
        }
        serial::write("ac97 civ");
        serial::write_line(civ != 0 ? ">0" : "=0");
        serial::write("ac97 pos=");
        write_hex32(ac97_pos(active));
        serial::write("\r\n");
        ac97_stop(active);
        return civ != 0 && vol_ok;
    }

}  // namespace hype::audio::ac97
