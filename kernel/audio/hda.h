#pragma once

#include "hcpp/types.hpp"

namespace hype::audio::hda {

    constexpr hcpp::u16 reg_gcap = 0x00;
    constexpr hcpp::u16 reg_vmin = 0x02;
    constexpr hcpp::u16 reg_vmaj = 0x03;
    constexpr hcpp::u16 reg_gctl = 0x08;
    constexpr hcpp::u16 reg_wakeen = 0x0C;
    constexpr hcpp::u16 reg_statests = 0x0E;
    constexpr hcpp::u16 reg_intctl = 0x20;
    constexpr hcpp::u16 reg_intsts = 0x24;
    constexpr hcpp::u16 reg_corblbase = 0x40;
    constexpr hcpp::u16 reg_corbubase = 0x44;
    constexpr hcpp::u16 reg_corbwp = 0x48;
    constexpr hcpp::u16 reg_corbrp = 0x4A;
    constexpr hcpp::u16 reg_corbctl = 0x4C;
    constexpr hcpp::u16 reg_corbsts = 0x4D;
    constexpr hcpp::u16 reg_corbsize = 0x4E;
    constexpr hcpp::u16 reg_rirblbase = 0x50;
    constexpr hcpp::u16 reg_rirbubase = 0x54;
    constexpr hcpp::u16 reg_rirbwp = 0x58;
    constexpr hcpp::u16 reg_rintcnt = 0x5A;
    constexpr hcpp::u16 reg_rirbctl = 0x5C;
    constexpr hcpp::u16 reg_rirbsts = 0x5D;
    constexpr hcpp::u16 reg_rirbsize = 0x5E;
    constexpr hcpp::u16 reg_ics = 0x60;
    constexpr hcpp::u16 reg_stream_base = 0x80;
    constexpr hcpp::u16 stream_desc_size = 0x20;

    constexpr hcpp::u16 sd_ctl = 0x00;
    constexpr hcpp::u16 sd_ctl_stream = 0x02;
    constexpr hcpp::u16 sd_sts = 0x03;
    constexpr hcpp::u16 sd_lpib = 0x04;
    constexpr hcpp::u16 sd_cbl = 0x08;
    constexpr hcpp::u16 sd_lvi = 0x0C;
    constexpr hcpp::u16 sd_fmt = 0x12;
    constexpr hcpp::u16 sd_bdpl = 0x18;
    constexpr hcpp::u16 sd_bdpu = 0x1C;

    constexpr hcpp::u32 gctl_reset = 0x1;
    constexpr hcpp::u8 corbctl_run = 0x2;
    constexpr hcpp::u8 rirbctl_run = 0x2;
    constexpr hcpp::u8 rirbctl_irq_en = 0x1;
    constexpr hcpp::u8 rirbsts_irq = 0x1;
    constexpr hcpp::u8 ring_size_256 = 0x2;
    constexpr hcpp::u16 corbrp_reset = 0x8000;
    constexpr hcpp::u16 rirbwp_reset = 0x8000;
    constexpr hcpp::u16 ring_entries = 256;
    constexpr hcpp::u8 max_codecs = 15;
    constexpr hcpp::u8 sd_ctl_srst = 0x1;
    constexpr hcpp::u8 sd_ctl_run = 0x2;
    constexpr hcpp::u8 sd_ctl_ioce = 0x4;
    constexpr hcpp::u32 intctl_stream_mask = 0x3FFFFFFF;
    constexpr hcpp::u32 intctl_cie = 0x40000000;
    constexpr hcpp::u32 intctl_gie = 0x80000000;
    constexpr hcpp::u64 bdl_ioc = 1ull << 32;
    constexpr hcpp::u32 verb_get_parameter = 0xF00;
    constexpr hcpp::u32 param_vendor_id = 0x00;
    constexpr hcpp::u32 param_node_count = 0x04;
    constexpr hcpp::u32 param_function_type = 0x05;
    constexpr hcpp::u32 param_widget_caps = 0x09;
    constexpr hcpp::u32 param_pin_caps = 0x0C;
    constexpr hcpp::u32 param_connlist_len = 0x0E;
    constexpr hcpp::u32 param_amp_out_caps = 0x12;
    constexpr hcpp::u32 param_amp_in_caps = 0x13;
    constexpr hcpp::u8 function_group_audio = 0x01;
    constexpr hcpp::u8 widget_audio_output = 0x0;
    constexpr hcpp::u8 widget_audio_input = 0x1;
    constexpr hcpp::u8 widget_pin_complex = 0x4;
    constexpr hcpp::u32 widget_cap_connlist = 1u << 8;
    constexpr hcpp::u32 widget_cap_out_amp = 1u << 2;
    constexpr hcpp::u32 widget_cap_in_amp = 1u << 1;
    constexpr hcpp::u32 pin_cap_output = 1u << 4;
    constexpr hcpp::u32 pin_cap_input = 1u << 5;
    constexpr hcpp::u32 pin_cap_eapd = 1u << 16;
    constexpr hcpp::u8 pin_ctl_out_enable = 0x40;
    constexpr hcpp::u8 pin_ctl_in_enable = 0x20;
    constexpr hcpp::u8 eapd_enable = 0x2;
    constexpr hcpp::u32 verb_set_converter_stream = 0x70600;
    constexpr hcpp::u32 verb_set_pin_control = 0x70700;
    constexpr hcpp::u32 verb_set_power_state = 0x70500;
    constexpr hcpp::u32 verb_set_connection = 0x70100;
    constexpr hcpp::u32 verb_set_eapd = 0x70C00;
    constexpr hcpp::u32 verb_get_pin_control = 0xF0700;
    constexpr hcpp::u32 verb_get_config_default = 0xF1C00;
    constexpr hcpp::u32 verb_get_conn_list = 0xF0200;
    constexpr hcpp::u32 verb_set_stream_format = 0x20000;
    constexpr hcpp::u32 verb_set_amp_gain = 0x30000;
    constexpr hcpp::u32 verb_get_amp_gain = 0xB0000;
    constexpr hcpp::u32 amp_set_out_left_right = 0x7000;
    constexpr hcpp::u32 amp_set_in_left_right = 0xB000;
    constexpr hcpp::u32 amp_get_out_left = 0x2000;
    constexpr hcpp::u32 amp_get_in_left = 0xA000;
    constexpr hcpp::u32 amp_gain_mask = 0x7F;
    constexpr hcpp::u32 amp_mute_bit = 0x80;
    constexpr hcpp::u16 fmt_48k_s16_2ch = 0x0011;
    constexpr hcpp::u32 tone_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 tone_channels = 2;
    constexpr hcpp::u32 tone_pages = 16;
    constexpr hcpp::u32 capture_pages = 4;

    struct HdaCodec {
        hcpp::u8 address;
        hcpp::u32 vendor;
    };

    struct HdaController {
        hcpp::uintptr_t mmio;
        hcpp::u8 card_index;
        hcpp::u8 codec_mask;
        bool reset_done;
        hcpp::u32 corb_phys;
        hcpp::u32 rirb_phys;
        volatile hcpp::u32 *corb;
        volatile hcpp::u64 *rirb;
        hcpp::u16 rirb_wp;
        hcpp::u32 irq_count;
        hcpp::u32 intsts_last;
    };

    struct HdaPlayback {
        hcpp::u16 stream_offset;
        hcpp::u8 stream_number;
        hcpp::u8 codec;
        hcpp::u8 afg_node;
        hcpp::u8 dac_node;
        hcpp::u8 pin_node;
        hcpp::u8 connect_index;
        hcpp::u16 format;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 bdl_phys;
        hcpp::u16 bdl_entries;
        bool running;
    };

    struct HdaCapture {
        hcpp::u16 stream_offset;
        hcpp::u8 stream_number;
        hcpp::u8 codec;
        hcpp::u8 afg_node;
        hcpp::u8 adc_node;
        hcpp::u8 pin_node;
        hcpp::u8 connect_index;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 bdl_phys;
        hcpp::u16 bdl_entries;
        hcpp::u32 read_pos;
        bool running;
    };

    struct HdaTopology {
        hcpp::u8 function_groups;
        hcpp::u8 audio_groups;
        hcpp::u8 widgets;
        hcpp::u8 dac_widgets;
        hcpp::u8 adc_widgets;
        hcpp::u8 pin_widgets;
    };

    bool hda_bind(hcpp::size_t card_index, HdaController &controller) noexcept;
    bool hda_reset(HdaController &controller) noexcept;
    bool hda_codec_command(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                            hcpp::u32 command, hcpp::u32 &response) noexcept;
    bool hda_codec_verb(HdaController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept;
    hcpp::u8 hda_scan_codecs(HdaController &controller, HdaCodec *codecs, hcpp::u8 max) noexcept;
    bool hda_playback_start(HdaController &controller, hcpp::u8 codec,
                             HdaPlayback &playback) noexcept;
    hcpp::u32 hda_playback_position(HdaController &controller,
                                     const HdaPlayback &playback) noexcept;
    bool hda_capture_start(HdaController &controller, hcpp::u8 codec,
                           HdaCapture &capture) noexcept;
    hcpp::u32 hda_capture_position(HdaController &controller,
                                   const HdaCapture &capture) noexcept;
    hcpp::u32 hda_capture_read(HdaController &controller, HdaCapture &capture,
                               hcpp::i16 *buffer, hcpp::u32 frames) noexcept;
    bool hda_interrupts_enable(HdaController &controller, hcpp::u32 stream_mask) noexcept;
    hcpp::u32 hda_poll_interrupts(HdaController &controller) noexcept;
    bool hda_volume_set(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                        hcpp::u8 gain, bool mute) noexcept;
    bool hda_volume_get(HdaController &controller, hcpp::u8 codec, hcpp::u8 node,
                        hcpp::u8 &gain, bool &mute) noexcept;
    bool hda_topology_walk(HdaController &controller, hcpp::u8 codec,
                           HdaTopology &topology) noexcept;

    constexpr hcpp::u16 hda_format_invalid = 0xFFFFu;
    hcpp::u16 hda_format_word(hcpp::u32 rate, hcpp::u8 channels, hcpp::u8 bits) noexcept;
    bool hda_stream_open(HdaController &controller, hcpp::u8 codec, HdaPlayback &playback,
                         hcpp::u16 format, hcpp::u32 period_bytes,
                         hcpp::u16 bdl_entries) noexcept;
    bool hda_stream_run(HdaController &controller, HdaPlayback &playback) noexcept;
    bool hda_stream_halt(HdaController &controller, HdaPlayback &playback) noexcept;

}  // namespace hype::audio::hda
