#include "kernel/display/selftest.h"

#include "hcpp/display/edid.hpp"
#include "hcpp/display/hdr.hpp"
#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/display_huxga.h"
#include "kernel/display/display_qxga.h"
#include "kernel/display/display_wvga.h"
#include "kernel/display/displaydpi.h"
#include "kernel/display/displayhda.h"
#include "kernel/display/displaysize.h"
#include "kernel/display/edidcta.h"
#include "kernel/display/hgc.h"
#include "kernel/display/id-stat.h"
#include "kernel/display/idscreen.h"
#include "kernel/display/pal.h"
#include "kernel/display/scaler.h"
#include "kernel/display/scaleuhd.h"
#include "kernel/display/uhdtv2.h"
#include "kernel/display/uhdtv2-vision.h"
#include "kernel/display/urecordscr.h"
#include "kernel/display/writeframe.h"
#include "kernel/serial.hpp"

namespace hype::display {

    namespace {

        bool channel_near(hcpp::u32 actual, hcpp::i32 expected, hcpp::i32 tolerance) noexcept {
            const hcpp::i32 diff = static_cast<hcpp::i32>(actual) - expected;
            return diff >= -tolerance && diff <= tolerance;
        }

        hcpp::i32 clamp_expected(hcpp::i32 value, hcpp::i32 hi) noexcept {
            if (value < 0) {
                return 0;
            }
            return value > hi ? hi : value;
        }

        bool modes_selftest() noexcept {
            if (mode_count() < 8) {
                return false;
            }
            hcpp::u64 previous_pixels = 0;
            for (hcpp::u32 i = 0; i < mode_count(); ++i) {
                const DisplayMode* mode = mode_at(i);
                if (mode == nullptr || mode->width == 0 || mode->height == 0 || mode->hz == 0) {
                    return false;
                }
                const hcpp::u64 pixels =
                    static_cast<hcpp::u64>(mode->width) * mode->height;
                if (pixels < previous_pixels) {
                    return false;
                }
                previous_pixels = pixels;
            }
            if (mode_at(mode_count()) != nullptr) {
                return false;
            }
            if (find_mode(640, 480) == nullptr || find_mode(800, 480) == nullptr ||
                find_mode(1024, 768) == nullptr || find_mode(2048, 1536) == nullptr ||
                find_mode(3840, 2160) == nullptr || find_mode(7680, 4320) == nullptr) {
                return false;
            }
            if (find_mode(123, 456) != nullptr) {
                return false;
            }
            const DisplayMode* wvga = find_mode(800, 480);
            const DisplayMode* qxga = find_mode(2048, 1536);
            const DisplayMode* huxga = find_mode(6400, 4800);
            if (wvga == nullptr || qxga == nullptr || huxga == nullptr) {
                return false;
            }
            if (wvga->timing_id != timing_wvga || qxga->timing_id != timing_qxga ||
                huxga->timing_id != timing_huxga) {
                return false;
            }
            if (wvga_pixel_clock_khz() == 0 || qxga_pixel_clock_khz() == 0 ||
                huxga_pixel_clock_khz() == 0) {
                return false;
            }
            if (uhd_tier(1920, 1080) != UhdTier::IntegerFast ||
                uhd_tier(3840, 2160) != UhdTier::Native ||
                uhd_tier(1366, 768) != UhdTier::EdgePreserve ||
                uhd_tier(7680, 4320) != UhdTier::Downscale) {
                return false;
            }
            return true;
        }

        bool scaler_selftest() noexcept {
            static hcpp::u32 src[32 * 32];
            static hcpp::u32 dst[64 * 64];
            for (hcpp::u32 y = 0; y < 32; ++y) {
                for (hcpp::u32 x = 0; x < 32; ++x) {
                    const hcpp::u32 red = x * 8;
                    const hcpp::u32 green = y * 8;
                    const hcpp::u32 blue = (x + y) * 4;
                    src[y * 32 + x] = 0xFF000000u | (red << 16) | (green << 8) | blue;
                }
            }
            const ScaleRect full_src{0, 0, 32, 32};
            const ScaleRect full_dst{0, 0, 64, 64};
            if (!scale_bilinear(src, 32, 32, 32, full_src, dst, 64, 64, 64, full_dst)) {
                return false;
            }
            for (hcpp::u32 y = 0; y < 64; ++y) {
                for (hcpp::u32 x = 0; x < 64; ++x) {
                    const hcpp::u32 pixel = dst[y * 64 + x];
                    const hcpp::u32 red = (pixel >> 16) & 0xFF;
                    const hcpp::u32 green = (pixel >> 8) & 0xFF;
                    const hcpp::u32 blue = pixel & 0xFF;
                    if (!channel_near(red, clamp_expected(4 * static_cast<hcpp::i32>(x) - 2, 248), 4) ||
                        !channel_near(green, clamp_expected(4 * static_cast<hcpp::i32>(y) - 2, 248), 4) ||
                        !channel_near(blue, clamp_expected(2 * static_cast<hcpp::i32>(x + y) - 2, 252), 4)) {
                        return false;
                    }
                }
            }
            static hcpp::u32 nn_dst[64 * 64];
            if (!scale_auto(src, 32, 32, 32, full_src, nn_dst, 64, 64, 64, full_dst)) {
                return false;
            }
            for (hcpp::u32 y = 0; y < 64; ++y) {
                for (hcpp::u32 x = 0; x < 64; ++x) {
                    if (nn_dst[y * 64 + x] != src[(y / 2) * 32 + (x / 2)]) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool writeframe_selftest() noexcept {
            static hcpp::u8 buf_a[64 * 48 * 4];
            static hcpp::u8 buf_b[64 * 48 * 4];
            static hcpp::u8 buf_c[64 * 48 * 4];
            static hcpp::u8 buf_d[32 * 24 * 4];
            static hcpp::u8 buf_e[64 * 48 * 4];
            hcpp::graphics::Framebuffer fb_a(buf_a, 64, 48, 64 * 4, 32, 8, 16, 8, 8, 8, 0);
            hcpp::graphics::Framebuffer fb_b(buf_b, 64, 48, 64 * 4, 32, 8, 0, 8, 8, 8, 16);
            hcpp::graphics::Framebuffer fb_c(buf_c, 64, 48, 64 * 4, 32, 8, 16, 8, 8, 8, 0);
            hcpp::graphics::Framebuffer fb_d(buf_d, 32, 24, 32 * 4, 32, 8, 16, 8, 8, 8, 0);
            hcpp::graphics::Framebuffer fb_e(buf_e, 64, 48, 64 * 4, 32, 8, 0, 8, 8, 8, 16);
            for (hcpp::u32 y = 0; y < 48; ++y) {
                for (hcpp::u32 x = 0; x < 64; ++x) {
                    const hcpp::u8 red = static_cast<hcpp::u8>(x * 4);
                    const hcpp::u8 green = static_cast<hcpp::u8>(y * 5);
                    const hcpp::u8 blue = static_cast<hcpp::u8>((x + y) * 3);
                    fb_a.put_pixel(x, y, fb_a.encode_rgb(red, green, blue));
                }
            }
            frame_stats_reset();
            if (!write_frame(fb_b, fb_a, WriteFrameOpts{})) {
                return false;
            }
            for (hcpp::u32 y = 0; y < 48; ++y) {
                for (hcpp::u32 x = 0; x < 64; ++x) {
                    hcpp::u8 ra = 0;
                    hcpp::u8 ga = 0;
                    hcpp::u8 ba = 0;
                    hcpp::u8 rb = 0;
                    hcpp::u8 gb = 0;
                    hcpp::u8 bb = 0;
                    fb_a.decode_rgb(fb_a.get_pixel(x, y), ra, ga, ba);
                    fb_b.decode_rgb(fb_b.get_pixel(x, y), rb, gb, bb);
                    if (ra != rb || ga != gb || ba != bb) {
                        return false;
                    }
                }
            }
            WriteFrameOpts opts{};
            opts.filter = ScaleFilter::Bilinear;
            opts.dst_w = 32;
            opts.dst_h = 24;
            if (!write_frame(fb_d, fb_a, opts)) {
                return false;
            }
            {
                hcpp::u8 red = 0;
                hcpp::u8 green = 0;
                hcpp::u8 blue = 0;
                fb_d.decode_rgb(fb_d.get_pixel(31, 23), red, green, blue);
                if (!channel_near(red, 250, 8) || !channel_near(green, 232, 8)) {
                    return false;
                }
            }
            if (!hgc_init() || !hgc_ready() || hgc_mode_count() != mode_count()) {
                return false;
            }
            if (hgc_timing_for(timing_wvga) != wvga_timing() ||
                hgc_timing_for(timing_qxga) != qxga_timing() ||
                hgc_timing_for(timing_huxga) != huxga_timing()) {
                return false;
            }
            if (!hgc_present(fb_e, fb_b)) {
                return false;
            }
            if (hgc_present_count() == 0 || frame_stats().frames < 3) {
                return false;
            }
            for (hcpp::u32 y = 0; y < 48; ++y) {
                for (hcpp::u32 x = 0; x < 64; ++x) {
                    hcpp::u8 ra = 0;
                    hcpp::u8 ga = 0;
                    hcpp::u8 ba = 0;
                    hcpp::u8 re = 0;
                    hcpp::u8 ge = 0;
                    hcpp::u8 be = 0;
                    fb_a.decode_rgb(fb_a.get_pixel(x, y), ra, ga, ba);
                    fb_e.decode_rgb(fb_e.get_pixel(x, y), re, ge, be);
                    if (ra != re || ga != ge || ba != be) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool pal_selftest() noexcept {
            if (!pal_sane()) {
                return false;
            }
            if (pal_timing()->vactive != pal_active_lines ||
                pal_timing_block.hfront + pal_timing_block.hsync + pal_timing_block.hback !=
                    pal_total_samples - pal_active_samples) {
                return false;
            }
            if (pal_pixel_clock_khz() != pal_clock_khz ||
                pal_clock_khz * 1000 != pal_line_rate_hz * pal_total_samples) {
                return false;
            }
            return true;
        }

        bool uhd_selftest(hcpp::u32& count_out) noexcept {
            count_out = uhd2_mode_count();
            if (count_out < 10 || uhd2_mode_at(count_out) != nullptr) {
                return false;
            }
            for (hcpp::u32 i = 0; i < count_out; ++i) {
                const Uhd2Mode* mode = uhd2_mode_at(i);
                if (mode == nullptr || !uhd2_is_uhd(mode->width, mode->height) ||
                    mode->hz == 0 || uhd_find_vic(mode->vic) != mode) {
                    return false;
                }
            }
            if (uhd_find(3840, 2160, 60) == nullptr ||
                uhd_find(3840, 2160, 60)->vic != vic_uhd4k_60 ||
                uhd_find(7680, 4320, 30) == nullptr ||
                uhd_find(7680, 4320, 30)->vic != vic_uhd8k_30 ||
                uhd_find(3840, 2160, 24) == nullptr ||
                uhd_find(3840, 2160, 24)->vic != vic_uhd4k_24) {
                return false;
            }
            if (uhd_find(1920, 1080, 60) != nullptr || uhd_find(3840, 2160, 120) != nullptr ||
                uhd_find_vic(1) != nullptr) {
                return false;
            }
            return true;
        }

        bool hdr_selftest() noexcept {
            if (!uhd_primaries_equal(bt2020_primaries,
                                     UhdPrimaries{35400, 14600, 8500, 39850, 6550, 2300,
                                                  15635, 16450})) {
                return false;
            }
            if (static_cast<hcpp::u8>(UhdTransfer::Pq) != 16 ||
                static_cast<hcpp::u8>(UhdTransfer::Hlg) != 18) {
                return false;
            }
            if (!hcpp::display::hdr_eotf_pq_supported(1u << 2) ||
                hcpp::display::hdr_eotf_pq_supported(1u << 3) ||
                !hcpp::display::hdr_eotf_hlg_supported(1u << 3)) {
                return false;
            }
            const UhdHdr10Meta meta = uhd_hdr10_default();
            hcpp::u8 packed[uhd_hdr10_block_size];
            if (uhd_hdr10_pack(meta, packed, uhd_hdr10_block_size - 1) ||
                !uhd_hdr10_pack(meta, packed, sizeof(packed))) {
                return false;
            }
            UhdHdr10Meta parsed{};
            if (!uhd_hdr10_parse(packed, sizeof(packed), parsed) ||
                uhd_hdr10_parse(packed, 8, parsed) || !uhd_hdr10_equal(meta, parsed)) {
                return false;
            }
            return true;
        }

        bool audio_bridge_selftest() noexcept {
            if (!displayhda_bind_widget(0, 2) || displayhda_widget_count() == 0) {
                return false;
            }
            if (!displayhda_route(0, 1) || displayhda_route_count() == 0) {
                return false;
            }
            const DisplayAudioRoute* route = displayhda_route_at(0);
            if (route == nullptr || route->output_id != 0 || route->stream_id != 1 ||
                !route->active) {
                return false;
            }
            if (!displayhda_route(0, 3)) {
                return false;
            }
            if (!displayhda_unroute(0) || displayhda_find_route(0) != nullptr) {
                return false;
            }
            audio::hda::IzauController empty{};
            if (displayhda_route(empty, *route)) {
                return false;
            }
            return true;
        }

        void edid_fix_checksum(hcpp::u8* block) noexcept {
            hcpp::u8 sum = 0;
            for (hcpp::u32 i = 0; i < 127; ++i) {
                sum = static_cast<hcpp::u8>(sum + block[i]);
            }
            block[127] = static_cast<hcpp::u8>(0 - sum);
        }

        void build_synthetic_edid(hcpp::u8* edid) noexcept {
            for (hcpp::u32 i = 0; i < 128; ++i) {
                edid[i] = 0;
            }
            edid[0] = 0x00;
            for (hcpp::u32 i = 1; i < 7; ++i) {
                edid[i] = 0xFF;
            }
            edid[7] = 0x00;
            edid[8] = 0x22;
            edid[9] = 0x09;
            edid[10] = 0x04;
            edid[11] = 0xD2;
            edid[12] = 0x04;
            edid[13] = 0x03;
            edid[14] = 0x02;
            edid[15] = 0x01;
            edid[16] = 1;
            edid[17] = 34;
            edid[18] = 1;
            edid[19] = 4;
            edid[20] = 0xA5;
            edid[21] = 52;
            edid[22] = 29;
            edid[23] = 120;
            edid[24] = 0x02;
            edid[126] = 1;
            edid_fix_checksum(edid);
        }

        void build_synthetic_cta(hcpp::u8* cta) noexcept {
            for (hcpp::u32 i = 0; i < 128; ++i) {
                cta[i] = 0;
            }
            cta[0] = 0x02;
            cta[1] = 0x03;
            cta[3] = 0x70;
            hcpp::u32 position = 4;
            cta[position++] = 0x43;
            cta[position++] = 0x84;
            cta[position++] = 0x10;
            cta[position++] = 0x1F;
            cta[position++] = 0x26;
            cta[position++] = 0x09;
            cta[position++] = 0x07;
            cta[position++] = 0x07;
            cta[position++] = 0x15;
            cta[position++] = 0x07;
            cta[position++] = 0x50;
            cta[position++] = 0x83;
            cta[position++] = 0x4F;
            cta[position++] = 0x00;
            cta[position++] = 0x00;
            cta[position++] = 0x68;
            cta[position++] = 0x03;
            cta[position++] = 0x0C;
            cta[position++] = 0x00;
            cta[position++] = 0x10;
            cta[position++] = 0x00;
            cta[position++] = 0xA8;
            cta[position++] = 0x28;
            cta[position++] = 0x00;
            cta[2] = static_cast<hcpp::u8>(position);
            edid_fix_checksum(cta);
        }

        bool output_id_selftest(const hcpp::u8* edid) noexcept {
            const OutputId id0 = idscreen_register(edid, 0);
            const OutputId id1 = idscreen_register(edid, 1);
            if (id0 == output_id_none || id1 == output_id_none || id0 == id1) {
                return false;
            }
            if (idscreen_register(edid, 0) != id0 || idscreen_register(edid, 1) != id1) {
                return false;
            }
            if (idscreen_id_for(edid, 0) != id0 || idscreen_id_for(edid, 2) == id0) {
                return false;
            }
            if (idscreen_count() != 2) {
                return false;
            }
            const OutputDescriptor* first = idscreen_find(id0);
            const OutputDescriptor* second = idscreen_find(id1);
            if (first == nullptr || second == nullptr) {
                return false;
            }
            if (first->connector != 0 || second->connector != 1) {
                return false;
            }
            if (first->product_code != 0xD204 || first->serial_number != 0x01020304) {
                return false;
            }
            if (first->manufacturer[0] != 'H' || first->manufacturer[1] != 'P' ||
                first->manufacturer[2] != 'I') {
                return false;
            }
            if (idscreen_find(0xDEADBEEFull) != nullptr || idscreen_at(0) != first ||
                idscreen_at(1) != second || idscreen_at(2) != nullptr) {
                return false;
            }
            if (idscreen_register(nullptr, 0) != output_id_none) {
                return false;
            }
            OutputStatus* stat = output_stat_for(id0);
            if (stat == nullptr || stat->output_id != id0 || !stat->valid) {
                return false;
            }
            if (!output_stat_set_link(id0, OutputLink::Up) ||
                !output_stat_set_mode(id0, 13)) {
                return false;
            }
            if (!output_stat_present(id0, 42) || !output_stat_present(id0, 100)) {
                return false;
            }
            const OutputStatus* readback = output_stat_read(id0);
            if (readback == nullptr || readback->frames_presented != 2 ||
                readback->last_present_ticks != 100 ||
                readback->link_state != OutputLink::Up ||
                readback->active_mode_idx != 13) {
                return false;
            }
            if (output_stat_read(id1) != nullptr || output_stat_count() != 1) {
                return false;
            }
            if (output_stat_for(output_id_none) != nullptr) {
                return false;
            }
            return true;
        }

        bool cta_selftest(const hcpp::u8* edid, const hcpp::u8* cta) noexcept {
            CtaInfo info;
            if (!cta_parse(cta, info)) {
                return false;
            }
            if (!info.present || !info.checksum_ok || info.revision != 3) {
                return false;
            }
            if (!info.basic_audio || !info.ycbcr444 || !info.ycbcr422 || info.underscan) {
                return false;
            }
            if (info.data_block_count != 4) {
                return false;
            }
            if (info.video_count != 3) {
                return false;
            }
            if (info.videos[0].vic != 4 || !info.videos[0].native ||
                info.videos[1].vic != 16 || info.videos[1].native ||
                info.videos[2].vic != 31 || info.videos[2].native) {
                return false;
            }
            if (!cta_has_vic(info, 16) || cta_has_vic(info, 95) ||
                cta_native_vic(info) != 4) {
                return false;
            }
            if (info.audio_count != 2) {
                return false;
            }
            if (info.audios[0].format_code != 1 || info.audios[0].max_channels != 2 ||
                info.audios[0].sample_rates != 0x07 || info.audios[0].detail != 0x07) {
                return false;
            }
            if (info.audios[1].format_code != 2 || info.audios[1].max_channels != 6 ||
                info.audios[1].detail != 0x50) {
                return false;
            }
            if (!info.speaker_present || info.speaker_map != 0x4F) {
                return false;
            }
            if (!info.hdmi_present || info.hdmi_phys_address != 0x1000 || !info.hdmi_ai ||
                !info.hdmi_dc_36 || info.hdmi_dc_30 || !info.hdmi_dc_y444 ||
                info.hdmi_max_tmds_mhz != 200) {
                return false;
            }
            CtaInfo non_cta;
            if (cta_parse(edid, non_cta) || non_cta.present) {
                return false;
            }
            if (cta_parse(nullptr, non_cta)) {
                return false;
            }
            static hcpp::u8 foreign[128];
            for (hcpp::u32 i = 0; i < 128; ++i) {
                foreign[i] = 0;
            }
            foreign[0] = 0x70;
            if (cta_parse(foreign, non_cta) || non_cta.present) {
                return false;
            }
            return true;
        }

        bool dpi_selftest() noexcept {
            if (dpi_table_count() != 17 || dpi_table[0] != 96 ||
                dpi_table[dpi_table_count() - 1] != 480) {
                return false;
            }
            if (dpi_exact(1920, 1080, 521) != 107 || dpi_for(1920, 1080, 521) != 96) {
                return false;
            }
            if (dpi_exact(2560, 1440, 600) != 124 || dpi_for(2560, 1440, 600) != 120) {
                return false;
            }
            if (dpi_for(3840, 2160, 600) != 192) {
                return false;
            }
            if (dpi_for(1920, 1080, 0) != 96 || dpi_exact(0, 0, 300) != 96) {
                return false;
            }
            if (dpi_quantize(96) != 96 || dpi_quantize(480) != 480 ||
                dpi_quantize(4000) != 480 || dpi_quantize(10) != 96) {
                return false;
            }
            if (scale_factor_for(96) != DpiScale::X100 ||
                scale_factor_for(119) != DpiScale::X100 ||
                scale_factor_for(120) != DpiScale::X125 ||
                scale_factor_for(144) != DpiScale::X150 ||
                scale_factor_for(191) != DpiScale::X150 ||
                scale_factor_for(192) != DpiScale::X200 ||
                scale_factor_for(480) != DpiScale::X200) {
                return false;
            }
            if (scale_percent_for(150) != 150 || scale_q16_for(200) != 131072) {
                return false;
            }
            return true;
        }

    }  // namespace

    bool display2_selftest() noexcept {
        static hcpp::u8 edid[128];
        static hcpp::u8 cta[128];
        build_synthetic_edid(edid);
        build_synthetic_cta(cta);
        const hcpp::display::EdidInfo base = hcpp::display::parse_edid_base(edid);
        if (!base.header_valid || !base.checksum_ok || base.extension_count != 1) {
            serial::write_line("disp id=fail");
            return false;
        }
        if (!output_id_selftest(edid)) {
            serial::write_line("disp id=fail");
            return false;
        }
        serial::write("disp outputs=");
        serial::write_dec32(idscreen_count());
        serial::write("\r\n");
        if (!cta_selftest(edid, cta)) {
            serial::write_line("disp cta=fail");
            return false;
        }
        serial::write_line("disp cta=ok");
        if (!dpi_selftest()) {
            serial::write_line("disp dpi=fail");
            return false;
        }
        serial::write_line("disp dpi=ok");
        serial::write_line("disp id ok");
        return true;
    }

    bool display_selftest() noexcept {
        serial::write("disp modes=");
        serial::write_dec32(mode_count());
        serial::write("\r\n");
        if (!modes_selftest()) {
            serial::write_line("disp modes=fail");
            return false;
        }
        if (!scaler_selftest()) {
            serial::write_line("disp scale=fail");
            return false;
        }
        serial::write_line("disp scale=bilinear ok");
        if (!writeframe_selftest()) {
            serial::write_line("disp writeframe=fail");
            return false;
        }
        serial::write_line("disp writeframe=ok");
        if (!pal_selftest()) {
            serial::write_line("disp pal=fail");
            return false;
        }
        serial::write_line("disp pal=ok");
        hcpp::u32 uhd_modes = 0;
        if (!uhd_selftest(uhd_modes)) {
            serial::write_line("disp uhd=fail");
            return false;
        }
        serial::write("disp uhd modes=");
        serial::write_dec32(uhd_modes);
        serial::write("\r\n");
        if (!hdr_selftest()) {
            serial::write_line("disp hdr=fail");
            return false;
        }
        serial::write_line("disp hdr=ok");
        if (!audio_bridge_selftest()) {
            serial::write_line("disp audio=fail");
            return false;
        }
        serial::write_line("disp tables ok");
        if (!record_selftest()) {
            return false;
        }
        serial::write_line("disp core ok");
        return true;
    }

}  // namespace hype::display
