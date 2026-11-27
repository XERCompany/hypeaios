#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    constexpr hcpp::u8 cta_max_video_timings = 32;
    constexpr hcpp::u8 cta_max_audio_formats = 10;
    constexpr hcpp::u8 cta_max_data_blocks = 24;

    struct CtaVideoTiming {
        hcpp::u8 vic = 0;
        bool native = false;
    };

    struct CtaAudioFormat {
        hcpp::u8 format_code = 0;
        hcpp::u8 max_channels = 0;
        hcpp::u8 sample_rates = 0;
        hcpp::u8 detail = 0;
    };

    struct CtaInfo {
        bool present = false;
        bool checksum_ok = false;
        hcpp::u8 revision = 0;
        hcpp::u8 dtd_offset = 0;
        bool underscan = false;
        bool basic_audio = false;
        bool ycbcr444 = false;
        bool ycbcr422 = false;
        hcpp::u8 data_block_count = 0;
        hcpp::u8 video_count = 0;
        CtaVideoTiming videos[cta_max_video_timings]{};
        hcpp::u8 audio_count = 0;
        CtaAudioFormat audios[cta_max_audio_formats]{};
        bool speaker_present = false;
        hcpp::u8 speaker_map = 0;
        bool hdmi_present = false;
        hcpp::u16 hdmi_phys_address = 0;
        bool hdmi_ai = false;
        bool hdmi_dc_36 = false;
        bool hdmi_dc_30 = false;
        bool hdmi_dc_y444 = false;
        hcpp::u8 hdmi_max_tmds_mhz = 0;
    };

    bool cta_parse(const hcpp::u8* ext, CtaInfo& info) noexcept;
    bool cta_has_vic(const CtaInfo& info, hcpp::u8 vic) noexcept;
    const char* cta_audio_codec_name(hcpp::u8 format_code) noexcept;
    hcpp::u8 cta_native_vic(const CtaInfo& info) noexcept;

}  // namespace hype::display
