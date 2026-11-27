#include "kernel/display/writeframe.h"

#include "kernel/interrupts/idt.h"

namespace hype::display {

    namespace {

        constexpr hcpp::u32 max_row_width = 8192;
        constexpr hcpp::u64 pit_period_us = 10000;

        FrameStats g_stats{};
        hcpp::u32 g_src_row0[max_row_width];
        hcpp::u32 g_src_row1[max_row_width];

        void load_canonical_row(const hcpp::graphics::Framebuffer& src, hcpp::u32 y,
                                hcpp::u32* row, hcpp::u32 count) noexcept {
            for (hcpp::u32 x = 0; x < count; ++x) {
                hcpp::u8 red = 0;
                hcpp::u8 green = 0;
                hcpp::u8 blue = 0;
                src.decode_rgb(src.get_pixel(x, y), red, green, blue);
                row[x] = 0xFF000000u | (static_cast<hcpp::u32>(red) << 16) |
                         (static_cast<hcpp::u32>(green) << 8) | blue;
            }
        }

        void emit_pixel(hcpp::graphics::Framebuffer& dst, hcpp::u32 x, hcpp::u32 y,
                        hcpp::u32 canonical, bool convert) noexcept {
            if (!convert) {
                dst.put_pixel(x, y, canonical);
                return;
            }
            const hcpp::u8 red = static_cast<hcpp::u8>((canonical >> 16) & 0xFF);
            const hcpp::u8 green = static_cast<hcpp::u8>((canonical >> 8) & 0xFF);
            const hcpp::u8 blue = static_cast<hcpp::u8>(canonical & 0xFF);
            dst.put_pixel(x, y, dst.encode_rgb(red, green, blue));
        }

        bool copy_frame(hcpp::graphics::Framebuffer& dst,
                        const hcpp::graphics::Framebuffer& src, hcpp::u32 dst_x,
                        hcpp::u32 dst_y, bool convert) noexcept {
            for (hcpp::u32 y = 0; y < src.height(); ++y) {
                for (hcpp::u32 x = 0; x < src.width(); ++x) {
                    if (convert) {
                        hcpp::u8 red = 0;
                        hcpp::u8 green = 0;
                        hcpp::u8 blue = 0;
                        src.decode_rgb(src.get_pixel(x, y), red, green, blue);
                        dst.put_pixel(dst_x + x, dst_y + y, dst.encode_rgb(red, green, blue));
                    } else {
                        dst.put_pixel(dst_x + x, dst_y + y, src.get_pixel(x, y));
                    }
                }
            }
            return true;
        }

        void nearest_row(const hcpp::u32* src_row, hcpp::u32 src_w,
                         hcpp::graphics::Framebuffer& dst, hcpp::u32 y, hcpp::u32 dst_x,
                         hcpp::u32 dst_w, bool convert) noexcept {
            for (hcpp::u32 x = 0; x < dst_w; ++x) {
                const hcpp::u32 sx = (x * src_w) / dst_w;
                emit_pixel(dst, dst_x + x, y, src_row[sx], convert);
            }
        }

        void bilinear_rows(const hcpp::u32* row0, const hcpp::u32* row1, hcpp::u32 src_w,
                           hcpp::u32 frac_y, hcpp::i64 step_x, hcpp::graphics::Framebuffer& dst,
                           hcpp::u32 y, hcpp::u32 dst_x, hcpp::u32 dst_w,
                           bool convert) noexcept {
            for (hcpp::u32 x = 0; x < dst_w; ++x) {
                const hcpp::i64 fx = (step_x >> 1) - 0x8000 + static_cast<hcpp::i64>(x) * step_x;
                hcpp::u32 tap_x0 = 0;
                hcpp::u32 tap_x1 = 0;
                hcpp::u32 frac_x = 0;
                scale_sample_index(fx, src_w, tap_x0, tap_x1, frac_x);
                emit_pixel(dst, dst_x + x, y,
                           bilinear_sample(row0[tap_x0], row0[tap_x1], row1[tap_x0],
                                           row1[tap_x1], frac_x, frac_y),
                           convert);
            }
        }

        bool scale_frame(hcpp::graphics::Framebuffer& dst,
                         const hcpp::graphics::Framebuffer& src, const WriteFrameOpts& opts,
                         hcpp::u32 dst_x, hcpp::u32 dst_y, hcpp::u32 target_w,
                         hcpp::u32 target_h) noexcept {
            const hcpp::u32 src_w = src.width();
            const hcpp::u32 src_h = src.height();
            const bool integer_ratio =
                scale_integer_ratio(src_w, src_h, target_w, target_h) &&
                !opts.edge_preserve;
            const bool use_nearest =
                opts.filter == ScaleFilter::Nearest ||
                (opts.filter == ScaleFilter::Auto && integer_ratio);
            if (src_w > max_row_width) {
                for (hcpp::u32 y = 0; y < target_h; ++y) {
                    const hcpp::u32 sy = (y * src_h) / target_h;
                    for (hcpp::u32 x = 0; x < target_w; ++x) {
                        const hcpp::u32 sx = (x * src_w) / target_w;
                        hcpp::u8 red = 0;
                        hcpp::u8 green = 0;
                        hcpp::u8 blue = 0;
                        src.decode_rgb(src.get_pixel(sx, sy), red, green, blue);
                        emit_pixel(dst, dst_x + x, dst_y + y,
                                   0xFF000000u | (static_cast<hcpp::u32>(red) << 16) |
                                       (static_cast<hcpp::u32>(green) << 8) | blue,
                                   opts.convert);
                    }
                }
                return true;
            }
            const hcpp::i64 step_y = (static_cast<hcpp::i64>(src_h) << 16) / target_h;
            const hcpp::i64 step_x = (static_cast<hcpp::i64>(src_w) << 16) / target_w;
            for (hcpp::u32 y = 0; y < target_h; ++y) {
                if (use_nearest) {
                    const hcpp::u32 sy = (y * src_h) / target_h;
                    load_canonical_row(src, sy, g_src_row0, src_w);
                    nearest_row(g_src_row0, src_w, dst, dst_y + y, dst_x, target_w,
                                opts.convert);
                    continue;
                }
                const hcpp::i64 fy = (step_y >> 1) - 0x8000 + static_cast<hcpp::i64>(y) * step_y;
                hcpp::u32 tap_y0 = 0;
                hcpp::u32 tap_y1 = 0;
                hcpp::u32 frac_y = 0;
                scale_sample_index(fy, src_h, tap_y0, tap_y1, frac_y);
                load_canonical_row(src, tap_y0, g_src_row0, src_w);
                load_canonical_row(src, tap_y1, g_src_row1, src_w);
                bilinear_rows(g_src_row0, g_src_row1, src_w, frac_y, step_x, dst, dst_y + y,
                              dst_x, target_w, opts.convert);
            }
            return true;
        }

    }  // namespace

    bool write_frame(hcpp::graphics::Framebuffer& dst,
                     const hcpp::graphics::Framebuffer& src,
                     const WriteFrameOpts& opts) noexcept {
        const hcpp::u64 start_ticks = hype::interrupts::pit_ticks();
        bool ok = false;
        if (dst.valid() && src.valid()) {
            const hcpp::u32 dst_x = opts.dst_x < dst.width() ? opts.dst_x : dst.width();
            const hcpp::u32 dst_y = opts.dst_y < dst.height() ? opts.dst_y : dst.height();
            const hcpp::u32 room_w = dst.width() - dst_x;
            const hcpp::u32 room_h = dst.height() - dst_y;
            const hcpp::u32 target_w =
                opts.dst_w == 0 ? room_w : (opts.dst_w < room_w ? opts.dst_w : room_w);
            const hcpp::u32 target_h =
                opts.dst_h == 0 ? room_h : (opts.dst_h < room_h ? opts.dst_h : room_h);
            if (target_w != 0 && target_h != 0) {
                if (target_w == src.width() && target_h == src.height()) {
                    ok = copy_frame(dst, src, dst_x, dst_y, opts.convert);
                } else {
                    ok = scale_frame(dst, src, opts, dst_x, dst_y, target_w, target_h);
                }
            }
        }
        ++g_stats.frames;
        g_stats.last_us = (hype::interrupts::pit_ticks() - start_ticks) * pit_period_us;
        return ok;
    }

    bool write_frame(hcpp::graphics::Framebuffer& dst,
                     const hcpp::graphics::Framebuffer& src) noexcept {
        return write_frame(dst, src, WriteFrameOpts{});
    }

    const FrameStats& frame_stats() noexcept { return g_stats; }

    void frame_stats_reset() noexcept {
        g_stats.frames = 0;
        g_stats.last_us = 0;
    }

}  // namespace hype::display
