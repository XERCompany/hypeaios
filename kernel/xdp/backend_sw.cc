#include "kernel/xdp/xdpint.h"

#include "hcpp/memory.hpp"

// XDP software backend: executes the xdp_cmd op set straight against
// surface pixels and presents dirty rects into the bound VBE linear
// framebuffer. Always available — it is the v1 active path and the
// render fallback underneath every render-less present backend.

namespace hype::xdp {

    namespace {

        hype::ui::Rect clip_to_surface(const xdp_surface& surf,
                                       hype::ui::Rect rect) noexcept {
            const hype::ui::Rect bounds{
                0, 0, static_cast<hcpp::i32>(surf.width),
                static_cast<hcpp::i32>(surf.height)};
            return rect.intersection(bounds);
        }

        hcpp::u32* row_at(const xdp_surface& surf, hcpp::i32 y) noexcept {
            return reinterpret_cast<hcpp::u32*>(
                surf.pixels +
                static_cast<hcpp::size_t>(y) * surf.pitch_bytes);
        }

        const hcpp::u32* crow_at(const xdp_surface& surf,
                                 hcpp::i32 y) noexcept {
            return reinterpret_cast<const hcpp::u32*>(
                surf.pixels +
                static_cast<hcpp::size_t>(y) * surf.pitch_bytes);
        }

        void fill_span(hcpp::u32* dst, hcpp::u32 count,
                       hcpp::u32 color) noexcept {
            for (hcpp::u32 i = 0; i < count; ++i) dst[i] = color;
        }

        void exec_clear(xdp_surface& surf, hcpp::u32 color) noexcept {
            for (hcpp::u32 y = 0; y < surf.height; ++y) {
                fill_span(row_at(surf, static_cast<hcpp::i32>(y)),
                          surf.width, color);
            }
        }

        void exec_fill(xdp_surface& surf, hype::ui::Rect rect,
                       hcpp::u32 color) noexcept {
            const hype::ui::Rect r = clip_to_surface(surf, rect);
            if (r.empty()) return;
            for (hcpp::i32 y = r.y; y < r.y + r.h; ++y) {
                fill_span(row_at(surf, y) + r.x, static_cast<hcpp::u32>(r.w),
                          color);
            }
        }

        void exec_stroke(xdp_surface& surf, hype::ui::Rect rect,
                         hcpp::u32 color, hcpp::u8 thickness) noexcept {
            if (thickness == 0) thickness = 1;
            const hype::ui::Rect r = clip_to_surface(surf, rect);
            if (r.empty()) return;
            const hcpp::i32 t = thickness;
            // Four edge rects; corner overlap is harmless.
            exec_fill(surf, hype::ui::Rect{r.x, r.y, r.w, t}, color);
            exec_fill(surf, hype::ui::Rect{r.x, r.bottom() - t, r.w, t}, color);
            exec_fill(surf, hype::ui::Rect{r.x, r.y + t, t, r.h - 2 * t},
                      color);
            exec_fill(surf,
                      hype::ui::Rect{r.right() - t, r.y + t, t, r.h - 2 * t},
                      color);
        }

        void exec_blit(xdp_surface& surf, const xdp_surface& src,
                       hype::ui::Point src_pos,
                       hype::ui::Rect dst) noexcept {
            // Clip the destination, then back-map the origin so the
            // source window stays aligned.
            hype::ui::Rect clipped = clip_to_surface(surf, dst);
            if (clipped.empty()) return;
            hype::ui::Point s{src_pos.x + (clipped.x - dst.x),
                              src_pos.y + (clipped.y - dst.y)};
            // Clip the source window as well.
            if (s.x < 0) {
                clipped.x -= s.x;
                clipped.w += s.x;
                s.x = 0;
            }
            if (s.y < 0) {
                clipped.y -= s.y;
                clipped.h += s.y;
                s.y = 0;
            }
            if (s.x + clipped.w > static_cast<hcpp::i32>(src.width)) {
                clipped.w = static_cast<hcpp::i32>(src.width) - s.x;
            }
            if (s.y + clipped.h > static_cast<hcpp::i32>(src.height)) {
                clipped.h = static_cast<hcpp::i32>(src.height) - s.y;
            }
            if (clipped.w <= 0 || clipped.h <= 0) return;
            const hcpp::size_t bytes =
                static_cast<hcpp::size_t>(clipped.w) * 4;
            for (hcpp::i32 y = 0; y < clipped.h; ++y) {
                // hcpp::move handles overlapping ranges when blitting a
                // surface onto itself.
                hcpp::move(row_at(surf, clipped.y + y) + clipped.x,
                           crow_at(src, s.y + y) + s.x, bytes);
            }
        }

        bool sw_submit(xdp_surface& surf, const xdp_cmd* cmds,
                       hcpp::u32 count) noexcept {
            if (surf.pixels == nullptr) return false;
            for (hcpp::u32 i = 0; i < count; ++i) {
                const xdp_cmd& c = cmds[i];
                switch (c.op) {
                    case xdp_op_clear:
                        exec_clear(surf, c.color);
                        break;
                    case xdp_op_fill_rect:
                        exec_fill(surf, c.rect, c.color);
                        break;
                    case xdp_op_stroke_rect:
                        exec_stroke(surf, c.rect, c.color, c.aux);
                        break;
                    case xdp_op_blit:
                        if (c.src != nullptr && c.src->pixels != nullptr &&
                            c.src->format == PixelFormat::Bgra32) {
                            exec_blit(surf, *c.src, c.src_pos, c.rect);
                        }
                        break;
                    default:
                        break;
                }
            }
            return true;
        }

        void present_rect_slow(const xdp_surface& surf,
                               const hcpp::graphics::Framebuffer& out,
                               const hype::ui::Rect& r) noexcept {
            // Generic path: re-encode every pixel for whatever channel
            // layout the boot loader picked (24 bpp or exotic masks).
            for (hcpp::i32 y = 0; y < r.h; ++y) {
                const hcpp::u32* src = crow_at(surf, r.y + y) + r.x;
                for (hcpp::i32 x = 0; x < r.w; ++x) {
                    const hcpp::u32 px = src[x];
                    out.put_pixel(
                        static_cast<hcpp::u32>(r.x + x),
                        static_cast<hcpp::u32>(r.y + y),
                        out.encode_rgb(static_cast<hcpp::u8>(px >> 16),
                                       static_cast<hcpp::u8>(px >> 8),
                                       static_cast<hcpp::u8>(px)));
                }
            }
        }

        bool sw_present(const xdp_surface& surf,
                        const hype::ui::Rect* dirty,
                        hcpp::u32 count) noexcept {
            const xdp_scanout& scan = xdp_scanout_info();
            if (!scan.bound || !scan.fb.valid() || surf.pixels == nullptr) {
                return false;
            }
            const hype::ui::Rect screen{
                0, 0, static_cast<hcpp::i32>(scan.fb.width()),
                static_cast<hcpp::i32>(scan.fb.height())};
            // Present region clamped by BOTH the surface and the
            // scanout — they are sized identically in v1 but nothing in
            // the contract requires that.
            const hype::ui::Rect surf_bounds{
                0, 0, static_cast<hcpp::i32>(surf.width),
                static_cast<hcpp::i32>(surf.height)};
            for (hcpp::u32 i = 0; i < count; ++i) {
                const hype::ui::Rect r =
                    dirty[i].intersection(surf_bounds).intersection(screen);
                if (r.empty()) continue;
                if (scan.native_bgra32 && scan.base != nullptr) {
                    // Fast path: LFB is already BGRA32 — straight row
                    // copies through the raw base pointer (Framebuffer
                    // keeps base_ private, so xdp_init hands it over).
                    const hcpp::size_t bytes =
                        static_cast<hcpp::size_t>(r.w) * 4;
                    for (hcpp::i32 y = 0; y < r.h; ++y) {
                        volatile hcpp::u8* dst =
                            scan.base +
                            static_cast<hcpp::size_t>(r.y + y) *
                                scan.fb.pitch() +
                            static_cast<hcpp::size_t>(r.x) * 4;
                        hcpp::copy(const_cast<hcpp::u8*>(dst),
                                   crow_at(surf, r.y + y) + r.x, bytes);
                    }
                } else {
                    present_rect_slow(surf, scan.fb, r);
                }
            }
            return true;
        }

        const xdp_backend backend{
            "sw", XDP_CAP_2D | XDP_CAP_PRESENT, sw_submit, sw_present};

    }  // namespace

    const xdp_backend* xdp_sw_backend() noexcept { return &backend; }

}  // namespace hype::xdp
