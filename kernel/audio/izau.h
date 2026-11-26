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
    constexpr hcpp::u32 verb_get_parameter = 0xF00;
    constexpr hcpp::u32 param_vendor_id = 0x00;
    constexpr hcpp::u32 param_node_count = 0x04;
    constexpr hcpp::u32 param_function_type = 0x05;
    constexpr hcpp::u32 param_widget_caps = 0x09;
    constexpr hcpp::u32 param_pin_caps = 0x0C;
    constexpr hcpp::u32 param_connlist_len = 0x0E;
    constexpr hcpp::u32 param_amp_out_caps = 0x12;
    constexpr hcpp::u8 function_group_audio = 0x01;
    constexpr hcpp::u8 widget_audio_output = 0x0;
    constexpr hcpp::u8 widget_pin_complex = 0x4;
    constexpr hcpp::u32 widget_cap_connlist = 1u << 8;
    constexpr hcpp::u32 widget_cap_out_amp = 1u << 2;
    constexpr hcpp::u32 pin_cap_output = 1u << 4;
    constexpr hcpp::u32 pin_cap_eapd = 1u << 16;
    constexpr hcpp::u8 pin_ctl_out_enable = 0x40;
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
    constexpr hcpp::u32 amp_set_out_left_right = 0x7000;
    constexpr hcpp::u16 fmt_48k_s16_2ch = 0x0011;
    constexpr hcpp::u32 tone_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 tone_channels = 2;
    constexpr hcpp::u32 tone_pages = 16;

    struct IzauCodec {
        hcpp::u8 address;
        hcpp::u32 vendor;
    };

    struct IzauController {
        hcpp::uintptr_t mmio;
        hcpp::u8 card_index;
        hcpp::u8 codec_mask;
        bool reset_done;
        hcpp::u32 corb_phys;
        hcpp::u32 rirb_phys;
        volatile hcpp::u32 *corb;
        volatile hcpp::u64 *rirb;
        hcpp::u16 rirb_wp;
    };

    struct IzauPlayback {
        hcpp::u16 stream_offset;
        hcpp::u8 stream_number;
        hcpp::u8 codec;
        hcpp::u8 afg_node;
        hcpp::u8 dac_node;
        hcpp::u8 pin_node;
        hcpp::u8 connect_index;
        hcpp::u32 buffer_phys;
        hcpp::u32 buffer_bytes;
        hcpp::u32 bdl_phys;
        hcpp::u16 bdl_entries;
        bool running;
    };

    bool izau_bind(hcpp::size_t card_index, IzauController &controller) noexcept;
    bool izau_reset(IzauController &controller) noexcept;
    bool izau_codec_command(IzauController &controller, hcpp::u8 codec, hcpp::u8 node,
                            hcpp::u32 command, hcpp::u32 &response) noexcept;
    bool izau_codec_verb(IzauController &controller, hcpp::u8 codec, hcpp::u8 node, hcpp::u32 verb,
                         hcpp::u8 payload, hcpp::u32 &response) noexcept;
    hcpp::u8 izau_scan_codecs(IzauController &controller, IzauCodec *codecs, hcpp::u8 max) noexcept;
    bool izau_playback_start(IzauController &controller, hcpp::u8 codec,
                             IzauPlayback &playback) noexcept;
    hcpp::u32 izau_playback_position(IzauController &controller,
                                     const IzauPlayback &playback) noexcept;

}  // namespace hype::audio::hda
