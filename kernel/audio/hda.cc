#include "kernel/audio/hda.h"

#include "kernel/audio/audiopcicard.h"
#include "kernel/audio/waveform.h"
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

        volatile hcpp::u8 &reg8(HdaController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u8 *>(controller.mmio + offset);
        }

        volatile hcpp::u16 &reg16(HdaController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u16 *>(controller.mmio + offset);
        }

        volatile hcpp::u32 &reg32(HdaController &controller, hcpp::u16 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(controller.mmio + offset);
        }

        void settle(hcpp::u32 spins) noexcept {
            for (hcpp::u32 i = 0; i < spins; ++i) {
                asm volatile("pause");
            }
        }

        bool wait_gctl(HdaController &controller, hcpp::u32 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg32(controller, reg_gctl) & gctl_reset) == expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_corbrp(HdaController &controller, hcpp::u16 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg16(controller, reg_corbrp) & corbrp_reset) == expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_rirb_response(HdaController &controller) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg16(controller, reg_rirbwp) & 0xFF) != controller.rirb_wp) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        bool wait_statests(HdaController &controller) noexcept {
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

        bool setup_ring(HdaController &controller, hcpp::u16 lbase, hcpp::u16 ubase, hcpp::u16 size_reg,
                        hcpp::u32 phys) noexcept {
            reg32(controller, lbase) = phys;
            reg32(controller, ubase) = 0;
            reg8(controller, size_reg) = ring_size_256;
            return (reg8(controller, size_reg) & 0x3) == ring_size_256;
        }

        bool setup_corb(HdaController &controller) noexcept {
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

        bool setup_rirb(HdaController &controller) noexcept {
            reg8(controller, reg_rirbctl) = 0;
            if (!setup_ring(controller, reg_rirblbase, reg_rirbubase, reg_rirbsize, controller.rirb_phys)) {
                return false;
            }
            reg16(controller, reg_rirbwp) = rirbwp_reset;
            reg16(controller, reg_rintcnt) = 1;
            controller.rirb_wp = 0;
            reg8(controller, reg_rirbctl) = rirbctl_run | rirbctl_irq_en;
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

        bool send_command(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                          hcpp::u32 command) noexcept {
            hcpp::u32 response = 0;
            return hda_codec_command(controller, codec, node, command, response);
        }

        bool get_parameter(HdaController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u8 param,
                           hcpp::u32 &value) noexcept {
            return hda_codec_verb(controller, codec, node, verb_get_parameter, param, value);
        }

        bool wait_stream_reset(HdaController &controller, hcpp::u16 offset, hcpp::u8 expected) noexcept {
            for (hcpp::u32 i = 0; i < poll_limit; ++i) {
                if ((reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) & sd_ctl_srst) == expected) {
                    return true;
                }
                asm volatile("pause");
            }
            return false;
        }

        hcpp::u8 pick_connection(HdaController &controller, hcpp::u8 codec, hcpp::u8 pin,
                                 hcpp::u32 widget_caps, hcpp::u8 dac) noexcept {
            if ((widget_caps & widget_cap_connlist) == 0) {
                return 0;
            }
            hcpp::u32 length = 0;
            if (!get_parameter(controller, codec, pin, param_connlist_len, length)) {
                return 0;
            }
            const hcpp::u8 count = static_cast<hcpp::u8>(length & 0x7F);
            const bool long_form = (length & 0x80) != 0;
            if (count <= 1) {
                return 0;
            }
            const hcpp::u8 lanes = long_form ? 2 : 4;
            const hcpp::u32 lane_mask = long_form ? 0xFFFFu : 0xFFu;
            for (hcpp::u8 index = 0; index < count; index = static_cast<hcpp::u8>(index + lanes)) {
                hcpp::u32 entries = 0;
                if (!hda_codec_verb(controller, codec, pin, verb_get_conn_list >> 8, index,
                                     entries)) {
                    return 0;
                }
                for (hcpp::u8 lane = 0; lane < lanes && index + lane < count; ++lane) {
                    const hcpp::u8 nid =
                        static_cast<hcpp::u8>((entries >> (lane * 8)) & lane_mask);
                    if (nid == dac) {
                        return static_cast<hcpp::u8>(index + lane);
                    }
                }
            }
            return 0;
        }

        bool find_output_path(HdaController &controller, hcpp::u8 codec,
                              HdaPlayback &playback) noexcept {
            hcpp::u32 nodes = 0;
            if (!get_parameter(controller, codec, 0, param_node_count, nodes)) {
                return false;
            }
            const hcpp::u8 fg_start = static_cast<hcpp::u8>((nodes >> 16) & 0x7F);
            const hcpp::u8 fg_count = static_cast<hcpp::u8>(nodes & 0xFF);
            hcpp::u8 pin_caps_node = 0;
            hcpp::u32 pin_widget_caps = 0;
            hcpp::u8 dac_widget_node = 0;
            for (hcpp::u8 group = 0; group < fg_count; ++group) {
                const hcpp::u8 fg = static_cast<hcpp::u8>(fg_start + group);
                if (fg == 0) {
                    continue;
                }
                hcpp::u32 type = 0;
                if (!get_parameter(controller, codec, fg, param_function_type, type) ||
                    (type & 0xFF) != function_group_audio) {
                    continue;
                }
                hcpp::u32 widgets = 0;
                if (!get_parameter(controller, codec, fg, param_node_count, widgets)) {
                    continue;
                }
                const hcpp::u8 widget_start = static_cast<hcpp::u8>((widgets >> 16) & 0x7F);
                const hcpp::u8 widget_count = static_cast<hcpp::u8>(widgets & 0xFF);
                for (hcpp::u8 i = 0; i < widget_count; ++i) {
                    const hcpp::u8 nid = static_cast<hcpp::u8>(widget_start + i);
                    if (nid == 0) {
                        continue;
                    }
                    hcpp::u32 caps = 0;
                    if (!get_parameter(controller, codec, nid, param_widget_caps, caps)) {
                        continue;
                    }
                    const hcpp::u8 kind = static_cast<hcpp::u8>((caps >> 20) & 0xF);
                    if (kind == widget_audio_output && dac_widget_node == 0) {
                        dac_widget_node = nid;
                    } else if (kind == widget_pin_complex && pin_caps_node == 0) {
                        hcpp::u32 pins = 0;
                        if (!get_parameter(controller, codec, nid, param_pin_caps, pins) ||
                            (pins & pin_cap_output) == 0) {
                            continue;
                        }
                        hcpp::u32 config = 0;
                        if (!hda_codec_command(controller, codec, nid, verb_get_config_default,
                                                config)) {
                            continue;
                        }
                        if (((config >> 30) & 0x3) == 0x1) {
                            continue;
                        }
                        pin_caps_node = nid;
                        pin_widget_caps = caps;
                    }
                }
                if (pin_caps_node != 0 && dac_widget_node != 0) {
                    playback.afg_node = fg;
                    playback.pin_node = pin_caps_node;
                    playback.dac_node = dac_widget_node;
                    playback.connect_index = pick_connection(controller, codec, pin_caps_node,
                                                             pin_widget_caps, dac_widget_node);
                    return true;
                }
            }
            return false;
        }

        bool unmute_output_amp(HdaController &controller, hcpp::u8 codec, hcpp::u8 node) noexcept {
            hcpp::u32 caps = 0;
            if (!get_parameter(controller, codec, node, param_widget_caps, caps) ||
                (caps & widget_cap_out_amp) == 0) {
                return true;
            }
            hcpp::u32 amp = 0;
            hcpp::u32 gain = 0;
            if (get_parameter(controller, codec, node, param_amp_out_caps, amp)) {
                gain = amp & 0x7F;
            }
            return send_command(controller, codec, node,
                                verb_set_amp_gain | amp_set_out_left_right | gain);
        }

        bool configure_output_path(HdaController &controller, hcpp::u8 codec,
                                   HdaPlayback &playback) noexcept {
            if (!send_command(controller, codec, playback.afg_node, verb_set_power_state) ||
                !send_command(controller, codec, playback.pin_node, verb_set_power_state) ||
                !send_command(controller, codec, playback.dac_node, verb_set_power_state)) {
                return false;
            }
            if (!send_command(controller, codec, playback.pin_node,
                              verb_set_connection | playback.connect_index)) {
                return false;
            }
            hcpp::u32 pin_control = 0;
            if (!hda_codec_command(controller, codec, playback.pin_node, verb_get_pin_control,
                                    pin_control)) {
                return false;
            }
            if (!send_command(controller, codec, playback.pin_node,
                              verb_set_pin_control |
                                  ((pin_control | pin_ctl_out_enable) & 0xFF))) {
                return false;
            }
            hcpp::u32 pins = 0;
            if (get_parameter(controller, codec, playback.pin_node, param_pin_caps, pins) &&
                (pins & pin_cap_eapd) != 0 &&
                !send_command(controller, codec, playback.pin_node,
                              verb_set_eapd | eapd_enable)) {
                return false;
            }
            if (!unmute_output_amp(controller, codec, playback.dac_node) ||
                !unmute_output_amp(controller, codec, playback.pin_node)) {
                return false;
            }
            if (!send_command(controller, codec, playback.dac_node,
                              verb_set_converter_stream |
                                  ((static_cast<hcpp::u32>(playback.stream_number) << 4) | 0))) {
                return false;
            }
            return send_command(controller, codec, playback.dac_node,
                                verb_set_stream_format | playback.format);
        }

        hcpp::u16 output_stream_offset(HdaController &controller) noexcept {
            const hcpp::u16 gcap = reg16(controller, reg_gcap);
            const hcpp::u16 input_streams = static_cast<hcpp::u16>((gcap >> 8) & 0xF);
            const hcpp::u16 output_streams = static_cast<hcpp::u16>((gcap >> 12) & 0xF);
            if (output_streams == 0) {
                return 0;
            }
            return static_cast<hcpp::u16>(reg_stream_base + input_streams * stream_desc_size);
        }

        hcpp::u16 input_stream_offset(HdaController &controller) noexcept {
            const hcpp::u16 gcap = reg16(controller, reg_gcap);
            const hcpp::u16 input_streams = static_cast<hcpp::u16>((gcap >> 8) & 0xF);
            if (input_streams == 0) {
                return 0;
            }
            return reg_stream_base;
        }

        bool find_input_path(HdaController &controller, hcpp::u8 codec,
                             HdaCapture &capture) noexcept {
            hcpp::u32 nodes = 0;
            if (!get_parameter(controller, codec, 0, param_node_count, nodes)) {
                return false;
            }
            const hcpp::u8 fg_start = static_cast<hcpp::u8>((nodes >> 16) & 0x7F);
            const hcpp::u8 fg_count = static_cast<hcpp::u8>(nodes & 0xFF);
            hcpp::u8 adc_widget_node = 0;
            hcpp::u32 adc_widget_caps = 0;
            hcpp::u8 pin_caps_node = 0;
            for (hcpp::u8 group = 0; group < fg_count; ++group) {
                const hcpp::u8 fg = static_cast<hcpp::u8>(fg_start + group);
                if (fg == 0) {
                    continue;
                }
                hcpp::u32 type = 0;
                if (!get_parameter(controller, codec, fg, param_function_type, type) ||
                    (type & 0xFF) != function_group_audio) {
                    continue;
                }
                hcpp::u32 widgets = 0;
                if (!get_parameter(controller, codec, fg, param_node_count, widgets)) {
                    continue;
                }
                const hcpp::u8 widget_start = static_cast<hcpp::u8>((widgets >> 16) & 0x7F);
                const hcpp::u8 widget_count = static_cast<hcpp::u8>(widgets & 0xFF);
                for (hcpp::u8 i = 0; i < widget_count; ++i) {
                    const hcpp::u8 nid = static_cast<hcpp::u8>(widget_start + i);
                    if (nid == 0) {
                        continue;
                    }
                    hcpp::u32 caps = 0;
                    if (!get_parameter(controller, codec, nid, param_widget_caps, caps)) {
                        continue;
                    }
                    const hcpp::u8 kind = static_cast<hcpp::u8>((caps >> 20) & 0xF);
                    if (kind == widget_audio_input && adc_widget_node == 0) {
                        adc_widget_node = nid;
                        adc_widget_caps = caps;
                    } else if (kind == widget_pin_complex && pin_caps_node == 0) {
                        hcpp::u32 pins = 0;
                        if (!get_parameter(controller, codec, nid, param_pin_caps, pins) ||
                            (pins & pin_cap_input) == 0) {
                            continue;
                        }
                        hcpp::u32 config = 0;
                        if (!hda_codec_command(controller, codec, nid, verb_get_config_default,
                                               config)) {
                            continue;
                        }
                        if (((config >> 30) & 0x3) == 0x1) {
                            continue;
                        }
                        pin_caps_node = nid;
                    }
                }
                if (adc_widget_node != 0 && pin_caps_node != 0) {
                    capture.afg_node = fg;
                    capture.adc_node = adc_widget_node;
                    capture.pin_node = pin_caps_node;
                    capture.connect_index = pick_connection(controller, codec, adc_widget_node,
                                                            adc_widget_caps, pin_caps_node);
                    return true;
                }
            }
            return false;
        }

        bool unmute_input_amp(HdaController &controller, hcpp::u8 codec, hcpp::u8 node) noexcept {
            hcpp::u32 caps = 0;
            if (!get_parameter(controller, codec, node, param_widget_caps, caps) ||
                (caps & widget_cap_in_amp) == 0) {
                return true;
            }
            hcpp::u32 amp = 0;
            hcpp::u32 gain = 0;
            if (get_parameter(controller, codec, node, param_amp_in_caps, amp)) {
                gain = amp & amp_gain_mask;
            }
            return send_command(controller, codec, node,
                                verb_set_amp_gain | amp_set_in_left_right | gain);
        }

        bool configure_input_path(HdaController &controller, hcpp::u8 codec,
                                  HdaCapture &capture) noexcept {
            if (!send_command(controller, codec, capture.afg_node, verb_set_power_state) ||
                !send_command(controller, codec, capture.pin_node, verb_set_power_state) ||
                !send_command(controller, codec, capture.adc_node, verb_set_power_state)) {
                return false;
            }
            if (!send_command(controller, codec, capture.adc_node,
                              verb_set_connection | capture.connect_index)) {
                return false;
            }
            hcpp::u32 pin_control = 0;
            if (!hda_codec_command(controller, codec, capture.pin_node, verb_get_pin_control,
                                   pin_control)) {
                return false;
            }
            if (!send_command(controller, codec, capture.pin_node,
                              verb_set_pin_control |
                                  ((pin_control | pin_ctl_in_enable) & 0xFF))) {
                return false;
            }
            if (!unmute_input_amp(controller, codec, capture.pin_node) ||
                !unmute_input_amp(controller, codec, capture.adc_node) ||
                !unmute_output_amp(controller, codec, capture.pin_node)) {
                return false;
            }
            if (!send_command(controller, codec, capture.adc_node,
                              verb_set_converter_stream |
                                  ((static_cast<hcpp::u32>(capture.stream_number) << 4) | 0))) {
                return false;
            }
            return send_command(controller, codec, capture.adc_node,
                                verb_set_stream_format | fmt_48k_s16_2ch);
        }

        bool build_capture_buffer(HdaCapture &capture) noexcept {
            capture.bdl_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
            capture.buffer_phys = static_cast<hcpp::u32>(memory::pmm_allocate_pages(capture_pages));
            if (capture.bdl_phys == 0 || capture.buffer_phys == 0) {
                return false;
            }
            zero_page(capture.bdl_phys);
            for (hcpp::u32 page = 0; page < capture_pages; ++page) {
                zero_page(capture.buffer_phys + page * memory::page_size);
            }
            capture.buffer_bytes = static_cast<hcpp::u32>(capture_pages * memory::page_size);
            auto *bdl = reinterpret_cast<volatile hcpp::u64 *>(capture.bdl_phys);
            capture.bdl_entries = static_cast<hcpp::u16>(capture_pages);
            for (hcpp::u16 i = 0; i < capture.bdl_entries; ++i) {
                bdl[i * 2] = capture.buffer_phys +
                             static_cast<hcpp::u64>(i) * memory::page_size;
                bdl[i * 2 + 1] = memory::page_size | bdl_ioc;
            }
            return true;
        }

        bool start_input_stream(HdaController &controller, HdaCapture &capture) noexcept {
            const hcpp::u16 offset = capture.stream_offset;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = 0;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = sd_ctl_srst;
            if (!wait_stream_reset(controller, offset, sd_ctl_srst)) {
                return false;
            }
            reg32(controller, static_cast<hcpp::u16>(offset + sd_cbl)) = capture.buffer_bytes;
            reg16(controller, static_cast<hcpp::u16>(offset + sd_lvi)) =
                static_cast<hcpp::u16>(capture.bdl_entries - 1);
            reg16(controller, static_cast<hcpp::u16>(offset + sd_fmt)) = fmt_48k_s16_2ch;
            reg32(controller, static_cast<hcpp::u16>(offset + sd_bdpl)) = capture.bdl_phys;
            reg32(controller, static_cast<hcpp::u16>(offset + sd_bdpu)) = 0;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = 0;
            if (!wait_stream_reset(controller, offset, 0)) {
                return false;
            }
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl_stream)) =
                static_cast<hcpp::u8>(capture.stream_number << 4);
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = sd_ctl_run | sd_ctl_ioce;
            return (reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) & sd_ctl_run) != 0;
        }

        bool program_output_stream(HdaController &controller,
                                   const HdaPlayback &playback) noexcept {
            const hcpp::u16 offset = playback.stream_offset;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = 0;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = sd_ctl_srst;
            if (!wait_stream_reset(controller, offset, sd_ctl_srst)) {
                return false;
            }
            reg32(controller, static_cast<hcpp::u16>(offset + sd_cbl)) = playback.buffer_bytes;
            reg16(controller, static_cast<hcpp::u16>(offset + sd_lvi)) =
                static_cast<hcpp::u16>(playback.bdl_entries - 1);
            reg16(controller, static_cast<hcpp::u16>(offset + sd_fmt)) = playback.format;
            reg32(controller, static_cast<hcpp::u16>(offset + sd_bdpl)) = playback.bdl_phys;
            reg32(controller, static_cast<hcpp::u16>(offset + sd_bdpu)) = 0;
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = 0;
            if (!wait_stream_reset(controller, offset, 0)) {
                return false;
            }
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl_stream)) =
                static_cast<hcpp::u8>(playback.stream_number << 4);
            return true;
        }

        bool start_output_stream(HdaController &controller, HdaPlayback &playback) noexcept {
            const hcpp::u16 offset = playback.stream_offset;
            if (!program_output_stream(controller, playback)) {
                return false;
            }
            reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) = sd_ctl_run;
            return (reg8(controller, static_cast<hcpp::u16>(offset + sd_ctl)) & sd_ctl_run) != 0;
        }

        bool build_stream_ring(HdaPlayback &playback, hcpp::u32 period_bytes,
                               hcpp::u16 entries) noexcept {
            if (period_bytes == 0 || entries < 2 || entries > 256 ||
                period_bytes > memory::page_size) {
                return false;
            }
            const hcpp::u64 ring_bytes =
                static_cast<hcpp::u64>(period_bytes) * entries;
            const hcpp::u32 pages = static_cast<hcpp::u32>(
                (ring_bytes + memory::page_size - 1) / memory::page_size);
            playback.bdl_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
            playback.buffer_phys = static_cast<hcpp::u32>(memory::pmm_allocate_pages(pages));
            if (playback.bdl_phys == 0 || playback.buffer_phys == 0) {
                return false;
            }
            for (hcpp::u16 i = 0; i < entries; ++i) {
                const hcpp::u32 address = playback.buffer_phys + i * period_bytes;
                if ((address & (memory::page_size - 1)) + period_bytes > memory::page_size) {
                    playback.buffer_phys = 0;
                    return false;
                }
            }
            zero_page(playback.bdl_phys);
            auto *bdl = reinterpret_cast<volatile hcpp::u64 *>(playback.bdl_phys);
            for (hcpp::u16 i = 0; i < entries; ++i) {
                bdl[i * 2] = playback.buffer_phys +
                             static_cast<hcpp::u64>(i) * period_bytes;
                bdl[i * 2 + 1] = period_bytes;
            }
            playback.buffer_bytes = static_cast<hcpp::u32>(ring_bytes);
            playback.bdl_entries = entries;
            auto *ring = reinterpret_cast<volatile hcpp::u64 *>(playback.buffer_phys);
            for (hcpp::u64 i = 0; i < ring_bytes / sizeof(hcpp::u64); ++i) {
                ring[i] = 0;
            }
            return true;
        }

        bool build_tone_buffer(HdaPlayback &playback) noexcept {
            playback.bdl_phys = static_cast<hcpp::u32>(memory::pmm_allocate_page());
            playback.buffer_phys = static_cast<hcpp::u32>(memory::pmm_allocate_pages(tone_pages));
            if (playback.bdl_phys == 0 || playback.buffer_phys == 0) {
                return false;
            }
            zero_page(playback.bdl_phys);
            playback.buffer_bytes = static_cast<hcpp::u32>(tone_pages * memory::page_size);
            auto *bdl = reinterpret_cast<volatile hcpp::u64 *>(playback.bdl_phys);
            playback.bdl_entries = static_cast<hcpp::u16>(tone_pages);
            for (hcpp::u16 i = 0; i < playback.bdl_entries; ++i) {
                bdl[i * 2] = playback.buffer_phys +
                             static_cast<hcpp::u64>(i) * memory::page_size;
                bdl[i * 2 + 1] = memory::page_size;
            }
            auto *samples = reinterpret_cast<hcpp::i16 *>(playback.buffer_phys);
            const hcpp::u32 frames = playback.buffer_bytes / (tone_channels * sizeof(hcpp::i16));
            return waveform_render_sine(samples, frames, tone_freq_hz, tone_rate, tone_channels) ==
                   frames;
        }

    }  // namespace

    bool hda_bind(hcpp::size_t card_index, HdaController &controller) noexcept {
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
        controller.irq_count = 0;
        controller.intsts_last = 0;
        return true;
    }

    bool hda_reset(HdaController &controller) noexcept {
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

    bool hda_codec_command(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                            hcpp::u32 command, hcpp::u32 &response) noexcept {
        if (!controller.reset_done || controller.corb == nullptr || controller.rirb == nullptr) {
            return false;
        }
        const hcpp::u16 wp = reg16(controller, reg_corbwp) & 0xFF;
        const hcpp::u16 next = static_cast<hcpp::u16>((wp + 1) & (ring_entries - 1));
        controller.corb[next] = (static_cast<hcpp::u32>(codec & 0xF) << 28) |
                                (static_cast<hcpp::u32>(node) << 20) | (command & 0xFFFFFF);
        asm volatile("" ::: "memory");
        reg16(controller, reg_corbwp) = next;
        if (!wait_rirb_response(controller)) {
            return false;
        }
        const hcpp::u16 rwp = reg16(controller, reg_rirbwp) & 0xFF;
        controller.rirb_wp = rwp;
        const hcpp::u64 entry = controller.rirb[rwp];
        response = static_cast<hcpp::u32>(entry);
        reg8(controller, reg_rirbsts) = rirbsts_irq;
        return true;
    }

    bool hda_codec_verb(HdaController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept {
        return hda_codec_command(controller, codec, node, ((verb & 0xFFF) << 8) | payload,
                                  response);
    }

    hcpp::u8 hda_scan_codecs(HdaController &controller, HdaCodec *codecs, hcpp::u8 max) noexcept {
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
            if (!hda_codec_verb(controller, address, 0, verb_get_parameter, param_vendor_id, response)) {
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

    bool hda_playback_start(HdaController &controller, hcpp::u8 codec,
                             HdaPlayback &playback) noexcept {
        playback = {};
        playback.codec = codec;
        playback.stream_number = 1;
        playback.format = fmt_48k_s16_2ch;
        playback.stream_offset = output_stream_offset(controller);
        if (playback.stream_offset == 0) {
            return false;
        }
        if (!find_output_path(controller, codec, playback)) {
            return false;
        }
        if (!build_tone_buffer(playback)) {
            return false;
        }
        if (!configure_output_path(controller, codec, playback)) {
            return false;
        }
        if (!start_output_stream(controller, playback)) {
            return false;
        }
        playback.running = true;
        return true;
    }

    hcpp::u32 hda_playback_position(HdaController &controller,
                                     const HdaPlayback &playback) noexcept {
        if (playback.stream_offset == 0) {
            return 0;
        }
        return reg32(controller, static_cast<hcpp::u16>(playback.stream_offset + sd_lpib));
    }

    bool hda_capture_start(HdaController &controller, hcpp::u8 codec,
                           HdaCapture &capture) noexcept {
        capture = {};
        capture.codec = codec;
        capture.stream_number = 2;
        capture.stream_offset = input_stream_offset(controller);
        if (capture.stream_offset == 0) {
            return false;
        }
        if (!find_input_path(controller, codec, capture)) {
            return false;
        }
        if (!build_capture_buffer(capture)) {
            return false;
        }
        if (!configure_input_path(controller, codec, capture)) {
            return false;
        }
        if (!start_input_stream(controller, capture)) {
            return false;
        }
        capture.read_pos = 0;
        capture.running = true;
        return true;
    }

    hcpp::u32 hda_capture_position(HdaController &controller,
                                   const HdaCapture &capture) noexcept {
        if (!capture.running || capture.stream_offset == 0) {
            return 0;
        }
        return reg32(controller, static_cast<hcpp::u16>(capture.stream_offset + sd_lpib));
    }

    hcpp::u32 hda_capture_read(HdaController &controller, HdaCapture &capture,
                               hcpp::i16 *buffer, hcpp::u32 frames) noexcept {
        if (!capture.running || buffer == nullptr || frames == 0 ||
            capture.buffer_phys == 0 || capture.buffer_bytes == 0) {
            return 0;
        }
        const hcpp::u32 lpib =
            reg32(controller, static_cast<hcpp::u16>(capture.stream_offset + sd_lpib));
        const hcpp::u32 cbl = capture.buffer_bytes;
        const hcpp::u32 read_pos = capture.read_pos % cbl;
        const hcpp::u32 avail =
            lpib >= read_pos ? lpib - read_pos : cbl - read_pos + lpib;
        const hcpp::u32 frame_bytes = tone_channels * sizeof(hcpp::i16);
        const hcpp::u32 avail_frames = avail / frame_bytes;
        const hcpp::u32 count = frames < avail_frames ? frames : avail_frames;
        const auto *source = reinterpret_cast<const volatile hcpp::u8 *>(capture.buffer_phys);
        auto *dst = reinterpret_cast<hcpp::u8 *>(buffer);
        for (hcpp::u32 i = 0; i < count * frame_bytes; ++i) {
            dst[i] = source[(read_pos + i) % cbl];
        }
        capture.read_pos = (read_pos + count * frame_bytes) % cbl;
        return count;
    }

    bool hda_interrupts_enable(HdaController &controller, hcpp::u32 stream_mask) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        const hcpp::u32 mask = stream_mask & intctl_stream_mask;
        reg32(controller, reg_intctl) |= intctl_gie | intctl_cie | mask;
        const hcpp::u32 ctl = reg32(controller, reg_intctl);
        return (ctl & intctl_gie) != 0 && (ctl & intctl_cie) != 0 &&
               (ctl & mask) == mask;
    }

    hcpp::u32 hda_poll_interrupts(HdaController &controller) noexcept {
        if (controller.mmio == 0) {
            return 0;
        }
        const hcpp::u32 sts = reg32(controller, reg_intsts);
        controller.intsts_last = sts;
        const hcpp::u32 stream_bits = sts & intctl_stream_mask;
        if (stream_bits == 0) {
            return 0;
        }
        hcpp::u32 events = 0;
        for (hcpp::u32 bits = stream_bits; bits != 0; bits &= bits - 1) {
            ++events;
        }
        controller.irq_count += events;
        reg32(controller, reg_intsts) = stream_bits;
        return stream_bits;
    }

    bool hda_volume_set(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                        hcpp::u8 gain, bool mute) noexcept {
        hcpp::u32 response = 0;
        const hcpp::u32 payload = amp_set_out_left_right |
                                  (mute ? amp_mute_bit : 0) |
                                  (static_cast<hcpp::u32>(gain) & amp_gain_mask);
        return hda_codec_command(controller, codec, node, verb_set_amp_gain | payload, response);
    }

    bool hda_volume_get(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                        hcpp::u8 &gain, bool &mute) noexcept {
        hcpp::u32 response = 0;
        if (!hda_codec_command(controller, codec, node,
                               verb_get_amp_gain | amp_get_out_left, response)) {
            return false;
        }
        gain = static_cast<hcpp::u8>(response & amp_gain_mask);
        mute = (response & amp_mute_bit) != 0;
        return true;
    }

    bool hda_topology_walk(HdaController &controller, hcpp::u8 codec,
                           HdaTopology &topology) noexcept {
        topology = {};
        hcpp::u32 nodes = 0;
        if (!get_parameter(controller, codec, 0, param_node_count, nodes)) {
            return false;
        }
        const hcpp::u8 fg_start = static_cast<hcpp::u8>((nodes >> 16) & 0x7F);
        const hcpp::u8 fg_count = static_cast<hcpp::u8>(nodes & 0xFF);
        for (hcpp::u8 group = 0; group < fg_count; ++group) {
            const hcpp::u8 fg = static_cast<hcpp::u8>(fg_start + group);
            if (fg == 0) {
                continue;
            }
            ++topology.function_groups;
            hcpp::u32 type = 0;
            if (!get_parameter(controller, codec, fg, param_function_type, type) ||
                (type & 0xFF) != function_group_audio) {
                continue;
            }
            ++topology.audio_groups;
            hcpp::u32 widgets = 0;
            if (!get_parameter(controller, codec, fg, param_node_count, widgets)) {
                continue;
            }
            const hcpp::u8 widget_start = static_cast<hcpp::u8>((widgets >> 16) & 0x7F);
            const hcpp::u8 widget_count = static_cast<hcpp::u8>(widgets & 0xFF);
            for (hcpp::u8 i = 0; i < widget_count; ++i) {
                const hcpp::u8 nid = static_cast<hcpp::u8>(widget_start + i);
                if (nid == 0) {
                    continue;
                }
                hcpp::u32 caps = 0;
                if (!get_parameter(controller, codec, nid, param_widget_caps, caps)) {
                    continue;
                }
                ++topology.widgets;
                const hcpp::u8 kind = static_cast<hcpp::u8>((caps >> 20) & 0xF);
                if (kind == widget_audio_output) {
                    ++topology.dac_widgets;
                } else if (kind == widget_audio_input) {
                    ++topology.adc_widgets;
                } else if (kind == widget_pin_complex) {
                    ++topology.pin_widgets;
                }
            }
        }
        return topology.function_groups != 0;
    }

    hcpp::u16 hda_format_word(hcpp::u32 rate, hcpp::u8 channels, hcpp::u8 bits) noexcept {
        hcpp::u16 bits_code;
        switch (bits) {
            case 8: bits_code = 0; break;
            case 16: bits_code = 1; break;
            case 20: bits_code = 2; break;
            case 24: bits_code = 3; break;
            case 32: bits_code = 4; break;
            default: return hda_format_invalid;
        }
        if (channels == 0 || channels > 16) {
            return hda_format_invalid;
        }
        hcpp::u16 base = 0;
        hcpp::u16 mult = 0;
        hcpp::u16 div = 0;
        switch (rate) {
            case 48000: break;
            case 44100: base = 1; break;
            case 96000: mult = 1; break;
            case 88200: base = 1; mult = 1; break;
            case 24000: div = 1; break;
            case 22050: base = 1; div = 1; break;
            case 192000: mult = 3; break;
            default: return hda_format_invalid;
        }
        return static_cast<hcpp::u16>((channels - 1) | (bits_code << 4) | (div << 8) |
                                      (mult << 11) | (base << 14));
    }

    bool hda_stream_open(HdaController &controller, hcpp::u8 codec, HdaPlayback &playback,
                         hcpp::u16 format, hcpp::u32 period_bytes,
                         hcpp::u16 bdl_entries) noexcept {
        playback = {};
        playback.codec = codec;
        playback.stream_number = 1;
        playback.format = format;
        playback.stream_offset = output_stream_offset(controller);
        if (playback.stream_offset == 0) {
            return false;
        }
        if (!find_output_path(controller, codec, playback)) {
            return false;
        }
        if (!build_stream_ring(playback, period_bytes, bdl_entries)) {
            return false;
        }
        if (!configure_output_path(controller, codec, playback)) {
            return false;
        }
        return program_output_stream(controller, playback);
    }

    bool hda_stream_run(HdaController &controller, HdaPlayback &playback) noexcept {
        if (playback.stream_offset == 0) {
            return false;
        }
        const hcpp::u16 ctl = playback.stream_offset + sd_ctl;
        reg8(controller, ctl) = static_cast<hcpp::u8>(reg8(controller, ctl) | sd_ctl_run);
        if ((reg8(controller, ctl) & sd_ctl_run) == 0) {
            return false;
        }
        playback.running = true;
        return true;
    }

    bool hda_stream_halt(HdaController &controller, HdaPlayback &playback) noexcept {
        if (playback.stream_offset == 0) {
            playback.running = false;
            return false;
        }
        const hcpp::u16 ctl = playback.stream_offset + sd_ctl;
        reg8(controller, ctl) = static_cast<hcpp::u8>(reg8(controller, ctl) & ~sd_ctl_run);
        reg8(controller, ctl) = static_cast<hcpp::u8>(reg8(controller, ctl) | sd_ctl_srst);
        const bool reset = wait_stream_reset(controller, playback.stream_offset, sd_ctl_srst);
        reg8(controller, ctl) = static_cast<hcpp::u8>(reg8(controller, ctl) & ~sd_ctl_srst);
        playback.running = false;
        return reset;
    }

}  // namespace hype::audio::hda
