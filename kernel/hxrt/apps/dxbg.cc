#include "kernel/hxrt/apps/dxbg.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/registry.h"
#include "kernel/display/scaler.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/listbox.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/ui/widgets/separator.h"
#include "kernel/vfs/hypeblob.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/deskbg.h"

namespace hype::hxrt {

    namespace {

        constexpr const char* wall_dir = "HYPEOS://U/wallpapers";
        constexpr hcpp::u32 seed_w = 160;
        constexpr hcpp::u32 seed_h = 90;
        constexpr hcpp::u32 seed_count = 3;
        constexpr hcpp::u32 prev_w = 96;
        constexpr hcpp::u32 prev_h = 54;
        constexpr hcpp::u32 file_cap = 96 * 1024;
        constexpr hcpp::size_t io_arena_pages = 96;

        const char* const seed_names[seed_count] = {"sky.png", "dusk.png",
                                                    "grove.png"};

        hype::codec::Arena io_arena{};
        bool io_ready = false;
        bool seeded = false;
        hcpp::u8 seed_px[seed_w * seed_h * 4];
        hcpp::u8 enc_buf[64 * 1024];
        hcpp::u32 preview_px[prev_w * prev_h];
        bool preview_valid = false;
        char wall_names[dxbg_max_wallpapers][hype::vfs::fs_name_size];
        hcpp::u32 wall_count = 0;
        hcpp::u32 active_wall = 0;

        void text_copy(char* dst, const char* src, hcpp::u32 cap) noexcept {
            hcpp::u32 n = 0;
            while (src != nullptr && src[n] != '\0' && n + 1 < cap) {
                dst[n] = src[n];
                ++n;
            }
            dst[n] = '\0';
        }

        void path_join(char* out, hcpp::u32 cap, const char* dir,
                       const char* leaf) noexcept {
            hcpp::u32 n = 0;
            while (dir != nullptr && dir[n] != '\0' && n + 1 < cap) {
                out[n] = dir[n];
                ++n;
            }
            if (n + 1 < cap) {
                out[n++] = '/';
            }
            while (leaf != nullptr && *leaf != '\0' && n + 1 < cap) {
                out[n++] = *leaf++;
            }
            out[n] = '\0';
        }

        hcpp::u32 hash32(hcpp::u32 v) noexcept {
            v ^= v >> 16;
            v *= 0x7FEB352Du;
            v ^= v >> 15;
            v *= 0x846CA68Bu;
            v ^= v >> 16;
            return v;
        }

        void render_wall(hcpp::u32 variant, hcpp::u8* out) noexcept {
            for (hcpp::u32 y = 0; y < seed_h; ++y) {
                const hcpp::u32 t = (y * 255) / (seed_h - 1);
                for (hcpp::u32 x = 0; x < seed_w; ++x) {
                    hcpp::u32 r = 0;
                    hcpp::u32 g = 0;
                    hcpp::u32 b = 0;
                    switch (variant) {
                        case 0:
                            r = 10 + t / 6;
                            g = 24 + t / 2;
                            b = 80 + t / 2;
                            if (hash32(x * 73856093u ^ y * 19349663u) % 401 == 0) {
                                r = 240;
                                g = 244;
                                b = 255;
                            }
                            break;
                        case 1:
                            r = 60 + (t * 170) / 255;
                            g = 30 + (t * 95) / 255;
                            b = 95 > (t * 55) / 255 ? 95 - (t * 55) / 255 : 0;
                            if (hash32(x * 2654435761u ^ y * 974634211u) % 503 ==
                                0) {
                                r = r + 30 > 255 ? 255 : r + 30;
                                g = g + 20 > 255 ? 255 : g + 20;
                                b = b + 10 > 255 ? 255 : b + 10;
                            }
                            break;
                        default:
                            r = 20 + t / 8;
                            g = 70 + t / 3;
                            b = 30 + t / 8;
                            if (y > 55 + (x * 7) % 20) {
                                r = 12 + t / 12;
                                g = 48 + t / 4;
                                b = 20 + t / 10;
                            }
                            if (hash32(x * 40503u ^ y * 32749u) % 389 == 0) {
                                r = 180;
                                g = 230;
                                b = 160;
                            }
                            break;
                    }
                    hcpp::i32 dx = 0;
                    hcpp::i32 dy = 0;
                    hcpp::u32 glow = 0;
                    switch (variant) {
                        case 0:
                            dx = static_cast<hcpp::i32>(x) - 124;
                            dy = static_cast<hcpp::i32>(y) - 20;
                            glow = 144;
                            break;
                        case 1:
                            dx = static_cast<hcpp::i32>(x) - 80;
                            dy = static_cast<hcpp::i32>(y) - 62;
                            glow = 121;
                            break;
                        default:
                            glow = 0;
                            break;
                    }
                    if (glow != 0 &&
                        static_cast<hcpp::u32>(dx * dx + dy * dy) < glow) {
                        r = variant == 0 ? 235 : 250;
                        g = variant == 0 ? 240 : 220;
                        b = variant == 0 ? 245 : 140;
                    }
                    hcpp::u8* p = out + (static_cast<hcpp::size_t>(y) * seed_w +
                                         x) * 4;
                    p[0] = static_cast<hcpp::u8>(r);
                    p[1] = static_cast<hcpp::u8>(g);
                    p[2] = static_cast<hcpp::u8>(b);
                    p[3] = 0xFF;
                }
            }
        }

        bool ensure_io() noexcept {
            if (!io_ready) {
                io_ready =
                    hype::codec::arena_from_pmm(io_arena, io_arena_pages);
            }
            return io_ready;
        }

        bool decode_file(const char* path, hype::codec::FrameBuf& frame) noexcept {
            if (!ensure_io() || path == nullptr) {
                return false;
            }
            hype::codec::arena_reset(io_arena);
            hype::vfs::FsStat info{};
            if (hype::vfs::fs_stat(path, info) != hype::vfs::FsStatus::Ok ||
                info.size == 0 || info.size > file_cap ||
                (info.flags & hype::vfs::FS_DIR) != 0) {
                return false;
            }
            hcpp::u8* file = static_cast<hcpp::u8*>(
                hype::codec::arena_alloc(io_arena, info.size, 8));
            if (file == nullptr) {
                return false;
            }
            hcpp::u32 got = 0;
            if (hype::vfs::fs_read(path, file, info.size, &got) !=
                    hype::vfs::FsStatus::Ok ||
                got != info.size) {
                return false;
            }
            const hype::codec::CodecId id =
                hype::codec::codec_probe(file, info.size);
            if (id == hype::codec::CodecId::Unknown) {
                return false;
            }
            return hype::codec::codec_decode(id, file, info.size, &frame,
                                             io_arena) ==
                       hype::codec::CodecStatus::Ok &&
                   frame.data != nullptr;
        }

        void pack_frame(const hype::codec::FrameBuf& frame, hcpp::u32* dst,
                        hcpp::u32 stride) noexcept {
            const hcpp::u32 bpp =
                frame.format == video::PixelFormat::RGB32 ? 4 : 3;
            const hcpp::u32 pitch = frame.pitch >= frame.width * bpp
                                        ? frame.pitch
                                        : frame.width * bpp;
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

        bool load_preview(const char* path) noexcept {
            hype::codec::FrameBuf frame{};
            preview_valid = false;
            if (!decode_file(path, frame)) {
                return false;
            }
            const hcpp::size_t packed_bytes =
                static_cast<hcpp::size_t>(frame.width) * frame.height * 4;
            hcpp::u32* packed = static_cast<hcpp::u32*>(
                hype::codec::arena_alloc(io_arena, packed_bytes, 8));
            if (packed == nullptr) {
                return false;
            }
            pack_frame(frame, packed, frame.width);
            const hype::display::ScaleRect src_rect{0, 0, frame.width,
                                                    frame.height};
            const hype::display::ScaleRect dst_rect{0, 0, prev_w, prev_h};
            preview_valid = hype::display::scale_bilinear(
                packed, frame.width, frame.height, frame.width, src_rect,
                preview_px, prev_w, prev_h, prev_w, dst_rect);
            return preview_valid;
        }

        void dxbg_refresh_list() noexcept {
            hype::vfs::FsStat listing[dxbg_max_wallpapers];
            const hcpp::u32 found =
                hype::vfs::fs_list(wall_dir, listing, dxbg_max_wallpapers);
            wall_count = 0;
            const hcpp::u32 limit =
                found < dxbg_max_wallpapers ? found : dxbg_max_wallpapers;
            for (hcpp::u32 i = 0; i < limit; ++i) {
                if ((listing[i].flags & hype::vfs::FS_DIR) != 0) {
                    continue;
                }
                text_copy(wall_names[wall_count], listing[i].name,
                          hype::vfs::fs_name_size);
                ++wall_count;
            }
        }

        class PreviewBox : public hype::ui::Widget {
        public:
            hype::ui::Size measure(hype::ui::Size available) noexcept override {
                (void)available;
                return hype::ui::Size{210, 120};
            }

            void paint(hype::ui::Canvas& canvas) noexcept override {
                const hype::ui::Rect box = bounds();
                if (box.empty()) {
                    return;
                }
                const hype::ui::Theme& theme = hype::ui::theme_active();
                canvas.stroke_rect(box, canvas.color(theme.border));
                const hype::ui::Rect inner{box.x + 1, box.y + 1, box.w - 2,
                                           box.h - 2};
                if (inner.empty()) {
                    return;
                }
                if (!preview_valid) {
                    canvas.fill_rect(inner, canvas.color(theme.muted));
                    return;
                }
                for (hcpp::i32 y = 0; y < inner.h; ++y) {
                    const hcpp::u32 sy =
                        (static_cast<hcpp::u32>(y) * prev_h) /
                        static_cast<hcpp::u32>(inner.h);
                    const hcpp::u32* row = preview_px + sy * prev_w;
                    for (hcpp::i32 x = 0; x < inner.w; ++x) {
                        const hcpp::u32 sx =
                            (static_cast<hcpp::u32>(x) * prev_w) /
                            static_cast<hcpp::u32>(inner.w);
                        canvas.draw_pixel(inner.x + x, inner.y + y,
                                          canvas.color(row[sx] & 0xFFFFFFu));
                    }
                }
            }
        };

        void dxbg_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            static hype::ui::Panel panel;
            static hype::ui::Flex column{hype::ui::FlexDirection::Column};
            static hype::ui::Label title;
            static hype::ui::Separator rule;
            static hype::ui::Listbox walls;
            static PreviewBox preview;
            static hype::ui::Flex row{hype::ui::FlexDirection::Row};
            static hype::ui::Button apply_btn;
            static hype::ui::Button slide_btn;
            static hype::ui::Button reset_btn;
            static hype::ui::Label status;
            static bool built = false;
            if (!built) {
                const hype::ui::Theme& theme = hype::ui::theme_active();
                panel.set_title("DXBackgroundDesktop");
                column.set_padding(hype::ui::Insets::uniform(
                    static_cast<hcpp::i32>(theme.spacing_md + 4)));
                column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
                column.set_cross_align(hype::ui::CrossAlign::Stretch);
                title.set_text("Desktop wallpaper");
                title.set_text_size(16);
                dxbg_seed_wallpapers();
                dxbg_refresh_list();
                for (hcpp::u32 i = 0; i < wall_count; ++i) {
                    walls.add_option(hcpp::StringView(wall_names[i]));
                }
                walls.set_visible_count(4);
                if (wall_count != 0) {
                    walls.highlight(0);
                    char path[64];
                    path_join(path, sizeof(path), wall_dir, wall_names[0]);
                    load_preview(path);
                }
                apply_btn.set_label("Apply");
                slide_btn.set_label("Slideshow: off");
                reset_btn.set_label("Reset gradient");
                row.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
                row.add_child(&apply_btn);
                row.add_child(&slide_btn);
                row.add_child(&reset_btn);
                status.set_color(hype::ui::LabelColor::Muted);
                column.add_child(&title);
                column.add_child(&rule);
                column.add_child(&walls);
                column.add_child(&preview);
                column.add_child(&row);
                column.add_child(&status);
                panel.add_child(&column);
                built = true;
            }
            status.set_text(hype::win::desk_bg_mode() ==
                                    hype::win::DeskBgMode::Image
                                ? "mode: image"
                                : "mode: gradient");
            hype::ui::Canvas canvas(*ctx.fb);
            canvas.set_clip(ctx.paint_clip());
            panel.layout_tree(ctx.region);
            panel.paint_tree(canvas);
        }

        void dxbg_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::User), 0,
            "DXBackgroundDesktop",
            "HYPEOS://U/apps/DXBackgroundDesktop.hxer", dxbg_entry, dxbg_close,
            nullptr};

    }

    const HxerHeader* dxbg_app() noexcept { return &desc; }

    bool dxbg_seed_wallpapers() noexcept {
        if (seeded) {
            return true;
        }
        if (!ensure_io()) {
            return false;
        }
        hype::vfs::fs_mkdir(wall_dir);
        bool ok = true;
        for (hcpp::u32 i = 0; i < seed_count; ++i) {
            render_wall(i, seed_px);
            hype::codec::FrameBuf frame{seed_w,
                                        seed_h,
                                        seed_w * 4,
                                        video::PixelFormat::RGB32,
                                        seed_px,
                                        sizeof(seed_px),
                                        sizeof(seed_px)};
            hype::codec::arena_reset(io_arena);
            hcpp::size_t written = 0;
            if (hype::codec::codec_encode(hype::codec::CodecId::Png, &frame,
                                          enc_buf, sizeof(enc_buf), written,
                                          io_arena) !=
                    hype::codec::CodecStatus::Ok ||
                written == 0) {
                ok = false;
                continue;
            }
            char path[64];
            path_join(path, sizeof(path), wall_dir, seed_names[i]);
            if (hype::vfs::fs_write(path, enc_buf,
                                    static_cast<hcpp::u32>(written)) !=
                hype::vfs::FsStatus::Ok) {
                ok = false;
            }
        }
        seeded = ok;
        if (ok) {
            // Persist the freshly written wallpapers to the HYPEDATA blob.
            hype::vfs::hypeblob_sync();
        }
        return ok;
    }

    hcpp::u32 dxbg_wallpaper_count() noexcept { return wall_count; }

    const char* dxbg_wallpaper_name(hcpp::u32 index) noexcept {
        return index < wall_count ? wall_names[index] : nullptr;
    }

    bool dxbg_apply(const char* path) noexcept {
        hype::codec::FrameBuf frame{};
        if (!decode_file(path, frame)) {
            return false;
        }
        if (!hype::win::desk_bg_set_image(frame)) {
            return false;
        }
        load_preview(path);
        return true;
    }

    bool dxbg_apply_index(hcpp::u32 index) noexcept {
        if (index >= wall_count) {
            return false;
        }
        char path[64];
        path_join(path, sizeof(path), wall_dir, wall_names[index]);
        if (!dxbg_apply(path)) {
            return false;
        }
        active_wall = index;
        return true;
    }

    bool dxbg_selftest() noexcept {
        const bool seed_ok = dxbg_seed_wallpapers();
        dxbg_refresh_list();
        serial::write("dxbg walls=");
        serial::write_dec32(wall_count);
        serial::write("\r\n");
        bool applied = false;
        if (wall_count != 0) {
            applied = dxbg_apply_index(0);
        }
        serial::write("dxbg apply=");
        serial::write_line(applied ? "ok" : "fail");
        const bool image_mode =
            hype::win::desk_bg_mode() == hype::win::DeskBgMode::Image &&
            hype::win::desk_bg_ready();
        serial::write("desk bg=");
        serial::write_line(image_mode ? "image" : "gradient");
        bool pixel_ok = false;
        if (image_mode) {
            static hcpp::u8 test_mem[64 * 48 * 4];
            hcpp::graphics::Framebuffer test_fb(test_mem, 64, 48, 64 * 4, 32, 8,
                                                16, 8, 8, 8, 0);
            hype::ui::Canvas canvas(test_fb);
            const hype::ui::Theme& theme = hype::ui::theme_active();
            const hype::ui::Rect area{0, 0, 64, 48};
            canvas.set_clip(area);
            hype::gfx::gfx_gradient_v(canvas, area, theme.bg,
                                      hype::ui::rui_shade(theme.bg, 10));
            const hcpp::u32 grad_a = test_fb.get_pixel(8, 8);
            const hcpp::u32 grad_b = test_fb.get_pixel(40, 32);
            const bool painted = hype::win::desk_bg_paint(test_fb);
            pixel_ok = painted &&
                       (test_fb.get_pixel(8, 8) != grad_a ||
                        test_fb.get_pixel(40, 32) != grad_b);
        }
        serial::write("dxbg pixel=");
        serial::write_line(pixel_ok ? "ok" : "fail");
        (void)active_wall;
        return seed_ok && wall_count >= seed_count && applied && image_mode &&
               pixel_ok;
    }

}
