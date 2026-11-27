#include "kernel/gdxf/gdxf.h"

#include "kernel/gdxf/gdxfint.h"
#include "kernel/memory/pmm.h"

namespace hype::gdxf {

    namespace {

        struct Tex {
            hcpp::u32* pixels = nullptr;
            hcpp::u32 w = 0;
            hcpp::u32 h = 0;
            hcpp::size_t pages = 0;
        };

        Tex texs[gdxf_max_textures];

        Tex* tex_get(hcpp::u32 handle) noexcept {
            if (handle == gdxf_invalid_tex || handle > gdxf_max_textures) {
                return nullptr;
            }
            Tex& tex = texs[handle - 1];
            return tex.pixels != nullptr ? &tex : nullptr;
        }

        hcpp::u32 blend_over(hcpp::u32 src, hcpp::u32 dst) noexcept {
            const hcpp::u32 a = src >> 24;
            if (a == 0) {
                return dst;
            }
            if (a == 255) {
                return src;
            }
            const hcpp::u32 inv = 255 - a;
            const hcpp::u32 r =
                ((src >> 16 & 0xFF) * a + (dst >> 16 & 0xFF) * inv) / 255;
            const hcpp::u32 g =
                ((src >> 8 & 0xFF) * a + (dst >> 8 & 0xFF) * inv) / 255;
            const hcpp::u32 b = ((src & 0xFF) * a + (dst & 0xFF) * inv) / 255;
            return 0xFF000000u | (r << 16) | (g << 8) | b;
        }

        void blit_tex(const Tex& tex, hype::ui::Rect src, hype::ui::Rect dst,
                      TexFlags flags) noexcept {
            if (!g_gdxf.surf_acquired || g_gdxf.backbuf == nullptr ||
                src.empty() || dst.empty() || src.x < 0 || src.y < 0 ||
                src.right() > static_cast<hcpp::i32>(tex.w) ||
                src.bottom() > static_cast<hcpp::i32>(tex.h)) {
                return;
            }
            const hype::ui::Rect bounds{
                0, 0, static_cast<hcpp::i32>(g_gdxf.surf_w),
                static_cast<hcpp::i32>(g_gdxf.surf_h)};
            const hype::ui::Rect d = dst.intersection(bounds);
            if (d.empty()) {
                return;
            }
            const bool flip_x = gdxf_tex_flag(flags, TexFlags::FlipX);
            const bool flip_y = gdxf_tex_flag(flags, TexFlags::FlipY);
            const bool alpha = gdxf_tex_flag(flags, TexFlags::Alpha);
            const bool same_size = src.w == dst.w && src.h == dst.h;
            auto* back = reinterpret_cast<hcpp::u32*>(g_gdxf.backbuf);
            for (hcpp::i32 y = d.y; y < d.bottom(); ++y) {
                hcpp::i32 v = same_size ? y - dst.y
                                        : (y - dst.y) * src.h / dst.h;
                if (flip_y) {
                    v = src.h - 1 - v;
                }
                const hcpp::u32* srow =
                    tex.pixels + static_cast<hcpp::size_t>(src.y + v) * tex.w;
                hcpp::u32* drow =
                    back + static_cast<hcpp::size_t>(y) * g_gdxf.surf_w;
                for (hcpp::i32 x = d.x; x < d.right(); ++x) {
                    hcpp::i32 u = same_size ? x - dst.x
                                            : (x - dst.x) * src.w / dst.w;
                    if (flip_x) {
                        u = src.w - 1 - u;
                    }
                    const hcpp::u32 px = srow[src.x + u];
                    if (alpha) {
                        drow[x] = blend_over(px, drow[x]);
                    } else {
                        drow[x] = px;
                    }
                }
            }
            gdxf_dirty_add(d);
        }

    }  // namespace

    hcpp::u32 gdxf_tex_create(hcpp::u32 w, hcpp::u32 h,
                              const hcpp::u32* argb) noexcept {
        if (!g_gdxf.initialized || w == 0 || h == 0 || w > 2048 || h > 2048) {
            return gdxf_invalid_tex;
        }
        for (hcpp::u32 i = 0; i < gdxf_max_textures; ++i) {
            Tex& tex = texs[i];
            if (tex.pixels != nullptr) {
                continue;
            }
            const hcpp::size_t pages =
                (static_cast<hcpp::size_t>(w) * h * 4 + memory::page_size - 1) /
                memory::page_size;
            const hcpp::uintptr_t addr = memory::pmm_allocate_pages(pages);
            if (addr == 0) {
                return gdxf_invalid_tex;
            }
            tex.pixels = reinterpret_cast<hcpp::u32*>(addr);
            tex.w = w;
            tex.h = h;
            tex.pages = pages;
            const hcpp::size_t count = static_cast<hcpp::size_t>(w) * h;
            for (hcpp::size_t p = 0; p < count; ++p) {
                tex.pixels[p] = argb != nullptr ? argb[p] : 0xFF000000u;
            }
            return i + 1;
        }
        return gdxf_invalid_tex;
    }

    void gdxf_tex_destroy(hcpp::u32 tex) noexcept {
        Tex* target = tex_get(tex);
        if (target == nullptr) {
            return;
        }
        memory::pmm_free_pages(reinterpret_cast<hcpp::uintptr_t>(target->pixels),
                               target->pages);
        *target = Tex{};
    }

    void gdxf_sprite_batch_begin() noexcept {
        g_gdxf.dirty_count = 0;
        g_gdxf.batch_open = true;
    }

    void gdxf_sprite_draw(hcpp::u32 tex, hcpp::i32 x, hcpp::i32 y,
                          TexFlags flags) noexcept {
        const Tex* source = tex_get(tex);
        if (source == nullptr) {
            return;
        }
        blit_tex(*source,
                 hype::ui::Rect{0, 0, static_cast<hcpp::i32>(source->w),
                                static_cast<hcpp::i32>(source->h)},
                 hype::ui::Rect{x, y, static_cast<hcpp::i32>(source->w),
                                static_cast<hcpp::i32>(source->h)},
                 flags);
    }

    void gdxf_sprite_draw_ex(hcpp::u32 tex, hype::ui::Rect src,
                             hype::ui::Rect dst, TexFlags flags) noexcept {
        const Tex* source = tex_get(tex);
        if (source == nullptr) {
            return;
        }
        blit_tex(*source, src, dst, flags);
    }

    void gdxf_sprite_batch_end() noexcept {
        g_gdxf.batch_open = false;  // dirty list stays live until present
    }

    void gdxf_tilemap_draw(hcpp::u32 tex, hcpp::u32 tile_w, hcpp::u32 tile_h,
                           const hcpp::u8* map, hcpp::u32 map_w, hcpp::u32 map_h,
                           hcpp::i32 cam_x, hcpp::i32 cam_y) noexcept {
        const Tex* source = tex_get(tex);
        if (source == nullptr || map == nullptr || tile_w == 0 || tile_h == 0 ||
            map_w == 0 || map_h == 0) {
            return;
        }
        const hcpp::u32 tiles_per_row = source->w / tile_w;
        if (tiles_per_row == 0 || source->h < tile_h) {
            return;
        }
        const hcpp::i32 surf_w = static_cast<hcpp::i32>(g_gdxf.surf_w);
        const hcpp::i32 surf_h = static_cast<hcpp::i32>(g_gdxf.surf_h);
        hcpp::i32 row0 = cam_y / static_cast<hcpp::i32>(tile_h);
        hcpp::i32 col0 = cam_x / static_cast<hcpp::i32>(tile_w);
        if (row0 < 0) {
            row0 = 0;
        }
        if (col0 < 0) {
            col0 = 0;
        }
        const hcpp::i32 row1 =
            (cam_y + surf_h + static_cast<hcpp::i32>(tile_h) - 1) /
            static_cast<hcpp::i32>(tile_h);
        const hcpp::i32 col1 =
            (cam_x + surf_w + static_cast<hcpp::i32>(tile_w) - 1) /
            static_cast<hcpp::i32>(tile_w);
        for (hcpp::i32 my = row0; my < row1 && my < static_cast<hcpp::i32>(map_h);
             ++my) {
            for (hcpp::i32 mx = col0;
                 mx < col1 && mx < static_cast<hcpp::i32>(map_w); ++mx) {
                const hcpp::u8 cell = map[my * map_w + mx];
                if (cell == 0) {
                    continue;  // 0 = empty tile
                }
                const hcpp::u32 index = cell - 1;
                const hype::ui::Rect src{
                    static_cast<hcpp::i32>((index % tiles_per_row) * tile_w),
                    static_cast<hcpp::i32>((index / tiles_per_row) * tile_h),
                    static_cast<hcpp::i32>(tile_w),
                    static_cast<hcpp::i32>(tile_h)};
                const hype::ui::Rect dst{
                    mx * static_cast<hcpp::i32>(tile_w) - cam_x,
                    my * static_cast<hcpp::i32>(tile_h) - cam_y,
                    static_cast<hcpp::i32>(tile_w),
                    static_cast<hcpp::i32>(tile_h)};
                blit_tex(*source, src, dst, TexFlags::None);
            }
        }
    }

}  // namespace hype::gdxf
