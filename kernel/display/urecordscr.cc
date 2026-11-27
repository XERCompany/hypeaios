#include "kernel/display/urecordscr.h"

#include "kernel/codecapps/registry.h"
#include "kernel/display/fmtcvt.h"
#include "kernel/display/highbitrate.h"
#include "kernel/serial.hpp"

namespace hype::display {

    namespace {

        constexpr hcpp::size_t record_pool_bytes = 128 * 1024;
        constexpr hcpp::size_t record_arena_bytes = 96 * 1024;
        constexpr hcpp::u32 record_selftest_frames = 3;

        struct RecordSlot {
            hcpp::u32 offset;
            hcpp::u32 size;
            codec::CodecId codec;
        };

        hcpp::u8 g_ring[record_pool_bytes];
        hcpp::u8 g_arena_mem[record_arena_bytes];
        codec::Arena g_arena{g_arena_mem, record_arena_bytes, 0};
        RecordSlot g_slots[urecord_max_frames];
        hcpp::u32 g_oldest = 0;
        hcpp::u32 g_count = 0;
        hcpp::u32 g_head = 0;
        hcpp::size_t g_total_bytes = 0;
        hcpp::u32 g_total_frames = 0;
        hcpp::u32 g_dropped = 0;
        bool g_active = false;
        BitrateTier g_tier = BitrateTier::High;
        codec::CodecId g_codec = codec::CodecId::Png;
        video::PixelFormat g_format = video::PixelFormat::RGB32;
        hcpp::u32 g_bitrate = 0;
        hcpp::graphics::Framebuffer g_fb;

        void evict_oldest() noexcept {
            g_total_bytes -= g_slots[g_oldest].size;
            g_oldest = (g_oldest + 1) % urecord_max_frames;
            --g_count;
            ++g_dropped;
        }

        hcpp::u8* ring_acquire(hcpp::size_t hint, hcpp::size_t& cap) noexcept {
            if (hint > record_pool_bytes) {
                hint = record_pool_bytes;
            }
            for (;;) {
                while (g_count >= urecord_max_frames) {
                    evict_oldest();
                }
                if (g_count == 0) {
                    g_head = 0;
                    cap = record_pool_bytes;
                    return g_ring;
                }
                const hcpp::u32 tail = g_slots[g_oldest].offset;
                if (g_head > tail) {
                    if (record_pool_bytes - g_head >= hint) {
                        cap = record_pool_bytes - g_head;
                        return g_ring + g_head;
                    }
                    if (tail >= hint) {
                        g_head = 0;
                        cap = tail;
                        return g_ring;
                    }
                    evict_oldest();
                } else if (g_head < tail) {
                    if (tail - g_head >= hint) {
                        cap = tail - g_head;
                        return g_ring + g_head;
                    }
                    evict_oldest();
                } else {
                    evict_oldest();
                }
            }
        }

        void ring_commit(hcpp::size_t written, codec::CodecId id) noexcept {
            g_slots[(g_oldest + g_count) % urecord_max_frames] =
                RecordSlot{g_head, static_cast<hcpp::u32>(written), id};
            ++g_count;
            g_head += static_cast<hcpp::u32>(written);
            g_total_bytes += written;
            ++g_total_frames;
        }

        hcpp::size_t encode_hint(const codec::FrameBuf& frame) noexcept {
            if (g_codec == codec::CodecId::Qoi) {
                return frame.size * 2 + 32;
            }
            return frame.size + frame.height + 8192;
        }

        void paint_synthetic(hcpp::graphics::Framebuffer& fb, hcpp::u32 frame_index) noexcept {
            for (hcpp::u32 y = 0; y < fb.height(); ++y) {
                for (hcpp::u32 x = 0; x < fb.width(); ++x) {
                    const hcpp::u8 red = static_cast<hcpp::u8>(x * 4 + frame_index * 40);
                    const hcpp::u8 green = static_cast<hcpp::u8>(y * 5 + frame_index * 30);
                    const hcpp::u8 blue =
                        static_cast<hcpp::u8>((x + y) * 3 + frame_index * 60);
                    fb.put_pixel(x, y, fb.encode_rgb(red, green, blue));
                }
            }
        }

        bool record_tier_pass(BitrateTier tier, codec::CodecId expected,
                              hcpp::u32& frames) noexcept {
            static hcpp::u8 fb_mem[64 * 48 * 4];
            hcpp::graphics::Framebuffer fb(fb_mem, 64, 48, 64 * 4, 32, 8, 16, 8, 8, 8, 0);
            frames = 0;
            if (!urecord_start(fb, tier) || urecord_codec() != expected ||
                urecord_bitrate_kbps() == 0) {
                return false;
            }
            for (hcpp::u32 i = 0; i < record_selftest_frames; ++i) {
                paint_synthetic(fb, i);
                if (!urecord_frame()) {
                    urecord_stop();
                    return false;
                }
            }
            if (urecord_frame_count() != record_selftest_frames) {
                urecord_stop();
                return false;
            }
            for (hcpp::u32 i = 0; i < record_selftest_frames; ++i) {
                hcpp::size_t size = 0;
                const hcpp::u8* data = urecord_frame_data(i, size);
                if (data == nullptr || size == 0 ||
                    codec::codec_probe(data, size) != expected) {
                    urecord_stop();
                    return false;
                }
            }
            hcpp::size_t last_size = 0;
            const hcpp::u8* last = urecord_last_frame(last_size);
            hcpp::size_t tail_size = 0;
            const hcpp::u8* tail = urecord_frame_data(record_selftest_frames - 1, tail_size);
            if (last == nullptr || last != tail || last_size != tail_size) {
                urecord_stop();
                return false;
            }
            frames = urecord_frame_count();
            urecord_stop();
            return !urecord_active();
        }

    }  // namespace

    bool urecord_start(hcpp::graphics::Framebuffer& fb, BitrateTier tier) noexcept {
        g_fb = fb;
        g_tier = tier;
        g_codec = tier == BitrateTier::Low ? codec::CodecId::Qoi : codec::CodecId::Png;
        g_format = tier == BitrateTier::Low ? video::PixelFormat::RGB24
                                            : video::PixelFormat::RGB32;
        const DisplayMode mode{fb.width(), fb.height(), 60, timing_generic};
        g_bitrate = bitrate_for(tier, mode);
        g_oldest = 0;
        g_count = 0;
        g_head = 0;
        g_total_bytes = 0;
        g_total_frames = 0;
        g_dropped = 0;
        codec::arena_reset(g_arena);
        g_active = g_fb.valid();
        return g_active;
    }

    bool urecord_frame() noexcept {
        if (!g_active || !g_fb.valid()) {
            return false;
        }
        codec::arena_reset(g_arena);
        codec::FrameBuf frame{};
        if (fmtcvt_frame(g_fb, frame, g_arena, g_format) != codec::CodecStatus::Ok) {
            return false;
        }
        hcpp::size_t cap = 0;
        hcpp::u8* out = ring_acquire(encode_hint(frame), cap);
        hcpp::size_t written = 0;
        if (codec::codec_encode(g_codec, &frame, out, cap, written, g_arena) !=
                codec::CodecStatus::Ok ||
            written == 0) {
            return false;
        }
        ring_commit(written, g_codec);
        return true;
    }

    void urecord_stop() noexcept { g_active = false; }

    bool urecord_active() noexcept { return g_active; }

    hcpp::u32 urecord_frame_count() noexcept { return g_count; }

    hcpp::u32 urecord_total_frames() noexcept { return g_total_frames; }

    hcpp::u32 urecord_dropped() noexcept { return g_dropped; }

    hcpp::size_t urecord_bytes() noexcept { return g_total_bytes; }

    hcpp::u32 urecord_bitrate_kbps() noexcept { return g_bitrate; }

    codec::CodecId urecord_codec() noexcept { return g_codec; }

    const hcpp::u8* urecord_frame_data(hcpp::u32 index, hcpp::size_t& size) noexcept {
        size = 0;
        if (index >= g_count) {
            return nullptr;
        }
        const RecordSlot& slot = g_slots[(g_oldest + index) % urecord_max_frames];
        size = slot.size;
        return g_ring + slot.offset;
    }

    const hcpp::u8* urecord_last_frame(hcpp::size_t& size) noexcept {
        size = 0;
        if (g_count == 0) {
            return nullptr;
        }
        return urecord_frame_data(g_count - 1, size);
    }

    bool record_selftest() noexcept {
        const BitrateTier tiers[] = {BitrateTier::Low, BitrateTier::High};
        hcpp::u32 passed = 0;
        hcpp::u32 frames = 0;
        for (hcpp::u32 i = 0; i < sizeof(tiers) / sizeof(tiers[0]); ++i) {
            const codec::CodecId expected =
                tiers[i] == BitrateTier::Low ? codec::CodecId::Qoi : codec::CodecId::Png;
            hcpp::u32 tier_frames = 0;
            if (record_tier_pass(tiers[i], expected, tier_frames)) {
                ++passed;
                frames = tier_frames;
            }
        }
        serial::write("disp record=");
        serial::write_line(passed == 2 ? "ok" : "fail");
        serial::write("disp frames=");
        serial::write_dec32(frames);
        serial::write("\r\n");
        serial::write("disp tiers=");
        serial::write_dec32(passed);
        serial::write("\r\n");
        return passed == 2;
    }

}  // namespace hype::display
