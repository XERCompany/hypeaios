#include "kernel/gfx/hypegfx.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"

namespace hype::gfx {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Insets;
        using hype::ui::Point;
        using hype::ui::Rect;

        constexpr hcpp::i32 k_mask_capacity = 65536;
        constexpr hcpp::i32 k_mask_max_width = 256;
        hcpp::u8 mask_a[k_mask_capacity];
        hcpp::u8 mask_b[k_mask_capacity];

        hcpp::i32 isqrt_i32(hcpp::i32 value) noexcept {
            if (value <= 0) {
                return 0;
            }
            const hcpp::u32 v = static_cast<hcpp::u32>(value);
            hcpp::u32 x = v;
            hcpp::u32 y = (x + 1) / 2;
            while (y < x) {
                x = y;
                y = (x + v / x) / 2;
            }
            return static_cast<hcpp::i32>(x);
        }

        hcpp::i32 clamp_i32(hcpp::i32 value, hcpp::i32 low, hcpp::i32 high) noexcept {
            if (value < low) {
                return low;
            }
            return value > high ? high : value;
        }

        hcpp::i32 clamp_radius(Rect rect, hcpp::i32 radius) noexcept {
            const hcpp::i32 half = (rect.w < rect.h ? rect.w : rect.h) / 2;
            if (radius < 0) {
                return 0;
            }
            return radius > half ? half : radius;
        }

        void blend_dark(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                        hcpp::u32 alpha) noexcept {
            if (alpha == 0) {
                return;
            }
            const hcpp::u32 dst =
                fb.get_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
            hcpp::u8 red = 0;
            hcpp::u8 green = 0;
            hcpp::u8 blue = 0;
            fb.decode_rgb(dst, red, green, blue);
            const hcpp::u32 inverse = 255 - alpha;
            fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                         fb.encode_rgb(static_cast<hcpp::u8>(red * inverse / 255),
                                       static_cast<hcpp::u8>(green * inverse / 255),
                                       static_cast<hcpp::u8>(blue * inverse / 255)));
        }

        // Multiplicative shade toward a tint color: always darkens (tint < 255
        // per channel) so even a warm-dark shadow stays a shadow over dark
        // backgrounds, while keeping a subtle warm bias instead of pure black.
        void blend_tint(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                        hcpp::u8 tint_r, hcpp::u8 tint_g, hcpp::u8 tint_b,
                        hcpp::u32 alpha) noexcept {
            if (alpha == 0) {
                return;
            }
            if (alpha > 255) {
                alpha = 255;
            }
            const hcpp::u32 dst =
                fb.get_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
            hcpp::u8 red = 0;
            hcpp::u8 green = 0;
            hcpp::u8 blue = 0;
            fb.decode_rgb(dst, red, green, blue);
            fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                         fb.encode_rgb(
                             static_cast<hcpp::u8>(
                                 red * (65025 - alpha * (255 - tint_r)) /
                                 65025),
                             static_cast<hcpp::u8>(
                                 green * (65025 - alpha * (255 - tint_g)) /
                                 65025),
                             static_cast<hcpp::u8>(
                                 blue * (65025 - alpha * (255 - tint_b)) /
                                 65025)));
        }

        // Additive ("plus-lighter") blend used for inner glow edges and the
        // faint top highlight on premium dark cards.
        void blend_add(hcpp::graphics::Framebuffer& fb, hcpp::i32 x, hcpp::i32 y,
                       hcpp::u8 add_r, hcpp::u8 add_g, hcpp::u8 add_b,
                       hcpp::u32 alpha) noexcept {
            if (alpha == 0) {
                return;
            }
            const hcpp::u32 dst =
                fb.get_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
            hcpp::u8 red = 0;
            hcpp::u8 green = 0;
            hcpp::u8 blue = 0;
            fb.decode_rgb(dst, red, green, blue);
            fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                         fb.encode_rgb(
                             static_cast<hcpp::u8>(clamp_i32(
                                 red + add_r * alpha / 255, 0, 255)),
                             static_cast<hcpp::u8>(clamp_i32(
                                 green + add_g * alpha / 255, 0, 255)),
                             static_cast<hcpp::u8>(clamp_i32(
                                 blue + add_b * alpha / 255, 0, 255))));
        }

        // Horizontal inset of a rounded rect at a given scanline row.
        hcpp::i32 row_inset(Rect rect, hcpp::i32 radius, hcpp::i32 row) noexcept {
            const hcpp::i32 top_center = rect.y + radius;
            const hcpp::i32 bottom_center = rect.bottom() - radius - 1;
            hcpp::i32 distance = 0;
            if (row < top_center) {
                distance = top_center - row;
            } else if (row > bottom_center) {
                distance = row - bottom_center;
            }
            if (distance <= 0) {
                return 0;
            }
            return radius - isqrt_i32(radius * radius - distance * distance);
        }

        void box_blur(hcpp::u8* dst, hcpp::u8* tmp, hcpp::i32 w, hcpp::i32 h,
                      hcpp::i32 radius) noexcept {
            const hcpp::i32 window = 2 * radius + 1;
            for (hcpp::i32 y = 0; y < h; ++y) {
                const hcpp::u8* row = dst + y * w;
                hcpp::u8* out = tmp + y * w;
                hcpp::i32 sum = 0;
                for (hcpp::i32 i = -radius; i <= radius; ++i) {
                    sum += row[clamp_i32(i, 0, w - 1)];
                }
                for (hcpp::i32 x = 0; x < w; ++x) {
                    out[x] = static_cast<hcpp::u8>(sum / window);
                    sum += row[clamp_i32(x + radius + 1, 0, w - 1)] -
                           row[clamp_i32(x - radius, 0, w - 1)];
                }
            }
            for (hcpp::i32 x = 0; x < w; ++x) {
                hcpp::i32 sum = 0;
                for (hcpp::i32 i = -radius; i <= radius; ++i) {
                    sum += tmp[clamp_i32(i, 0, h - 1) * w + x];
                }
                for (hcpp::i32 y = 0; y < h; ++y) {
                    dst[y * w + x] = static_cast<hcpp::u8>(sum / window);
                    sum += tmp[clamp_i32(y + radius + 1, 0, h - 1) * w + x] -
                           tmp[clamp_i32(y - radius, 0, h - 1) * w + x];
                }
            }
        }

        hcpp::u8 channel_lerp(hcpp::u8 a, hcpp::u8 b, hcpp::u32 t) noexcept {
            return static_cast<hcpp::u8>((a * (255 - t) + b * t) / 255);
        }

        void unpack_rgb(hcpp::u32 rgb, hcpp::u8& red, hcpp::u8& green,
                        hcpp::u8& blue) noexcept {
            red = static_cast<hcpp::u8>((rgb >> 16) & 0xFF);
            green = static_cast<hcpp::u8>((rgb >> 8) & 0xFF);
            blue = static_cast<hcpp::u8>(rgb & 0xFF);
        }

        hcpp::i32 edge_distance(Rect rect, hcpp::i32 radius, hcpp::i32 x,
                                hcpp::i32 y) noexcept {
            const hcpp::i32 dx_edge =
                x - rect.x < rect.right() - 1 - x ? x - rect.x : rect.right() - 1 - x;
            const hcpp::i32 dy_edge =
                y - rect.y < rect.bottom() - 1 - y ? y - rect.y : rect.bottom() - 1 - y;
            hcpp::i32 cx = x;
            if (x < rect.x + radius) {
                cx = rect.x + radius;
            } else if (x >= rect.right() - radius) {
                cx = rect.right() - radius - 1;
            }
            hcpp::i32 cy = y;
            if (y < rect.y + radius) {
                cy = rect.y + radius;
            } else if (y >= rect.bottom() - radius) {
                cy = rect.bottom() - radius - 1;
            }
            if (cx != x && cy != y) {
                const hcpp::i32 ddx = x - cx;
                const hcpp::i32 ddy = y - cy;
                return radius - isqrt_i32(ddx * ddx + ddy * ddy);
            }
            return dx_edge < dy_edge ? dx_edge : dy_edge;
        }

        bool pixel_near(hcpp::graphics::Framebuffer& fb, hcpp::u32 pixel, hcpp::u8 red,
                        hcpp::u8 green, hcpp::u8 blue) noexcept {
            hcpp::u8 r = 0;
            hcpp::u8 g = 0;
            hcpp::u8 b = 0;
            fb.decode_rgb(pixel, r, g, b);
            const hcpp::i32 dr = r - red;
            const hcpp::i32 dg = g - green;
            const hcpp::i32 db = b - blue;
            return dr >= -2 && dr <= 2 && dg >= -2 && dg <= 2 && db >= -2 && db <= 2;
        }

        hcpp::u32 pixel_luma(hcpp::graphics::Framebuffer& fb, hcpp::u32 pixel) noexcept {
            hcpp::u8 r = 0;
            hcpp::u8 g = 0;
            hcpp::u8 b = 0;
            fb.decode_rgb(pixel, r, g, b);
            return static_cast<hcpp::u32>(r) + g + b;
        }

    }  // namespace

    void gfx_soft_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius, hcpp::i32 blur,
                         hcpp::u8 alpha) noexcept {
        gfx_soft_shadow_tinted(canvas, rect, radius, blur, alpha, 0x000000);
    }

    void gfx_soft_shadow_tinted(Canvas& canvas, Rect rect, hcpp::i32 radius,
                                hcpp::i32 blur, hcpp::u8 alpha,
                                hcpp::u32 tint_rgb) noexcept {
        if (rect.empty() || alpha == 0) {
            return;
        }
        hcpp::graphics::Framebuffer& fb = canvas.target();
        if (!fb.valid()) {
            return;
        }
        blur = clamp_i32(blur, 0, 48);
        const hcpp::i32 offset_y = 2 + blur / 3;
        const Rect full = rect.inset(Insets::uniform(-blur))
                              .translated(Point{0, offset_y})
                              .intersection(canvas.clip())
                              .intersection(Rect{0, 0,
                                                 static_cast<hcpp::i32>(fb.width()),
                                                 static_cast<hcpp::i32>(fb.height())});
        if (full.empty()) {
            return;
        }

        // The static blur masks are bounded, so the shadow is rasterized in
        // tiles. Each tile's mask extends `halo` px past the tile's composited
        // area — the halo covers the full reach of the three blur passes, so
        // tiles are seamless even when the clip rect cuts through the shadow.
        const hcpp::i32 pass_radius = blur > 0 ? clamp_i32(blur / 2, 1, 16) : 0;
        const hcpp::i32 halo = 3 * pass_radius;
        const hcpp::i32 mask_w = k_mask_max_width;
        const hcpp::i32 mask_h_cap = k_mask_capacity / mask_w;
        const hcpp::i32 step_x = mask_w - 2 * halo;
        const hcpp::i32 step_y = mask_h_cap - 2 * halo;
        if (step_x <= 0 || step_y <= 0) {
            return;
        }
        const Rect shape = rect.translated(Point{0, offset_y});
        const hcpp::i32 r = clamp_radius(shape, radius);
        const hcpp::i32 rr = r * r;
        const hcpp::i32 top_center = shape.y + r;
        const hcpp::i32 bottom_center = shape.bottom() - r - 1;
        hcpp::u8 tr = 0;
        hcpp::u8 tg = 0;
        hcpp::u8 tb = 0;
        unpack_rgb(tint_rgb, tr, tg, tb);

        for (hcpp::i32 ty = full.y; ty < full.bottom(); ty += step_y) {
            const hcpp::i32 tile_h =
                full.bottom() - ty < step_y ? full.bottom() - ty : step_y;
            const hcpp::i32 mh = tile_h + 2 * halo;
            for (hcpp::i32 tx = full.x; tx < full.right(); tx += step_x) {
                const hcpp::i32 tile_w =
                    full.right() - tx < step_x ? full.right() - tx : step_x;
                const hcpp::i32 mw = tile_w + 2 * halo;
                const Rect mask_rect{tx - halo, ty - halo, mw, mh};

                const hcpp::i32 count = mw * mh;
                for (hcpp::i32 i = 0; i < count; ++i) {
                    mask_a[i] = 0;
                }
                for (hcpp::i32 row = shape.y; row < shape.bottom(); ++row) {
                    if (row < mask_rect.y || row >= mask_rect.bottom()) {
                        continue;
                    }
                    hcpp::i32 distance = 0;
                    if (row < top_center) {
                        distance = top_center - row;
                    } else if (row > bottom_center) {
                        distance = row - bottom_center;
                    }
                    hcpp::i32 inset = 0;
                    if (distance > 0) {
                        inset = r - isqrt_i32(rr - distance * distance);
                    }
                    hcpp::i32 span_x = shape.x + inset;
                    hcpp::i32 span_right = shape.right() - inset;
                    if (span_x < mask_rect.x) {
                        span_x = mask_rect.x;
                    }
                    if (span_right > mask_rect.right()) {
                        span_right = mask_rect.right();
                    }
                    hcpp::u8* out =
                        mask_a + (row - mask_rect.y) * mw + (span_x - mask_rect.x);
                    for (hcpp::i32 x = span_x; x < span_right; ++x) {
                        *out++ = 255;
                    }
                }

                if (blur > 0) {
                    for (hcpp::i32 pass = 0; pass < 3; ++pass) {
                        box_blur(mask_a, mask_b, mw, mh, pass_radius);
                    }
                }

                for (hcpp::i32 y = ty; y < ty + tile_h; ++y) {
                    const hcpp::u8* src =
                        mask_a + (y - mask_rect.y) * mw + (tx - mask_rect.x);
                    for (hcpp::i32 x = tx; x < tx + tile_w; ++x) {
                        const hcpp::u32 coverage = *src++;
                        if (coverage == 0) {
                            continue;
                        }
                        blend_tint(fb, x, y, tr, tg, tb,
                                   coverage * alpha / 255);
                    }
                }
            }
        }
    }

    void gfx_inner_shadow(Canvas& canvas, Rect rect, hcpp::i32 radius,
                          hcpp::i32 blur) noexcept {
        if (rect.empty() || blur <= 0) {
            return;
        }
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        const hcpp::i32 r = clamp_radius(rect, radius);
        constexpr hcpp::i32 strength = 128;
        for (hcpp::i32 y = area.y; y < area.bottom(); ++y) {
            for (hcpp::i32 x = area.x; x < area.right(); ++x) {
                const hcpp::i32 d = edge_distance(rect, r, x, y);
                if (d < 0 || d >= blur) {
                    continue;
                }
                blend_dark(fb, x, y,
                           static_cast<hcpp::u32>(strength * (blur - d) / blur));
            }
        }
    }

    // Premium dark-card edge: an accent inner shadow rising from the bottom
    // edge (clipped to the rounded shape) plus a faint white top highlight.
    void gfx_inner_glow_edge(Canvas& canvas, Rect rect, hcpp::i32 radius,
                             hcpp::u32 accent_rgb) noexcept {
        if (rect.empty()) {
            return;
        }
        hcpp::graphics::Framebuffer& fb = canvas.target();
        if (!fb.valid()) {
            return;
        }
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        const hcpp::i32 r = clamp_radius(rect, radius);
        hcpp::u8 ar = 0;
        hcpp::u8 ag = 0;
        hcpp::u8 ab = 0;
        unpack_rgb(accent_rgb, ar, ag, ab);

        const hcpp::i32 depth = clamp_i32(rect.h / 4, 10, 36);
        const hcpp::i32 glow_top = rect.bottom() - depth;
        const hcpp::i32 y0 = area.y > glow_top ? area.y : glow_top;
        for (hcpp::i32 y = y0; y < area.bottom(); ++y) {
            const hcpp::i32 d = rect.bottom() - 1 - y;
            const hcpp::i32 rise = depth - d;
            hcpp::u32 alpha = static_cast<hcpp::u32>(rise * rise * 140) /
                              static_cast<hcpp::u32>(depth * depth);
            if (d <= 1) {
                alpha += 110;
            }
            if (alpha > 220) {
                alpha = 220;
            }
            const hcpp::i32 inset = (r > 0 ? row_inset(rect, r, y) : 0) + 1;
            hcpp::i32 sx = rect.x + inset;
            if (sx < area.x) {
                sx = area.x;
            }
            hcpp::i32 sr = rect.right() - inset;
            if (sr > area.right()) {
                sr = area.right();
            }
            for (hcpp::i32 x = sx; x < sr; ++x) {
                blend_add(fb, x, y, ar, ag, ab, alpha);
            }
        }

        for (hcpp::i32 k = 0; k < 2; ++k) {
            const hcpp::i32 y = rect.y + 1 + k;
            if (y < area.y || y >= area.bottom()) {
                continue;
            }
            const hcpp::i32 inset = (r > 0 ? row_inset(rect, r, y) : 0) + 2;
            hcpp::i32 sx = rect.x + inset;
            if (sx < area.x) {
                sx = area.x;
            }
            hcpp::i32 sr = rect.right() - inset;
            if (sr > area.right()) {
                sr = area.right();
            }
            const hcpp::u32 alpha = k == 0 ? 30u : 14u;
            for (hcpp::i32 x = sx; x < sr; ++x) {
                blend_add(fb, x, y, 255, 255, 255, alpha);
            }
        }
    }

    void gfx_gradient_v(Canvas& canvas, Rect rect, hcpp::u32 c1, hcpp::u32 c2) noexcept {
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        hcpp::u8 r1 = 0;
        hcpp::u8 g1 = 0;
        hcpp::u8 b1 = 0;
        hcpp::u8 r2 = 0;
        hcpp::u8 g2 = 0;
        hcpp::u8 b2 = 0;
        unpack_rgb(c1, r1, g1, b1);
        unpack_rgb(c2, r2, g2, b2);
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const hcpp::i32 span = rect.h > 1 ? rect.h - 1 : 1;
        for (hcpp::i32 y = area.y; y < area.bottom(); ++y) {
            const hcpp::u32 t =
                static_cast<hcpp::u32>(clamp_i32((y - rect.y) * 255 / span, 0, 255));
            const hcpp::u32 color =
                fb.encode_rgb(channel_lerp(r1, r2, t), channel_lerp(g1, g2, t),
                              channel_lerp(b1, b2, t));
            fb.fill_rect(static_cast<hcpp::u32>(area.x), static_cast<hcpp::u32>(y),
                         static_cast<hcpp::u32>(area.w), 1, color);
        }
    }

    void gfx_gradient_h(Canvas& canvas, Rect rect, hcpp::u32 c1, hcpp::u32 c2) noexcept {
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        hcpp::u8 r1 = 0;
        hcpp::u8 g1 = 0;
        hcpp::u8 b1 = 0;
        hcpp::u8 r2 = 0;
        hcpp::u8 g2 = 0;
        hcpp::u8 b2 = 0;
        unpack_rgb(c1, r1, g1, b1);
        unpack_rgb(c2, r2, g2, b2);
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const hcpp::i32 span = rect.w > 1 ? rect.w - 1 : 1;
        for (hcpp::i32 x = area.x; x < area.right(); ++x) {
            const hcpp::u32 t =
                static_cast<hcpp::u32>(clamp_i32((x - rect.x) * 255 / span, 0, 255));
            const hcpp::u32 color =
                fb.encode_rgb(channel_lerp(r1, r2, t), channel_lerp(g1, g2, t),
                              channel_lerp(b1, b2, t));
            fb.fill_rect(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(area.y), 1,
                         static_cast<hcpp::u32>(area.h), color);
        }
    }

    void gfx_gradient_radial(Canvas& canvas, Rect rect, hcpp::u32 c_center,
                             hcpp::u32 c_edge) noexcept {
        const Rect area = rect.intersection(canvas.clip());
        if (area.empty()) {
            return;
        }
        hcpp::u8 r1 = 0;
        hcpp::u8 g1 = 0;
        hcpp::u8 b1 = 0;
        hcpp::u8 r2 = 0;
        hcpp::u8 g2 = 0;
        hcpp::u8 b2 = 0;
        unpack_rgb(c_center, r1, g1, b1);
        unpack_rgb(c_edge, r2, g2, b2);
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const hcpp::i32 cx = rect.x + rect.w / 2;
        const hcpp::i32 cy = rect.y + rect.h / 2;
        const hcpp::i32 hx = rect.w / 2;
        const hcpp::i32 hy = rect.h / 2;
        hcpp::i32 max_dist = isqrt_i32(hx * hx + hy * hy);
        if (max_dist <= 0) {
            max_dist = 1;
        }
        for (hcpp::i32 y = area.y; y < area.bottom(); ++y) {
            const hcpp::i32 dy = y - cy;
            for (hcpp::i32 x = area.x; x < area.right(); ++x) {
                const hcpp::i32 dx = x - cx;
                const hcpp::u32 t = static_cast<hcpp::u32>(
                    clamp_i32(isqrt_i32(dx * dx + dy * dy) * 255 / max_dist, 0, 255));
                fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                             fb.encode_rgb(channel_lerp(r1, r2, t),
                                           channel_lerp(g1, g2, t),
                                           channel_lerp(b1, b2, t)));
            }
        }
    }

    void gfx_elevation(Canvas& canvas, Rect rect, hcpp::u32 level) noexcept {
        if (rect.empty()) {
            return;
        }
        if (level > 4) {
            level = 4;
        }
        if (level > 0) {
            gfx_soft_shadow(canvas, rect, 4, 2 + static_cast<hcpp::i32>(level) * 3,
                            static_cast<hcpp::u8>(20 + level * 20));
        }
        const hcpp::i32 lift = static_cast<hcpp::i32>(level) * 10;
        if (lift == 0) {
            return;
        }
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const Rect area = rect.intersection(canvas.clip());
        for (hcpp::i32 y = area.y; y < area.bottom(); ++y) {
            for (hcpp::i32 x = area.x; x < area.right(); ++x) {
                const hcpp::u32 dst =
                    fb.get_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
                hcpp::u8 r = 0;
                hcpp::u8 g = 0;
                hcpp::u8 b = 0;
                fb.decode_rgb(dst, r, g, b);
                fb.put_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                             fb.encode_rgb(
                                 static_cast<hcpp::u8>(clamp_i32(r + lift, 0, 255)),
                                 static_cast<hcpp::u8>(clamp_i32(g + lift, 0, 255)),
                                 static_cast<hcpp::u8>(clamp_i32(b + lift, 0, 255))));
            }
        }
    }

    void gfx_glow(Canvas& canvas, Rect rect, hcpp::u32 color, hcpp::i32 radius) noexcept {
        if (rect.empty() || radius <= 0) {
            return;
        }
        hcpp::graphics::Framebuffer& fb = canvas.target();
        const Rect region =
            rect.inset(Insets::uniform(-radius)).intersection(canvas.clip());
        if (region.empty()) {
            return;
        }
        hcpp::u8 sr = 0;
        hcpp::u8 sg = 0;
        hcpp::u8 sb = 0;
        unpack_rgb(color, sr, sg, sb);
        for (hcpp::i32 y = region.y; y < region.bottom(); ++y) {
            for (hcpp::i32 x = region.x; x < region.right(); ++x) {
                const hcpp::i32 dx =
                    rect.x - x > 0 ? rect.x - x : (x - rect.right() + 1 > 0 ? x - rect.right() + 1 : 0);
                const hcpp::i32 dy =
                    rect.y - y > 0 ? rect.y - y : (y - rect.bottom() + 1 > 0 ? y - rect.bottom() + 1 : 0);
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const hcpp::i32 d = isqrt_i32(dx * dx + dy * dy);
                if (d <= 0 || d > radius) {
                    continue;
                }
                const hcpp::u32 t =
                    static_cast<hcpp::u32>((radius - d) * 255 / radius);
                const hcpp::u32 dst =
                    fb.get_pixel(static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y));
                hcpp::u8 r = 0;
                hcpp::u8 g = 0;
                hcpp::u8 b = 0;
                fb.decode_rgb(dst, r, g, b);
                fb.put_pixel(
                    static_cast<hcpp::u32>(x), static_cast<hcpp::u32>(y),
                    fb.encode_rgb(
                        static_cast<hcpp::u8>(clamp_i32(r + sr * t / 255, 0, 255)),
                        static_cast<hcpp::u8>(clamp_i32(g + sg * t / 255, 0, 255)),
                        static_cast<hcpp::u8>(clamp_i32(b + sb * t / 255, 0, 255))));
            }
        }
    }

    bool gfx_selftest() noexcept {
        static hcpp::u8 pixels[96 * 96 * 4] = {};
        hcpp::graphics::Framebuffer fb(pixels, 96, 96, 96 * 4, 32, 8, 16, 8, 8, 8, 0);
        if (!fb.valid()) {
            return false;
        }
        Canvas canvas(fb);
        const hcpp::u32 bg = canvas.color(0x808080);

        canvas.fill_rect(Rect{0, 0, 96, 96}, bg);
        gfx_soft_shadow(canvas, Rect{32, 28, 32, 28}, 4, 8, 160);
        gfx_inner_shadow(canvas, Rect{32, 28, 32, 28}, 4, 6);
        const bool shadow_ok =
            pixel_luma(fb, fb.get_pixel(40, 62)) < pixel_luma(fb, bg) &&
            pixel_luma(fb, fb.get_pixel(33, 29)) < pixel_luma(fb, bg) &&
            fb.get_pixel(4, 4) == bg;
        serial::write_line(shadow_ok ? "gfx shadow=ok" : "gfx shadow=fail");

        gfx_gradient_v(canvas, Rect{0, 0, 48, 32}, 0x000000, 0xFFFFFF);
        gfx_gradient_h(canvas, Rect{48, 0, 48, 32}, 0xFF0000, 0x0000FF);
        gfx_gradient_radial(canvas, Rect{0, 32, 48, 48}, 0xFFFFFF, 0x000000);
        const bool grad_ok = pixel_near(fb, fb.get_pixel(24, 0), 0, 0, 0) &&
                             pixel_near(fb, fb.get_pixel(24, 31), 255, 255, 255) &&
                             pixel_near(fb, fb.get_pixel(48, 16), 255, 0, 0) &&
                             pixel_near(fb, fb.get_pixel(95, 16), 0, 0, 255) &&
                             pixel_near(fb, fb.get_pixel(24, 56), 255, 255, 255) &&
                             pixel_near(fb, fb.get_pixel(0, 79), 0, 0, 0);
        serial::write_line(grad_ok ? "gfx grad=ok" : "gfx grad=fail");

        canvas.fill_rect(Rect{0, 0, 96, 96}, canvas.color(0x101010));
        const hcpp::u32 dark_bg = fb.get_pixel(4, 4);
        gfx_elevation(canvas, Rect{24, 24, 32, 32}, 4);
        const bool elev_ok =
            pixel_luma(fb, fb.get_pixel(40, 40)) > pixel_luma(fb, dark_bg) &&
            pixel_luma(fb, fb.get_pixel(40, 62)) < pixel_luma(fb, dark_bg);
        serial::write_line(elev_ok ? "gfx elev=ok" : "gfx elev=fail");

        canvas.fill_rect(Rect{0, 0, 96, 96}, canvas.color(0x101010));
        gfx_glow(canvas, Rect{40, 40, 16, 16}, 0x00FF00, 6);
        hcpp::u8 gr = 0;
        hcpp::u8 gg = 0;
        hcpp::u8 gb = 0;
        fb.decode_rgb(fb.get_pixel(48, 38), gr, gg, gb);
        hcpp::u8 br = 0;
        hcpp::u8 bgc = 0;
        hcpp::u8 bb = 0;
        fb.decode_rgb(fb.get_pixel(4, 4), br, bgc, bb);
        const bool glow_ok =
            static_cast<hcpp::i32>(gg) > static_cast<hcpp::i32>(bgc) + 50 &&
            fb.get_pixel(4, 4) == fb.get_pixel(2, 2);
        serial::write_line(glow_ok ? "gfx glow=ok" : "gfx glow=fail");

        canvas.fill_rect(Rect{0, 0, 96, 96}, canvas.color(0x101010));
        gfx_inner_glow_edge(canvas, Rect{24, 24, 48, 48}, 8, 0x2365FF);
        hcpp::u8 er = 0;
        hcpp::u8 eg = 0;
        hcpp::u8 eb = 0;
        fb.decode_rgb(fb.get_pixel(48, 70), er, eg, eb);
        const hcpp::u32 edge_luma = pixel_luma(fb, fb.get_pixel(48, 70));
        const hcpp::u32 top_luma = pixel_luma(fb, fb.get_pixel(48, 25));
        const bool edge_ok =
            static_cast<hcpp::i32>(eb) > 120 &&
            edge_luma > pixel_luma(fb, fb.get_pixel(48, 40)) &&
            top_luma > pixel_luma(fb, fb.get_pixel(4, 4)) + 40;
        serial::write_line(edge_ok ? "gfx edge=ok" : "gfx edge=fail");

        return shadow_ok && grad_ok && elev_ok && glow_ok && edge_ok;
    }

}  // namespace hype::gfx
