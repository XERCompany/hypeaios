#pragma once

// AudioScience HPI codec/mixer definitions: audio formats, stream
// states, source/destination node types, mixer control types and the
// packed control-attribute ids used inside HpiControlMsg.attribute.
//
// Reference: Linux sound/pci/asihpi/hpi.h (HPI_FORMATS,
// HPI_SOURCENODES, HPI_DESTNODES, HPI_CONTROLS) and hpi_internal.h
// (HPI_CTL_ATTR encoding).

#include "hcpp/types.hpp"
#include "kernel/chips/asihpibase.h"

namespace hype::chips::asihpi {

    // ---- Audio format ids (HPI_FORMATS) -------------------------------
    constexpr hcpp::u16 hpi_format_mixer_native = 0;
    constexpr hcpp::u16 hpi_format_pcm8_unsigned = 1;
    constexpr hcpp::u16 hpi_format_pcm16_signed = 2;
    constexpr hcpp::u16 hpi_format_mpeg_l1 = 3;
    constexpr hcpp::u16 hpi_format_mpeg_l2 = 4;
    constexpr hcpp::u16 hpi_format_mpeg_l3 = 5;
    constexpr hcpp::u16 hpi_format_dolby_ac2 = 6;
    constexpr hcpp::u16 hpi_format_dolby_ac3 = 7;
    constexpr hcpp::u16 hpi_format_pcm16_bigendian = 8;
    constexpr hcpp::u16 hpi_format_aa_tagit1_hits = 9;
    constexpr hcpp::u16 hpi_format_aa_tagit1_inserts = 10;
    constexpr hcpp::u16 hpi_format_pcm32_signed = 11;
    constexpr hcpp::u16 hpi_format_raw_bitstream = 12;
    constexpr hcpp::u16 hpi_format_aa_tagit1_hits_ex1 = 13;
    constexpr hcpp::u16 hpi_format_pcm32_float = 14;
    constexpr hcpp::u16 hpi_format_pcm24_signed = 15;
    constexpr hcpp::u16 hpi_format_oem1 = 16;
    constexpr hcpp::u16 hpi_format_oem2 = 17;
    constexpr hcpp::u16 hpi_format_undefined = 0xFFFF;

    // Public-API format struct (padded wire variant is HpiMsgFormat).
    struct HpiFormat {
        hcpp::u32 sample_rate;     // 11025, 32000, 44100 ...
        hcpp::u32 bit_rate;        // MPEG bitrate
        hcpp::u32 attributes;      // stereo/mono flags
        hcpp::u16 mode_legacy;
        hcpp::u16 unused;
        hcpp::u16 channels;
        hcpp::u16 format;
    };

    constexpr hcpp::u32 hpi_format_attr_stereo = 0;
    constexpr hcpp::u32 hpi_format_attr_jointstereo = 1;
    constexpr hcpp::u32 hpi_format_attr_mono = 2;

    // ---- Stream states (HPI_STREAM_STATES) -----------------------------
    enum class HpiStreamState : hcpp::u16 {
        Stopped = 1,
        Playing = 2,
        Recording = 3,
        Drained = 4,
        Sinegen = 5,
        Wait = 6,
    };

    // ---- Mixer source node types (HPI_SOURCENODES) ---------------------
    constexpr hcpp::u16 hpi_sourcenode_none = 100;
    constexpr hcpp::u16 hpi_sourcenode_ostream = 101;
    constexpr hcpp::u16 hpi_sourcenode_linein = 102;
    constexpr hcpp::u16 hpi_sourcenode_aesebu_in = 103;
    constexpr hcpp::u16 hpi_sourcenode_tuner = 104;
    constexpr hcpp::u16 hpi_sourcenode_rf = 105;
    constexpr hcpp::u16 hpi_sourcenode_clock_source = 106;
    constexpr hcpp::u16 hpi_sourcenode_raw_bitstream = 107;
    constexpr hcpp::u16 hpi_sourcenode_microphone = 108;
    constexpr hcpp::u16 hpi_sourcenode_cobranet = 109;
    constexpr hcpp::u16 hpi_sourcenode_analog = 110;
    constexpr hcpp::u16 hpi_sourcenode_adapter = 111;
    constexpr hcpp::u16 hpi_sourcenode_rtp_destination = 112;
    constexpr hcpp::u16 hpi_sourcenode_internal = 113;
    constexpr hcpp::u16 hpi_sourcenode_avb = 114;
    constexpr hcpp::u16 hpi_sourcenode_blulink = 115;
    constexpr hcpp::u16 hpi_sourcenode_last_index = 115;

    // ---- Mixer destination node types (HPI_DESTNODES) -------------------
    constexpr hcpp::u16 hpi_destnode_none = 200;
    constexpr hcpp::u16 hpi_destnode_istream = 201;
    constexpr hcpp::u16 hpi_destnode_lineout = 202;
    constexpr hcpp::u16 hpi_destnode_aesebu_out = 203;
    constexpr hcpp::u16 hpi_destnode_rf = 204;
    constexpr hcpp::u16 hpi_destnode_speaker = 205;
    constexpr hcpp::u16 hpi_destnode_cobranet = 206;
    constexpr hcpp::u16 hpi_destnode_analog = 207;
    constexpr hcpp::u16 hpi_destnode_rtp_source = 208;
    constexpr hcpp::u16 hpi_destnode_avb = 209;
    constexpr hcpp::u16 hpi_destnode_internal = 210;
    constexpr hcpp::u16 hpi_destnode_blulink = 211;
    constexpr hcpp::u16 hpi_destnode_last_index = 211;

    // ---- Mixer control types (HPI_CONTROLS) ------------------------------
    constexpr hcpp::u16 hpi_control_generic = 0;
    constexpr hcpp::u16 hpi_control_connection = 1;
    constexpr hcpp::u16 hpi_control_volume = 2;
    constexpr hcpp::u16 hpi_control_meter = 3;
    constexpr hcpp::u16 hpi_control_mute = 4;
    constexpr hcpp::u16 hpi_control_multiplexer = 5;
    constexpr hcpp::u16 hpi_control_aesebu_transmitter = 6;
    constexpr hcpp::u16 hpi_control_aesebutx = 6;
    constexpr hcpp::u16 hpi_control_aesebu_receiver = 7;
    constexpr hcpp::u16 hpi_control_aeseburx = 7;
    constexpr hcpp::u16 hpi_control_level = 8;
    constexpr hcpp::u16 hpi_control_tuner = 9;
    constexpr hcpp::u16 hpi_control_vox = 11;
    constexpr hcpp::u16 hpi_control_channel_mode = 15;
    constexpr hcpp::u16 hpi_control_bitstream = 16;
    constexpr hcpp::u16 hpi_control_sampleclock = 17;
    constexpr hcpp::u16 hpi_control_microphone = 18;
    constexpr hcpp::u16 hpi_control_parametric_eq = 19;
    constexpr hcpp::u16 hpi_control_equalizer = 19;
    constexpr hcpp::u16 hpi_control_compander = 20;
    constexpr hcpp::u16 hpi_control_cobranet = 21;
    constexpr hcpp::u16 hpi_control_tonedetector = 22;
    constexpr hcpp::u16 hpi_control_silencedetector = 23;
    constexpr hcpp::u16 hpi_control_pad = 24;
    constexpr hcpp::u16 hpi_control_src = 25;
    constexpr hcpp::u16 hpi_control_universal = 26;
    constexpr hcpp::u16 hpi_control_last_index = 26;

    // ---- Control attribute packing (HPI_CTL_ATTR) -------------------------
    // attribute = (control_type << 8) | attribute_index; 256 ids per type.
    constexpr hcpp::u16 hpi_ctl_attr(hcpp::u16 control,
                                     hcpp::u16 index) noexcept {
        return static_cast<hcpp::u16>((control << 8) + index);
    }
    constexpr hcpp::u16 hpi_ctl_attr_index(hcpp::u16 attr) noexcept {
        return attr & 0xFF;
    }
    constexpr hcpp::u16 hpi_ctl_attr_control(hcpp::u16 attr) noexcept {
        return attr >> 8;
    }

    // Generic attributes present on every control.
    constexpr hcpp::u16 hpi_generic_enable =
        hpi_ctl_attr(hpi_control_generic, 1);
    constexpr hcpp::u16 hpi_generic_event_enable =
        hpi_ctl_attr(hpi_control_generic, 2);

    // Volume control attributes (dB in milli-Bel: 0.01 dB units).
    constexpr hcpp::u16 hpi_volume_gain =
        hpi_ctl_attr(hpi_control_volume, 1);
    constexpr hcpp::u16 hpi_volume_autofade =
        hpi_ctl_attr(hpi_control_volume, 2);
    constexpr hcpp::u16 hpi_volume_mute =
        hpi_ctl_attr(hpi_control_volume, 3);
    constexpr hcpp::u16 hpi_volume_gain_and_flags =
        hpi_ctl_attr(hpi_control_volume, 4);
    constexpr hcpp::u16 hpi_volume_num_channels =
        hpi_ctl_attr(hpi_control_volume, 6);
    constexpr hcpp::u16 hpi_volume_range =
        hpi_ctl_attr(hpi_control_volume, 10);

    constexpr hcpp::u32 hpi_volume_flag_muted = 1u << 0;
    constexpr hcpp::u32 hpi_volume_flag_has_mute = 1u << 1;
    constexpr hcpp::u32 hpi_volume_flag_has_autofade = 1u << 2;
    // Gains use 0.01 dB units: 0 = 0 dB, -10000 = -100 dB.
    constexpr hcpp::i32 hpi_gain_unity = 0;
    constexpr hcpp::i32 hpi_gain_min_db100 = -10000;

    // Meter control attributes.
    constexpr hcpp::u16 hpi_meter_rms = hpi_ctl_attr(hpi_control_meter, 1);
    constexpr hcpp::u16 hpi_meter_peak = hpi_ctl_attr(hpi_control_meter, 2);
    constexpr hcpp::u16 hpi_meter_rms_ballistics =
        hpi_ctl_attr(hpi_control_meter, 3);
    constexpr hcpp::u16 hpi_meter_peak_ballistics =
        hpi_ctl_attr(hpi_control_meter, 4);
    constexpr hcpp::u16 hpi_meter_num_channels =
        hpi_ctl_attr(hpi_control_meter, 5);

    // Multiplexer.
    constexpr hcpp::u16 hpi_multiplexer_source =
        hpi_ctl_attr(hpi_control_multiplexer, 1);
    constexpr hcpp::u16 hpi_multiplexer_querysource =
        hpi_ctl_attr(hpi_control_multiplexer, 2);

    // AES/EBU transmitter/receiver.
    constexpr hcpp::u16 hpi_aesebutx_format =
        hpi_ctl_attr(hpi_control_aesebutx, 1);
    constexpr hcpp::u16 hpi_aesebutx_samplerate =
        hpi_ctl_attr(hpi_control_aesebutx, 3);
    constexpr hcpp::u16 hpi_aesebutx_channelstatus =
        hpi_ctl_attr(hpi_control_aesebutx, 4);
    constexpr hcpp::u16 hpi_aesebutx_userdata =
        hpi_ctl_attr(hpi_control_aesebutx, 5);
    constexpr hcpp::u16 hpi_aeseburx_format =
        hpi_ctl_attr(hpi_control_aeseburx, 1);
    constexpr hcpp::u16 hpi_aeseburx_errorstatus =
        hpi_ctl_attr(hpi_control_aeseburx, 2);
    constexpr hcpp::u16 hpi_aeseburx_samplerate =
        hpi_ctl_attr(hpi_control_aeseburx, 3);
    constexpr hcpp::u16 hpi_aeseburx_channelstatus =
        hpi_ctl_attr(hpi_control_aeseburx, 4);
    constexpr hcpp::u16 hpi_aeseburx_userdata =
        hpi_ctl_attr(hpi_control_aeseburx, 5);

    // Level/trim (dBu domain).
    constexpr hcpp::u16 hpi_level_gain = hpi_ctl_attr(hpi_control_level, 1);
    constexpr hcpp::u16 hpi_level_range =
        hpi_ctl_attr(hpi_control_level, 10);

    // Tuner.
    constexpr hcpp::u16 hpi_tuner_band = hpi_ctl_attr(hpi_control_tuner, 1);
    constexpr hcpp::u16 hpi_tuner_freq = hpi_ctl_attr(hpi_control_tuner, 2);
    constexpr hcpp::u16 hpi_tuner_level_avg =
        hpi_ctl_attr(hpi_control_tuner, 3);
    constexpr hcpp::u16 hpi_tuner_level_raw =
        hpi_ctl_attr(hpi_control_tuner, 4);
    constexpr hcpp::u16 hpi_tuner_snr = hpi_ctl_attr(hpi_control_tuner, 5);
    constexpr hcpp::u16 hpi_tuner_gain = hpi_ctl_attr(hpi_control_tuner, 6);
    constexpr hcpp::u16 hpi_tuner_status =
        hpi_ctl_attr(hpi_control_tuner, 7);
    constexpr hcpp::u16 hpi_tuner_mode = hpi_ctl_attr(hpi_control_tuner, 8);
    constexpr hcpp::u16 hpi_tuner_rds = hpi_ctl_attr(hpi_control_tuner, 9);
    constexpr hcpp::u16 hpi_tuner_deemphasis =
        hpi_ctl_attr(hpi_control_tuner, 10);
    constexpr hcpp::u16 hpi_tuner_program =
        hpi_ctl_attr(hpi_control_tuner, 11);

    // Vox / channel-mode / bitstream / sampleclock / microphone.
    constexpr hcpp::u16 hpi_vox_threshold =
        hpi_ctl_attr(hpi_control_vox, 1);
    constexpr hcpp::u16 hpi_channel_mode_mode =
        hpi_ctl_attr(hpi_control_channel_mode, 1);
    constexpr hcpp::u16 hpi_bitstream_data_polarity =
        hpi_ctl_attr(hpi_control_bitstream, 1);
    constexpr hcpp::u16 hpi_bitstream_clock_edge =
        hpi_ctl_attr(hpi_control_bitstream, 2);
    constexpr hcpp::u16 hpi_bitstream_clock_source =
        hpi_ctl_attr(hpi_control_bitstream, 3);
    constexpr hcpp::u16 hpi_bitstream_activity =
        hpi_ctl_attr(hpi_control_bitstream, 4);
    constexpr hcpp::u16 hpi_sampleclock_source =
        hpi_ctl_attr(hpi_control_sampleclock, 1);
    constexpr hcpp::u16 hpi_sampleclock_samplerate =
        hpi_ctl_attr(hpi_control_sampleclock, 2);
    constexpr hcpp::u16 hpi_sampleclock_source_index =
        hpi_ctl_attr(hpi_control_sampleclock, 3);
    constexpr hcpp::u16 hpi_sampleclock_local_samplerate =
        hpi_ctl_attr(hpi_control_sampleclock, 4);
    constexpr hcpp::u16 hpi_sampleclock_auto =
        hpi_ctl_attr(hpi_control_sampleclock, 5);
    constexpr hcpp::u16 hpi_sampleclock_local_lock =
        hpi_ctl_attr(hpi_control_sampleclock, 6);
    constexpr hcpp::u16 hpi_microphone_phantom_power =
        hpi_ctl_attr(hpi_control_microphone, 1);

    // Parametric EQ / compander / tone+silence detectors.
    constexpr hcpp::u16 hpi_equalizer_num_filters =
        hpi_ctl_attr(hpi_control_equalizer, 1);
    constexpr hcpp::u16 hpi_equalizer_filter =
        hpi_ctl_attr(hpi_control_equalizer, 2);
    constexpr hcpp::u16 hpi_equalizer_coefficients =
        hpi_ctl_attr(hpi_control_equalizer, 3);
    constexpr hcpp::u16 hpi_compander_params =
        hpi_ctl_attr(hpi_control_compander, 1);
    constexpr hcpp::u16 hpi_compander_makeupgain =
        hpi_ctl_attr(hpi_control_compander, 2);
    constexpr hcpp::u16 hpi_compander_threshold =
        hpi_ctl_attr(hpi_control_compander, 3);
    constexpr hcpp::u16 hpi_compander_ratio =
        hpi_ctl_attr(hpi_control_compander, 4);
    constexpr hcpp::u16 hpi_compander_attack =
        hpi_ctl_attr(hpi_control_compander, 5);
    constexpr hcpp::u16 hpi_compander_decay =
        hpi_ctl_attr(hpi_control_compander, 6);
    constexpr hcpp::u16 hpi_tonedetector_threshold =
        hpi_ctl_attr(hpi_control_tonedetector, 1);
    constexpr hcpp::u16 hpi_tonedetector_state =
        hpi_ctl_attr(hpi_control_tonedetector, 2);
    constexpr hcpp::u16 hpi_tonedetector_frequency =
        hpi_ctl_attr(hpi_control_tonedetector, 3);
    constexpr hcpp::u16 hpi_silencedetector_threshold =
        hpi_ctl_attr(hpi_control_silencedetector, 1);
    constexpr hcpp::u16 hpi_silencedetector_state =
        hpi_ctl_attr(hpi_control_silencedetector, 2);
    constexpr hcpp::u16 hpi_silencedetector_delay =
        hpi_ctl_attr(hpi_control_silencedetector, 3);
    constexpr hcpp::u16 hpi_universal_entity =
        hpi_ctl_attr(hpi_control_universal, 1);

    // Tuner PAD attributes.
    constexpr hcpp::u16 hpi_pad_channel_name =
        hpi_ctl_attr(hpi_control_pad, 1);
    constexpr hcpp::u16 hpi_pad_artist = hpi_ctl_attr(hpi_control_pad, 2);
    constexpr hcpp::u16 hpi_pad_title = hpi_ctl_attr(hpi_control_pad, 3);
    constexpr hcpp::u16 hpi_pad_comment = hpi_ctl_attr(hpi_control_pad, 4);
    constexpr hcpp::u16 hpi_pad_program_type =
        hpi_ctl_attr(hpi_control_pad, 5);
    constexpr hcpp::u16 hpi_pad_program_id =
        hpi_ctl_attr(hpi_control_pad, 6);
    constexpr hcpp::u16 hpi_pad_ta_support =
        hpi_ctl_attr(hpi_control_pad, 7);
    constexpr hcpp::u16 hpi_pad_ta_active =
        hpi_ctl_attr(hpi_control_pad, 8);

    // ---- Sample-clock sources ------------------------------------------
    constexpr hcpp::u16 hpi_sampleclock_source_adapter = 1;
    constexpr hcpp::u16 hpi_sampleclock_source_aesebu_sync = 2;
    constexpr hcpp::u16 hpi_sampleclock_source_word_external = 3;
    constexpr hcpp::u16 hpi_sampleclock_source_word_header = 4;
    constexpr hcpp::u16 hpi_sampleclock_source_smp = 5;
    constexpr hcpp::u16 hpi_sampleclock_source_aesebu_in = 6;
    constexpr hcpp::u16 hpi_sampleclock_source_network = 8;
    constexpr hcpp::u16 hpi_sampleclock_source_prev_adapter = 10;

}  // namespace hype::chips::asihpi
