#include "kernel/display/edidcta.h"

#include "hcpp/display/edid.hpp"

namespace hype::display {

    namespace {

        constexpr hcpp::u8 cta_tag = 0x02;
        constexpr hcpp::u8 block_audio = 1;
        constexpr hcpp::u8 block_video = 2;
        constexpr hcpp::u8 block_vendor = 3;
        constexpr hcpp::u8 block_speaker = 4;

        void parse_video_block(const hcpp::u8* payload, hcpp::u8 length, CtaInfo& info) noexcept {
            for (hcpp::u8 i = 0; i < length && info.video_count < cta_max_video_timings; ++i) {
                CtaVideoTiming& timing = info.videos[info.video_count];
                timing.vic = payload[i] & 0x7F;
                timing.native = (payload[i] & 0x80) != 0;
                ++info.video_count;
            }
        }

        void parse_audio_block(const hcpp::u8* payload, hcpp::u8 length, CtaInfo& info) noexcept {
            for (hcpp::u8 i = 0; i + 2 < length && info.audio_count < cta_max_audio_formats; i += 3) {
                CtaAudioFormat& format = info.audios[info.audio_count];
                format.format_code = (payload[i] >> 3) & 0x0F;
                format.max_channels = (payload[i] & 0x07) + 1;
                format.sample_rates = payload[i + 1];
                format.detail = payload[i + 2];
                ++info.audio_count;
            }
        }

        void parse_speaker_block(const hcpp::u8* payload, hcpp::u8 length, CtaInfo& info) noexcept {
            if (length < 3) {
                return;
            }
            info.speaker_present = true;
            info.speaker_map = payload[0];
        }

        void parse_vendor_block(const hcpp::u8* payload, hcpp::u8 length, CtaInfo& info) noexcept {
            if (length < 3) {
                return;
            }
            const bool hdmi_oui = payload[0] == 0x03 && payload[1] == 0x0C && payload[2] == 0x00;
            if (!hdmi_oui) {
                return;
            }
            info.hdmi_present = true;
            if (length >= 5) {
                info.hdmi_phys_address = static_cast<hcpp::u16>((payload[3] << 8) | payload[4]);
            }
            if (length >= 6) {
                const hcpp::u8 flags = payload[5];
                info.hdmi_ai = (flags & 0x80) != 0;
                info.hdmi_dc_36 = (flags & 0x20) != 0;
                info.hdmi_dc_30 = (flags & 0x10) != 0;
                info.hdmi_dc_y444 = (flags & 0x08) != 0;
            }
            if (length >= 7) {
                info.hdmi_max_tmds_mhz = static_cast<hcpp::u8>(payload[6] * 5);
            }
        }

        void parse_data_block(const hcpp::u8* data_block, hcpp::u8 length, CtaInfo& info) noexcept {
            const hcpp::u8 tag = data_block[0] >> 5;
            const hcpp::u8* payload = data_block + 1;
            const hcpp::u8 payload_length = length;
            switch (tag) {
                case block_video:
                    parse_video_block(payload, payload_length, info);
                    break;
                case block_audio:
                    parse_audio_block(payload, payload_length, info);
                    break;
                case block_vendor:
                    parse_vendor_block(payload, payload_length, info);
                    break;
                case block_speaker:
                    parse_speaker_block(payload, payload_length, info);
                    break;
                default:
                    break;
            }
        }

    }  // namespace

    bool cta_parse(const hcpp::u8* ext, CtaInfo& info) noexcept {
        info = CtaInfo{};
        if (ext == nullptr || ext[0] != cta_tag) {
            return false;
        }
        info.checksum_ok = hcpp::display::edid_checksum_ok(ext);
        info.present = true;
        info.revision = ext[1];
        info.dtd_offset = ext[2];
        info.underscan = (ext[3] & 0x80) != 0;
        info.basic_audio = (ext[3] & 0x40) != 0;
        info.ycbcr444 = (ext[3] & 0x20) != 0;
        info.ycbcr422 = (ext[3] & 0x10) != 0;

        hcpp::u32 end = info.dtd_offset;
        if (end == 0 || end > 127) {
            end = 127;
        }
        hcpp::u32 position = 4;
        while (position < end && info.data_block_count < cta_max_data_blocks) {
            const hcpp::u8 length = ext[position] & 0x1F;
            if (length == 0 || position + length > 127) {
                break;
            }
            parse_data_block(ext + position, length, info);
            ++info.data_block_count;
            position += static_cast<hcpp::u32>(length) + 1;
        }
        return true;
    }

    bool cta_has_vic(const CtaInfo& info, hcpp::u8 vic) noexcept {
        for (hcpp::u8 i = 0; i < info.video_count; ++i) {
            if (info.videos[i].vic == vic) {
                return true;
            }
        }
        return false;
    }

    hcpp::u8 cta_native_vic(const CtaInfo& info) noexcept {
        for (hcpp::u8 i = 0; i < info.video_count; ++i) {
            if (info.videos[i].native) {
                return info.videos[i].vic;
            }
        }
        return 0;
    }

    const char* cta_audio_codec_name(hcpp::u8 format_code) noexcept {
        switch (format_code) {
            case 1:
                return "lpcm";
            case 2:
                return "ac3";
            case 3:
                return "mpeg1";
            case 4:
                return "mp3";
            case 5:
                return "mpeg2";
            case 6:
                return "aac-lc";
            case 7:
                return "dts";
            case 8:
                return "atrac";
            case 10:
                return "e-ac3";
            case 11:
                return "dts-hd";
            case 12:
                return "mlp";
            case 13:
                return "dst";
            case 14:
                return "wma-pro";
            default:
                return "unknown";
        }
    }

}  // namespace hype::display
