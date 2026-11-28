#include "kernel/ui/screenshot.h"

#include "hcpp/memory.hpp"
#include "kernel/display/hgc.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/ui/deskbar.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/winman.h"
#include "kernel/xdp/xdp.h"

namespace hype::ui {

    namespace {

        // Deferred-capture flags set by screenshot_request() (IRQ-safe)
        // and consumed by screenshot_pump() on the desktop loop.
        volatile bool pending_full = false;
        volatile bool pending_window = false;

        // Auto-numbered file counter; restarts at 001 each boot and skips
        // names that already exist, so persisted shots are never clobbered.
        hcpp::u32 shot_counter = 0;

        // Last successfully written file in "U:/shots/shotNNN.bmp" form —
        // screenshot_pump() uses it for the deskbar toast.
        char last_saved[64] = "";

        struct ShotSource {
            const hcpp::u8* pixels = nullptr;  // top-down rows
            hcpp::u32 width = 0;
            hcpp::u32 height = 0;
            hcpp::u32 pitch = 0;               // bytes per row
            bool bgra32 = false;               // fast path: 0xAARRGGBB px
            const hcpp::graphics::Framebuffer* fb = nullptr;  // fallback
        };

        // Picks the pixel source: the persistent XDP compositor surface
        // (final desktop frame) first, the bound VBE scanout otherwise.
        bool grab_source(ShotSource& src) noexcept {
            hype::xdp::xdp_surface* xsurf =
                hype::xdp::xdp_compositor_surface();
            if (xsurf != nullptr && xsurf->pixels != nullptr &&
                xsurf->width != 0 && xsurf->height != 0 &&
                xsurf->pitch_bytes != 0) {
                src.pixels = xsurf->pixels;
                src.width = xsurf->width;
                src.height = xsurf->height;
                src.pitch = xsurf->pitch_bytes;
                src.bgra32 = true;
                return true;
            }
            const hcpp::graphics::Framebuffer* fb =
                hype::display::hgc_state().target;
            if (fb != nullptr && fb->valid()) {
                const hcpp::u32 bpp = fb->bytes_per_pixel();
                if (bpp != 3 && bpp != 4) {
                    return false;
                }
                src.pixels = const_cast<const hcpp::u8*>(fb->base());
                src.width = fb->width();
                src.height = fb->height();
                src.pitch = fb->pitch();
                src.bgra32 = bpp == 4;
                src.fb = fb;
                return true;
            }
            return false;
        }

        void put_le16(hcpp::u8* p, hcpp::u16 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v);
            p[1] = static_cast<hcpp::u8>(v >> 8);
        }

        void put_le32(hcpp::u8* p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v);
            p[1] = static_cast<hcpp::u8>(v >> 8);
            p[2] = static_cast<hcpp::u8>(v >> 16);
            p[3] = static_cast<hcpp::u8>(v >> 24);
        }

        void text_copy(char* dst, hcpp::u32 capacity,
                       const char* src) noexcept {
            hcpp::u32 i = 0;
            while (src[i] != '\0' && i + 1 < capacity) {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = '\0';
        }

        // Writes "shot%03u.bmp" into `leaf` (>= 12 bytes).
        void format_leaf(char* leaf, hcpp::u32 number) noexcept {
            const char prefix[] = "shot";
            hcpp::u32 i = 0;
            while (prefix[i] != '\0') {
                leaf[i] = prefix[i];
                ++i;
            }
            leaf[i++] = static_cast<char>('0' + (number / 100) % 10);
            leaf[i++] = static_cast<char>('0' + (number / 10) % 10);
            leaf[i++] = static_cast<char>('0' + number % 10);
            const char suffix[] = ".bmp";
            hcpp::u32 j = 0;
            while (suffix[j] != '\0') {
                leaf[i++] = suffix[j++];
            }
            leaf[i] = '\0';
        }

        // Picks the next free "HYPEOS://U/shots/shotNNN.bmp" name. Returns
        // false when every NNN slot is taken (or the shots dir vanished).
        bool next_path(char* out, hcpp::u32 capacity) noexcept {
            constexpr char dir_prefix[] = "HYPEOS://U/shots/";
            for (hcpp::u32 tries = 0; tries < 999; ++tries) {
                shot_counter = shot_counter % 999 + 1;
                char leaf[16];
                format_leaf(leaf, shot_counter);
                hcpp::u32 i = 0;
                while (dir_prefix[i] != '\0' && i + 1 < capacity) {
                    out[i] = dir_prefix[i];
                    ++i;
                }
                hcpp::u32 j = 0;
                while (leaf[j] != '\0' && i + 1 < capacity) {
                    out[i++] = leaf[j++];
                }
                out[i] = '\0';
                hype::vfs::FsStat st{};
                if (hype::vfs::fs_stat(out, st) == hype::vfs::FsStatus::Ok) {
                    continue;  // name taken — try the next number
                }
                return true;
            }
            return false;
        }

        const char* fs_fail_name(hype::vfs::FsStatus status) noexcept {
            switch (status) {
                case hype::vfs::FsStatus::BadPath: return "badpath";
                case hype::vfs::FsStatus::BadDisk: return "baddisk";
                case hype::vfs::FsStatus::NotFound: return "notfound";
                case hype::vfs::FsStatus::IsDir: return "isdir";
                case hype::vfs::FsStatus::Exists: return "exists";
                case hype::vfs::FsStatus::AccessDenied: return "denied";
                case hype::vfs::FsStatus::NoSpace: return "nospace";
                case hype::vfs::FsStatus::BadFd: return "badfd";
                case hype::vfs::FsStatus::IoError: return "io";
                case hype::vfs::FsStatus::Unsupported: return "unsupported";
                default: return "vfs";
            }
        }

        void shot_fail(const char* why) noexcept {
            serial::write("shot fail=");
            serial::write_line(why);
        }

        // Encodes `rect` (already clipped to the source) as an
        // uncompressed 24-bit BMP and writes it to `path` via fs_write.
        bool capture_rect(const ShotSource& src, const Rect& rect,
                          const char* path) noexcept {
            const hcpp::u32 w = static_cast<hcpp::u32>(rect.w);
            const hcpp::u32 h = static_cast<hcpp::u32>(rect.h);
            const hcpp::u32 row_stride = (w * 3 + 3) & ~3u;
            const hcpp::u64 image_bytes =
                static_cast<hcpp::u64>(row_stride) * h;
            const hcpp::u64 total = 54 + image_bytes;
            if (total == 54 || total > 0xFFFFFFFFull) {
                shot_fail("size");
                return false;
            }
            const hcpp::size_t pages =
                static_cast<hcpp::size_t>(
                    (total + hype::memory::page_size - 1) /
                    hype::memory::page_size);
            const hcpp::uintptr_t mem =
                hype::memory::pmm_allocate_pages(pages);
            if (mem == 0) {
                shot_fail("alloc");
                return false;
            }
            hcpp::u8* buf = reinterpret_cast<hcpp::u8*>(mem);
            hcpp::set(buf, 0, 54);
            // BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40), positive
            // height stores rows bottom-up.
            buf[0] = 'B';
            buf[1] = 'M';
            put_le32(buf + 2, static_cast<hcpp::u32>(total));
            put_le32(buf + 10, 54);
            put_le32(buf + 14, 40);
            put_le32(buf + 18, w);
            put_le32(buf + 22, h);
            put_le16(buf + 26, 1);
            put_le16(buf + 28, 24);
            put_le32(buf + 34, static_cast<hcpp::u32>(image_bytes));
            put_le32(buf + 38, 2835);  // 72 dpi
            put_le32(buf + 42, 2835);
            const hcpp::u32 src_x = static_cast<hcpp::u32>(rect.x);
            const hcpp::u32 src_y = static_cast<hcpp::u32>(rect.y);
            for (hcpp::u32 y = 0; y < h; ++y) {
                hcpp::u8* dst =
                    buf + 54 + static_cast<hcpp::u64>(h - 1 - y) * row_stride;
                const hcpp::u8* row =
                    src.pixels +
                    static_cast<hcpp::u64>(src_y + y) * src.pitch +
                    static_cast<hcpp::u64>(src_x) *
                        (src.bgra32 ? 4 : 1);
                if (src.bgra32) {
                    const hcpp::u32* row32 =
                        reinterpret_cast<const hcpp::u32*>(row);
                    for (hcpp::u32 x = 0; x < w; ++x) {
                        const hcpp::u32 p = row32[x];
                        dst[x * 3 + 0] = static_cast<hcpp::u8>(p);        // B
                        dst[x * 3 + 1] = static_cast<hcpp::u8>(p >> 8);   // G
                        dst[x * 3 + 2] = static_cast<hcpp::u8>(p >> 16);  // R
                    }
                } else if (src.fb != nullptr) {
                    // Generic VBE layout: decode through the fb channel
                    // masks so 3-byte and odd channel orders stay right.
                    for (hcpp::u32 x = 0; x < w; ++x) {
                        hcpp::u8 r = 0, g = 0, b = 0;
                        src.fb->decode_rgb(
                            src.fb->get_pixel(src_x + x, src_y + y),
                            r, g, b);
                        dst[x * 3 + 0] = b;
                        dst[x * 3 + 1] = g;
                        dst[x * 3 + 2] = r;
                    }
                }
            }
            const hype::vfs::FsStatus status =
                hype::vfs::fs_write(path, buf, static_cast<hcpp::u32>(total));
            hype::memory::pmm_free_pages(mem, pages);
            if (status != hype::vfs::FsStatus::Ok) {
                shot_fail(fs_fail_name(status));
                return false;
            }
            // Short "U:/..." form for serial + toast readability:
            // "HYPEOS://U/shots/x.bmp" -> "U:/shots/x.bmp".
            last_saved[0] = 'U';
            last_saved[1] = ':';
            text_copy(last_saved + 2, sizeof(last_saved) - 2, path + 10);
            serial::write("shot saved=");
            serial::write(last_saved);
            serial::write(" bytes=");
            serial::write_dec32(static_cast<hcpp::u32>(total));
            serial::write("\r\n");
            return true;
        }

        bool capture(Rect rect) noexcept {
            ShotSource src{};
            if (!grab_source(src)) {
                shot_fail("nosrc");
                return false;
            }
            const Rect screen{
                0, 0, static_cast<hcpp::i32>(src.width),
                static_cast<hcpp::i32>(src.height)};
            const Rect clip =
                rect.empty() ? screen : rect.intersection(screen);
            if (clip.empty()) {
                shot_fail("empty");
                return false;
            }
            char path[64];
            if (!next_path(path, sizeof(path))) {
                shot_fail("name");
                return false;
            }
            // Best-effort mkdir every time; Exists is the steady state.
            const hype::vfs::FsStatus mk =
                hype::vfs::fs_mkdir("HYPEOS://U/shots");
            if (mk != hype::vfs::FsStatus::Ok &&
                mk != hype::vfs::FsStatus::Exists) {
                shot_fail(fs_fail_name(mk));
                return false;
            }
            return capture_rect(src, clip, path);
        }

    }  // namespace

    bool screenshot_full() noexcept { return capture(Rect{}); }

    bool screenshot_window(Rect rect) noexcept { return capture(rect); }

    void screenshot_request(bool window) noexcept {
        if (window) {
            pending_window = true;
        } else if (!pending_window) {
            pending_full = true;
        }
    }

    void screenshot_pump() noexcept {
        if (!pending_full && !pending_window) {
            return;
        }
        if (hype::session::session_locked()) {
            // Same contract as the WM chords: a locked session owns the
            // keyboard, so queued captures die instead of firing late.
            pending_full = false;
            pending_window = false;
            return;
        }
        const bool want_window = pending_window;
        pending_full = false;
        pending_window = false;
        bool ok;
        if (want_window) {
            const hype::win::Win* front = hype::win::wm_focused();
            ok = front != nullptr ? screenshot_window(front->frame)
                                  : screenshot_full();
        } else {
            ok = screenshot_full();
        }
        if (ok) {
            hype::ui::deskbar_notify("Screenshot saved", last_saved);
        }
    }

}
