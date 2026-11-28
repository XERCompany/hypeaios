#include "kernel/win/deskbg.h"

#include "kernel/codecapps/image/imageutil.h"
#include "kernel/display/hgc.h"
#include "kernel/display/scaler.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/win/winman.h"

// Wallpaper engine: a PMM-resident wall surface rendered at the live
// framebuffer resolution (capped at 3840x2160) plus an optional packed
// source for image wallpapers. Procedural designs (aurora / mesh / dusk /
// mono / nebula) are generated in bounded fixed-point math so they stay
// crisp at 1080p through 8K instead of being upscaled. desk_bg_paint_clip
// lazily regenerates / re-scales when the scanout grows past the surface.
//
// Serial markers: "wp apply=<name> <w>x<h>", "wp gen=<ms>", and (on the
// caller side) "wp img=<file> ok|fail".

namespace hype::win {

    namespace {

        constexpr hcpp::u32 src_max_dim = 4096;
        constexpr hcpp::u32 src_max_bytes = 32 * 1024 * 1024;
        constexpr hcpp::u32 wall_max_bytes =
            desk_bg_wall_max_w * desk_bg_wall_max_h * 4;
        constexpr hcpp::u32 q12 = 4096;  // normalized coord space 0..4096

        DeskBgMode bg_mode = DeskBgMode::Gradient;
        hcpp::u32* wall_px = nullptr;
        hcpp::u32 wall_w = 0;
        hcpp::u32 wall_h = 0;
        hcpp::uintptr_t wall_base = 0;
        hcpp::size_t wall_pages = 0;
        hcpp::u32* src_px = nullptr;
        hcpp::u32 src_w = 0;
        hcpp::u32 src_h = 0;
        hcpp::uintptr_t src_base = 0;
        hcpp::size_t src_pages = 0;
        hcpp::u32 proc_id = 0;
        bool grow_blocked = false;

        const char* const design_names[] = {"aurora", "mesh", "dusk", "mono",
                                            "nebula"};
        constexpr hcpp::u32 design_count =
            sizeof(design_names) / sizeof(design_names[0]);

        bool str_eq(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
            return a[i] == b[i];
        }

        hcpp::u32 hash32(hcpp::u32 v) noexcept {
            v ^= v >> 16;
            v *= 0x7FEB352Du;
            v ^= v >> 15;
            v *= 0x846CA68Bu;
            v ^= v >> 16;
            return v;
        }

        hcpp::u32 pack_px(hcpp::u32 r, hcpp::u32 g, hcpp::u32 b) noexcept {
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            return 0xFF000000u | (r << 16) | (g << 8) | b;
        }

        hcpp::u32 lerp_ch(hcpp::u32 a, hcpp::u32 b, hcpp::u32 t) noexcept {
            return static_cast<hcpp::u32>(
                static_cast<hcpp::i32>(a) +
                ((static_cast<hcpp::i32>(b) - static_cast<hcpp::i32>(a)) *
                     static_cast<hcpp::i32>(t) >>
                 8));
        }

        // Multi-stop gradient over the normalized Q12 domain.
        struct Stop {
            hcpp::u16 pos;
            hcpp::u8 r, g, b;
        };

        hcpp::u32 gradient_stops(const Stop* st, hcpp::u32 n,
                                 hcpp::u32 t) noexcept {
            if (t <= st[0].pos) {
                return pack_px(st[0].r, st[0].g, st[0].b);
            }
            for (hcpp::u32 i = 1; i < n; ++i) {
                if (t <= st[i].pos) {
                    const hcpp::u32 span = st[i].pos - st[i - 1].pos;
                    hcpp::u32 f =
                        span == 0 ? 256 : ((t - st[i - 1].pos) * 256) / span;
                    if (f > 256) f = 256;
                    return pack_px(lerp_ch(st[i - 1].r, st[i].r, f),
                                   lerp_ch(st[i - 1].g, st[i].g, f),
                                   lerp_ch(st[i - 1].b, st[i].b, f));
                }
            }
            return pack_px(st[n - 1].r, st[n - 1].g, st[n - 1].b);
        }

        // Soft radial glow: additive quadratic falloff bounded to a square
        // box of side 2*r (Q12 units) so the pixel loop stays bounded.
        struct Blob {
            hcpp::u16 x, y;  // center, Q12 normalized
            hcpp::u16 r;     // radius, Q12 normalized
            hcpp::u8 cr, cg, cb;
            hcpp::u8 a;  // peak additive strength
        };

        void blob_add(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h,
                      const Blob& bl) noexcept {
            const hcpp::u32 r2 = static_cast<hcpp::u32>(bl.r) * bl.r;
            if (r2 == 0 || bl.a == 0) {
                return;
            }
            const hcpp::u32 x_lo =
                bl.x > bl.r ? ((bl.x - bl.r) * w) >> 12 : 0;
            const hcpp::u32 x_hi =
                ((static_cast<hcpp::u32>(bl.x) + bl.r) * w + 4095) >> 12;
            const hcpp::u32 y_lo =
                bl.y > bl.r ? ((bl.y - bl.r) * h) >> 12 : 0;
            const hcpp::u32 y_hi =
                ((static_cast<hcpp::u32>(bl.y) + bl.r) * h + 4095) >> 12;
            for (hcpp::u32 y = y_lo; y < y_hi && y < h; ++y) {
                const hcpp::u32 ny = (y * q12) / h;
                const hcpp::i32 dy = static_cast<hcpp::i32>(ny) - bl.y;
                const hcpp::u32 dy2 = static_cast<hcpp::u32>(dy * dy);
                if (dy2 >= r2) {
                    continue;
                }
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = x_lo; x < x_hi && x < w; ++x) {
                    const hcpp::u32 nx = (x * q12) / w;
                    const hcpp::i32 dx = static_cast<hcpp::i32>(nx) - bl.x;
                    const hcpp::u32 d2 =
                        static_cast<hcpp::u32>(dx * dx) + dy2;
                    if (d2 >= r2) {
                        continue;
                    }
                    // gn = 1 - d2/r2 in 0..256, quadratic falloff.
                    const hcpp::u32 m = r2 - d2;
                    const hcpp::u32 gn = (m * 256) / r2;
                    const hcpp::u32 g = (static_cast<hcpp::u32>(bl.a) * gn * gn) >>
                                        16;
                    if (g == 0) {
                        continue;
                    }
                    const hcpp::u32 p = row[x];
                    const hcpp::u32 r = (p >> 16 & 0xFF) +
                                        (static_cast<hcpp::u32>(bl.cr) * g) / 255;
                    const hcpp::u32 gg = (p >> 8 & 0xFF) +
                                         (static_cast<hcpp::u32>(bl.cg) * g) / 255;
                    const hcpp::u32 b = (p & 0xFF) +
                                        (static_cast<hcpp::u32>(bl.cb) * g) / 255;
                    row[x] = pack_px(r, gg, b);
                }
            }
        }

        // ---- procedural designs ----------------------------------------

        void gen_aurora(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h) noexcept {
            static const Stop st[] = {{0, 10, 13, 30},
                                      {1365, 15, 24, 56},
                                      {2731, 19, 32, 74},
                                      {4096, 9, 10, 16}};
            for (hcpp::u32 y = 0; y < h; ++y) {
                const hcpp::u32 t = (y * q12) / h;
                const hcpp::u32 base = gradient_stops(st, 4, t);
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    row[x] = base;
                }
            }
            static const Blob blobs[] = {
                {983, 860, 1500, 46, 228, 200, 96},    // teal, upper-left
                {3277, 1180, 1350, 140, 92, 255, 84},  // violet, upper-right
                {1640, 2280, 950, 40, 112, 214, 46},   // deep blue, mid-left
                {2460, 3450, 1100, 255, 92, 168, 40},  // magenta, low-mid
            };
            for (hcpp::u32 i = 0; i < 4; ++i) {
                blob_add(dst, w, h, blobs[i]);
            }
        }

        void gen_mesh(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h) noexcept {
            // 3x3 control lattice, bilinear inside each cell; muted
            // jewel tones on a dark base (mesh-gradient style).
            static const hcpp::u8 c[9][3] = {
                {42, 28, 92}, {20, 60, 92}, {26, 42, 96},
                {74, 36, 100}, {32, 48, 116}, {24, 74, 100},
                {28, 20, 54}, {56, 34, 84}, {15, 17, 40}};
            for (hcpp::u32 y = 0; y < h; ++y) {
                const hcpp::u32 ny = (y * q12) / h;
                hcpp::u32 cy = (ny * 2) >> 12;
                if (cy > 1) cy = 1;
                const hcpp::u32 fy = ((ny * 2) & 4095) >> 4;  // 0..256
                const hcpp::u8* r0 = c[cy * 3];
                const hcpp::u8* r1 = c[(cy + 1) * 3];
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    const hcpp::u32 nx = (x * q12) / w;
                    hcpp::u32 cx = (nx * 2) >> 12;
                    if (cx > 1) cx = 1;
                    const hcpp::u32 fx = ((nx * 2) & 4095) >> 4;
                    const hcpp::u8* a = r0 + cx * 3;
                    const hcpp::u8* b = r1 + cx * 3;
                    const hcpp::u32 tr = lerp_ch(a[0], a[3], fx);
                    const hcpp::u32 tg = lerp_ch(a[1], a[4], fx);
                    const hcpp::u32 tb = lerp_ch(a[2], a[5], fx);
                    const hcpp::u32 br = lerp_ch(b[0], b[3], fx);
                    const hcpp::u32 bg = lerp_ch(b[1], b[4], fx);
                    const hcpp::u32 bb = lerp_ch(b[2], b[5], fx);
                    row[x] = pack_px(lerp_ch(tr, br, fy), lerp_ch(tg, bg, fy),
                                     lerp_ch(tb, bb, fy));
                }
            }
        }

        void gen_dusk(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h) noexcept {
            static const Stop st[] = {{0, 14, 10, 28},     {1600, 44, 22, 50},
                                      {2450, 92, 42, 52},  {2900, 198, 104, 60},
                                      {3180, 56, 30, 28},  {4096, 9, 7, 10}};
            for (hcpp::u32 y = 0; y < h; ++y) {
                const hcpp::u32 t = (y * q12) / h;
                const hcpp::u32 base = gradient_stops(st, 6, t);
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    row[x] = base;
                }
            }
            static const Blob blobs[] = {
                {2048, 2890, 820, 255, 172, 92, 122},  // sun on the horizon
                {1100, 2500, 950, 204, 84, 60, 30},    // warm scatter, left
                {3050, 2620, 900, 220, 110, 80, 26},   // warm scatter, right
            };
            for (hcpp::u32 i = 0; i < 3; ++i) {
                blob_add(dst, w, h, blobs[i]);
            }
        }

        void gen_mono(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h) noexcept {
            // Near-flat theme surface (#0B0B0E) with a +/-2 lift top-to-
            // bottom and a 2-step corner vignette — deliberately minimal.
            for (hcpp::u32 y = 0; y < h; ++y) {
                const hcpp::u32 ny = (y * q12) / h;
                const hcpp::i32 lift =
                    (2048 - static_cast<hcpp::i32>(ny)) * 4 >> 12;
                const hcpp::i32 dy = static_cast<hcpp::i32>(ny) - 2048;
                const hcpp::u32 dy2 = static_cast<hcpp::u32>(dy * dy);
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    const hcpp::u32 nx = (x * q12) / w;
                    const hcpp::i32 dx = static_cast<hcpp::i32>(nx) - 2048;
                    const hcpp::u32 vig =
                        (static_cast<hcpp::u32>(dx * dx) + dy2) >> 22;
                    hcpp::i32 r = 11 + lift - static_cast<hcpp::i32>(vig);
                    hcpp::i32 g = 11 + lift - static_cast<hcpp::i32>(vig);
                    hcpp::i32 b = 14 + lift - static_cast<hcpp::i32>(vig);
                    if (r < 0) r = 0;
                    if (g < 0) g = 0;
                    if (b < 0) b = 0;
                    row[x] = pack_px(static_cast<hcpp::u32>(r),
                                     static_cast<hcpp::u32>(g),
                                     static_cast<hcpp::u32>(b));
                }
            }
        }

        // Nebula helpers: integer value-noise fbm, deterministic seed.
        constexpr hcpp::u32 neb_octaves = 5;
        constexpr hcpp::u32 neb_seed = 0x9E3779B9u;
        hcpp::u16 neb_acc[desk_bg_wall_max_w];

        hcpp::u32 neb_hash(hcpp::u32 ix, hcpp::u32 iy,
                           hcpp::u32 oct) noexcept {
            return hash32(ix * 0x8DA6B343u ^ iy * 0xD8163841u ^
                          neb_seed * (oct + 1)) &
                   0xFF;
        }

        hcpp::u32 smooth12(hcpp::u32 t) noexcept {
            const hcpp::u32 t2 = (t * t) >> 12;
            return (t2 * (3 * q12 - 2 * t)) >> 12;
        }

        // One octave pass over a row; accumulates v*amp into neb_acc.
        void neb_row(hcpp::u32 y, hcpp::u32 w, hcpp::u32 mind, hcpp::u32 freq,
                     hcpp::u32 amp, hcpp::u32 oct) noexcept {
            const hcpp::u32 ny = (y * q12 * freq) / mind;
            const hcpp::u32 iy = ny >> 12;
            const hcpp::u32 sy = smooth12(ny & 4095);
            hcpp::u32 ix_prev = 0xFFFFFFFFu;
            hcpp::u32 h00 = 0, h10 = 0, h01 = 0, h11 = 0;
            for (hcpp::u32 x = 0; x < w; ++x) {
                const hcpp::u32 nx = (x * q12 * freq) / mind;
                const hcpp::u32 ix = nx >> 12;
                if (ix != ix_prev) {
                    h00 = neb_hash(ix, iy, oct);
                    h10 = neb_hash(ix + 1, iy, oct);
                    h01 = neb_hash(ix, iy + 1, oct);
                    h11 = neb_hash(ix + 1, iy + 1, oct);
                    ix_prev = ix;
                }
                const hcpp::u32 sx = smooth12(nx & 4095);
                const hcpp::i32 top =
                    static_cast<hcpp::i32>(h00) +
                    ((static_cast<hcpp::i32>(h10) - static_cast<hcpp::i32>(h00)) *
                         static_cast<hcpp::i32>(sx) >>
                     12);
                const hcpp::i32 bot =
                    static_cast<hcpp::i32>(h01) +
                    ((static_cast<hcpp::i32>(h11) - static_cast<hcpp::i32>(h01)) *
                         static_cast<hcpp::i32>(sx) >>
                     12);
                const hcpp::i32 v =
                    top + ((bot - top) * static_cast<hcpp::i32>(sy) >> 12);
                neb_acc[x] = static_cast<hcpp::u16>(
                    neb_acc[x] + static_cast<hcpp::u32>(v < 0 ? 0 : v) * amp);
            }
        }

        void gen_nebula(hcpp::u32* dst, hcpp::u32 w, hcpp::u32 h) noexcept {
            // Noise coordinates are scaled by the smaller dimension so the
            // fbm cells stay square on ultrawide / portrait panels.
            const hcpp::u32 mind = w < h ? w : h;
            static const Stop st[] = {
                {0, 6, 8, 20},    {1160, 20, 16, 52}, {2000, 54, 36, 102},
                {2700, 92, 60, 158}, {3400, 128, 146, 206}, {4096, 158, 192, 240}};
            // Amplitudes sum to 256: 128+64+32+24+8.
            static const hcpp::u32 amps[neb_octaves] = {128, 64, 32, 24, 8};
            for (hcpp::u32 y = 0; y < h; ++y) {
                for (hcpp::u32 x = 0; x < w; ++x) {
                    neb_acc[x] = 0;
                }
                for (hcpp::u32 o = 0; o < neb_octaves; ++o) {
                    neb_row(y, w, mind, 1u << o, amps[o], o);
                }
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    const hcpp::u32 n = neb_acc[x] >> 8;
                    row[x] = gradient_stops(st, 6, (n * q12) / 255);
                }
            }
            static const Blob blobs[] = {
                {1150, 1250, 1300, 60, 160, 220, 42},   // cyan wash
                {3050, 950, 1500, 180, 70, 200, 32},    // magenta wash
            };
            for (hcpp::u32 i = 0; i < 2; ++i) {
                blob_add(dst, w, h, blobs[i]);
            }
            // Deterministic star field, two brightness classes.
            for (hcpp::u32 y = 0; y < h; ++y) {
                hcpp::u32* row = dst + static_cast<hcpp::size_t>(y) * w;
                for (hcpp::u32 x = 0; x < w; ++x) {
                    const hcpp::u32 s =
                        hash32(x * 2654435761u ^ y * 2246822519u ^ 0xA24BAED5u);
                    hcpp::u32 add = 0;
                    if (s % 6137 == 0) {
                        add = 200;
                    } else if (s % 1531 == 0) {
                        add = 110 + ((s >> 8) & 0x3F);
                    }
                    if (add == 0) {
                        continue;
                    }
                    const hcpp::u32 p = row[x];
                    row[x] = pack_px((p >> 16 & 0xFF) + add,
                                     (p >> 8 & 0xFF) + add, (p & 0xFF) + add);
                }
            }
        }

        void render_design(hcpp::u32 id) noexcept {
            if (wall_px == nullptr || wall_w == 0 || wall_h == 0) {
                return;
            }
            const hcpp::u64 t0 = hype::time::uptime_ms();
            switch (id) {
                case 1:
                    gen_mesh(wall_px, wall_w, wall_h);
                    break;
                case 2:
                    gen_dusk(wall_px, wall_w, wall_h);
                    break;
                case 3:
                    gen_mono(wall_px, wall_w, wall_h);
                    break;
                case 4:
                    gen_nebula(wall_px, wall_w, wall_h);
                    break;
                default:
                    gen_aurora(wall_px, wall_w, wall_h);
                    break;
            }
            const hcpp::u64 ms = hype::time::uptime_ms() - t0;
            serial::write("wp gen=");
            serial::write_dec32(static_cast<hcpp::u32>(ms));
            serial::write("\r\n");
        }

        void wp_apply_line(const char* name) noexcept {
            serial::write("wp apply=");
            serial::write(name != nullptr ? name : "?");
            serial::write(" ");
            serial::write_dec32(wall_w);
            serial::write("x");
            serial::write_dec32(wall_h);
            serial::write("\r\n");
        }

        // ---- surfaces ---------------------------------------------------

        void fb_target_dims(hcpp::u32& w, hcpp::u32& h) noexcept {
            w = desk_bg_wall_width;
            h = desk_bg_wall_height;
            const hcpp::graphics::Framebuffer* t =
                hype::display::hgc_state().target;
            if (t != nullptr && t->valid()) {
                w = t->width();
                h = t->height();
            }
            if (w < 64 || h < 64) {
                w = desk_bg_wall_width;
                h = desk_bg_wall_height;
            }
            if (w > desk_bg_wall_max_w) w = desk_bg_wall_max_w;
            if (h > desk_bg_wall_max_h) h = desk_bg_wall_max_h;
        }

        // Resize the wall surface. Keeps the old block when the new
        // allocation fails; falls back through a bounded size ladder so a
        // small-but-valid surface always beats none.
        bool ensure_wall(hcpp::u32 w, hcpp::u32 h) noexcept {
            if (wall_px != nullptr && wall_w == w && wall_h == h) {
                return true;
            }
            const hcpp::size_t bytes = static_cast<hcpp::size_t>(w) * h * 4;
            if (bytes == 0 || bytes > wall_max_bytes) {
                return false;
            }
            const hcpp::size_t pages =
                (bytes + hype::memory::page_size - 1) / hype::memory::page_size;
            const hcpp::uintptr_t mem = hype::memory::pmm_allocate_pages(pages);
            if (mem == 0) {
                if (w > 1920 || h > 1080) {
                    return ensure_wall(1920, 1080);
                }
                if (w > 1280 || h > 720) {
                    return ensure_wall(1280, 720);
                }
                return false;
            }
            if (wall_base != 0) {
                hype::memory::pmm_free_pages(wall_base, wall_pages);
            }
            wall_base = mem;
            wall_pages = pages;
            wall_px = reinterpret_cast<hcpp::u32*>(mem);
            wall_w = w;
            wall_h = h;
            return true;
        }

        bool ensure_src(hcpp::u32 w, hcpp::u32 h) noexcept {
            if (src_px != nullptr && src_w == w && src_h == h) {
                return true;
            }
            const hcpp::size_t bytes = static_cast<hcpp::size_t>(w) * h * 4;
            if (bytes == 0 || bytes > src_max_bytes) {
                return false;
            }
            const hcpp::size_t pages =
                (bytes + hype::memory::page_size - 1) / hype::memory::page_size;
            const hcpp::uintptr_t mem = hype::memory::pmm_allocate_pages(pages);
            if (mem == 0) {
                return false;
            }
            if (src_base != 0) {
                hype::memory::pmm_free_pages(src_base, src_pages);
            }
            src_base = mem;
            src_pages = pages;
            src_px = reinterpret_cast<hcpp::u32*>(mem);
            src_w = w;
            src_h = h;
            return true;
        }

        hcpp::u32 cover_scale_q16(hcpp::u32 src_w_, hcpp::u32 src_h_,
                                  hcpp::u32 dst_w, hcpp::u32 dst_h) noexcept {
            const hcpp::u32 sx = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(dst_w) << 16) / src_w_);
            const hcpp::u32 sy = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(dst_h) << 16) / src_h_);
            return sx > sy ? sx : sy;
        }

        void frame_to_packed(const hype::codec::FrameBuf& frame, hcpp::u32* dst,
                             hcpp::u32 stride) noexcept {
            const hcpp::u32 bpp =
                frame.format == video::PixelFormat::RGB32 ? 4 : 3;
            const hcpp::u32 pitch = hype::codec::image::source_pitch(frame);
            for (hcpp::u32 y = 0; y < frame.height; ++y) {
                const hcpp::u8* src_row =
                    frame.data + static_cast<hcpp::size_t>(y) * pitch;
                hcpp::u32* dst_row =
                    dst + static_cast<hcpp::size_t>(y) * stride;
                for (hcpp::u32 x = 0; x < frame.width; ++x) {
                    const hcpp::u8* p =
                        src_row + static_cast<hcpp::size_t>(x) * bpp;
                    const hcpp::u32 a = bpp == 4 ? p[3] : 0xFF;
                    dst_row[x] = (a << 24) |
                                 (static_cast<hcpp::u32>(p[0]) << 16) |
                                 (static_cast<hcpp::u32>(p[1]) << 8) | p[2];
                }
            }
        }

        // Cover-scale the packed image source into the wall surface.
        bool rescale_wall() noexcept {
            if (src_px == nullptr || wall_px == nullptr || src_w == 0 ||
                src_h == 0 || wall_w == 0 || wall_h == 0) {
                return false;
            }
            const hcpp::u32 scale =
                cover_scale_q16(src_w, src_h, wall_w, wall_h);
            hcpp::u32 eff_w = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(wall_w) << 16) / scale);
            hcpp::u32 eff_h = static_cast<hcpp::u32>(
                (static_cast<hcpp::u64>(wall_h) << 16) / scale);
            if (eff_w == 0) eff_w = 1;
            if (eff_h == 0) eff_h = 1;
            if (eff_w > src_w) eff_w = src_w;
            if (eff_h > src_h) eff_h = src_h;
            const hype::display::ScaleRect src_rect{
                (src_w - eff_w) / 2, (src_h - eff_h) / 2, eff_w, eff_h};
            const hype::display::ScaleRect dst_rect{0, 0, wall_w, wall_h};
            return hype::display::scale_bilinear(
                src_px, src_w, src_h, src_w, src_rect, wall_px, wall_w, wall_h,
                wall_w, dst_rect);
        }

        // Regrow the surface when the scanout outgrew it (res change).
        void lazy_grow(hcpp::u32 fw, hcpp::u32 fh) noexcept {
            hcpp::u32 tw = fw;
            hcpp::u32 th = fh;
            if (tw > desk_bg_wall_max_w) tw = desk_bg_wall_max_w;
            if (th > desk_bg_wall_max_h) th = desk_bg_wall_max_h;
            if (tw <= wall_w && th <= wall_h) {
                return;
            }
            if (!ensure_wall(tw, th)) {
                grow_blocked = true;
                return;
            }
            if (bg_mode == DeskBgMode::Procedural) {
                render_design(proc_id);
            } else if (bg_mode == DeskBgMode::Image) {
                rescale_wall();
            }
            if (wall_w < tw || wall_h < th) {
                grow_blocked = true;
            }
        }

    }

    bool desk_bg_set_image(const hype::codec::FrameBuf& frame) noexcept {
        if (!hype::codec::image::frame_source_valid(&frame) ||
            frame.width > src_max_dim || frame.height > src_max_dim ||
            static_cast<hcpp::size_t>(frame.width) * frame.height * 4 >
                src_max_bytes) {
            return false;
        }
        hcpp::u32 tw = 0;
        hcpp::u32 th = 0;
        fb_target_dims(tw, th);
        grow_blocked = false;
        if (!ensure_src(frame.width, frame.height)) {
            return false;
        }
        frame_to_packed(frame, src_px, src_w);
        if (!ensure_wall(tw, th) || !rescale_wall()) {
            return false;
        }
        bg_mode = DeskBgMode::Image;
        wm_invalidate_all();
        return true;
    }

    bool desk_bg_apply_design(const char* name) noexcept {
        const hcpp::i32 idx = desk_bg_design_index(name);
        if (idx < 0) {
            serial::write("wp apply=");
            serial::write(name != nullptr ? name : "?");
            serial::write_line(" fail");
            return false;
        }
        hcpp::u32 tw = 0;
        hcpp::u32 th = 0;
        fb_target_dims(tw, th);
        proc_id = static_cast<hcpp::u32>(idx);
        grow_blocked = false;
        if (!ensure_wall(tw, th) || wall_px == nullptr) {
            serial::write("wp apply=");
            serial::write(name);
            serial::write_line(" fail");
            return false;
        }
        render_design(proc_id);
        bg_mode = DeskBgMode::Procedural;
        wm_invalidate_all();
        wp_apply_line(name);
        return true;
    }

    hcpp::i32 desk_bg_design_index(const char* name) noexcept {
        if (name == nullptr) {
            return -1;
        }
        for (hcpp::u32 i = 0; i < design_count; ++i) {
            if (str_eq(design_names[i], name)) {
                return static_cast<hcpp::i32>(i);
            }
        }
        return -1;
    }

    hcpp::u32 desk_bg_design_count() noexcept { return design_count; }

    const char* desk_bg_design_name(hcpp::u32 index) noexcept {
        return index < design_count ? design_names[index] : nullptr;
    }

    const char* desk_bg_mode_name() noexcept {
        switch (bg_mode) {
            case DeskBgMode::Image:
                return "image";
            case DeskBgMode::Procedural:
                return design_names[proc_id];
            default:
                return "gradient";
        }
    }

    void desk_bg_surface_size(hcpp::u32& w, hcpp::u32& h) noexcept {
        w = wall_w;
        h = wall_h;
    }

    void desk_bg_reset() noexcept {
        bg_mode = DeskBgMode::Gradient;
        grow_blocked = false;
        hcpp::u32 tw = 0;
        hcpp::u32 th = 0;
        fb_target_dims(tw, th);
        serial::write("wp apply=gradient ");
        serial::write_dec32(tw);
        serial::write("x");
        serial::write_dec32(th);
        serial::write("\r\n");
        wm_invalidate_all();
    }

    DeskBgMode desk_bg_mode() noexcept { return bg_mode; }

    bool desk_bg_ready() noexcept {
        return bg_mode != DeskBgMode::Gradient && wall_px != nullptr;
    }

    hcpp::u32 desk_bg_sample(hcpp::u32 x, hcpp::u32 y) noexcept {
        if (wall_px == nullptr || x >= wall_w || y >= wall_h) {
            return 0;
        }
        return wall_px[static_cast<hcpp::size_t>(y) * wall_w + x];
    }

    bool desk_bg_paint_clip(hcpp::graphics::Framebuffer& fb,
                            hype::ui::Rect clip) noexcept {
        if (!fb.valid() || !desk_bg_ready()) {
            return false;
        }
        const hcpp::u32 fw = fb.width();
        const hcpp::u32 fh = fb.height();
        if (fw == 0 || fh == 0) {
            return false;
        }
        if (!grow_blocked) {
            lazy_grow(fw, fh);
        }
        const hype::ui::Rect screen{0, 0, static_cast<hcpp::i32>(fw),
                                    static_cast<hcpp::i32>(fh)};
        const hype::ui::Rect area = clip.empty()
                                        ? screen
                                        : clip.intersection(screen);
        if (area.empty()) {
            return true;
        }
        const hcpp::u32 scale = cover_scale_q16(wall_w, wall_h, fw, fh);
        hcpp::u32 eff_w =
            static_cast<hcpp::u32>((static_cast<hcpp::u64>(fw) << 16) / scale);
        hcpp::u32 eff_h =
            static_cast<hcpp::u32>((static_cast<hcpp::u64>(fh) << 16) / scale);
        if (eff_w == 0) eff_w = 1;
        if (eff_h == 0) eff_h = 1;
        if (eff_w > wall_w) eff_w = wall_w;
        if (eff_h > wall_h) eff_h = wall_h;
        const hcpp::u32 off_x = (wall_w - eff_w) / 2;
        const hcpp::u32 off_y = (wall_h - eff_h) / 2;
        for (hcpp::u32 y = static_cast<hcpp::u32>(area.y);
             y < static_cast<hcpp::u32>(area.bottom()); ++y) {
            const hcpp::u32 sy = off_y + (y * eff_h) / fh;
            const hcpp::u32* row =
                wall_px + static_cast<hcpp::size_t>(sy) * wall_w;
            for (hcpp::u32 x = static_cast<hcpp::u32>(area.x);
                 x < static_cast<hcpp::u32>(area.right()); ++x) {
                const hcpp::u32 sx = off_x + (x * eff_w) / fw;
                const hcpp::u32 px = row[sx];
                fb.put_pixel(x, y,
                             fb.encode_rgb(static_cast<hcpp::u8>(px >> 16),
                                           static_cast<hcpp::u8>(px >> 8),
                                           static_cast<hcpp::u8>(px)));
            }
        }
        return true;
    }

    bool desk_bg_paint(hcpp::graphics::Framebuffer& fb) noexcept {
        return desk_bg_paint_clip(fb, hype::ui::Rect{});
    }

}
