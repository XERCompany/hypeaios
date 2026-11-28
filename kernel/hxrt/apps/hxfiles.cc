// HxFiles — the HypeAI OS file manager (.hxer app, issue #128).
//
// A real file manager over the VFS facade (kernel/vfs/hypefs.h): every
// listing comes from fs_list(), every operation goes through the real
// fs_* primitives — fs_mkdir/fs_write/fs_read/fs_delete/fs_rename —
// with a write-through debounced sync into the HYPEDATA blob handled
// by the VFS itself. Nothing here is a fake listing or a stub op; the
// only "Unsupported" corner is directory rename on the FAT32 mount,
// which the fs_rename() contract reports honestly.
//
// Window model: the "HxFiles" XAU class is registered at boot
// (hxfiles_register_xau, called from hxrt_register_builtins). When the
// shell wm_open()s "HxFiles" — Start menu, SpeedStart — the wm paint
// hook adopts the window into XAU and this WndProc gets
// CREATE/PAINT/LBUTTONDOWN/RBUTTONDOWN/KEYDOWN/CHAR/DESTROY. The .hxer
// entry point below is the non-XAU fallback (paints once through the
// shared body; all interaction lives in the WndProc).
//
// Serial markers: "hxfiles open=ok" on window create,
// "hxfiles nav=<path>" on every directory change, and
// "hxfiles op=<copy|paste|del|mkdir|rename> ok|fail" per operation.

#include "kernel/hxrt/apps/builtins.h"

#include "hcpp/memory.hpp"
#include "hcpp/text_writer.hpp"
#include "kernel/functionemitkeys.h"
#include "kernel/input/scancode.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"
#include "kernel/vfs/fat32.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/winman.h"
#include "kernel/win/xau.h"

namespace hype::hxrt {

    namespace {

        using hype::ui::Canvas;
        using hype::ui::Insets;
        using hype::ui::Point;
        using hype::ui::Rect;
        using hype::ui::Theme;
        using hype::vfs::FsStat;
        using hype::vfs::FsStatus;
        using hype::vfs::HypePath;

        constexpr hcpp::u32 fx_cap = 128;              // entries shown per dir
        constexpr hcpp::u32 fx_path_cap =
            hype::vfs::hypepath_max_length;            // 288
        constexpr hcpp::u32 fx_name_cap =
            hype::vfs::fs_name_size;                   // 32
        constexpr hcpp::u32 fx_preview_cap = 6144;     // inline preview bytes
        constexpr hcpp::u32 fx_copy_max = 4u * 1024u * 1024u;  // copy scratch cap
        constexpr hcpp::u64 fx_dblclick_ms = 500;

        constexpr hcpp::i32 fx_toolbar_h = 34;
        constexpr hcpp::i32 fx_addr_h = 26;
        constexpr hcpp::i32 fx_side_w = 118;
        constexpr hcpp::i32 fx_status_h = 22;
        constexpr hcpp::i32 fx_preview_h = 112;
        constexpr hcpp::i32 fx_row_h = 20;
        constexpr hcpp::i32 fx_head_h = 20;
        constexpr hcpp::i32 fx_cell_w = 96;
        constexpr hcpp::i32 fx_cell_h = 80;
        constexpr hcpp::i32 fx_sb_w = 10;              // scrollbar width
        constexpr hcpp::u8 fx_sel_alpha = 76;

        // Toolbar buttons.
        enum FxBtn : hcpp::u8 { BtnUp = 0, BtnRefresh, BtnNewDir, BtnView,
                                BtnCount };

        // Context-menu command ids (WmCtxItem::id).
        constexpr hcpp::u32 fx_ctx_open = 1;
        constexpr hcpp::u32 fx_ctx_copy = 2;
        constexpr hcpp::u32 fx_ctx_paste = 3;
        constexpr hcpp::u32 fx_ctx_newdir = 4;
        constexpr hcpp::u32 fx_ctx_delete = 5;
        constexpr hcpp::u32 fx_ctx_rename = 6;

        // Key codes (PS/2 set 1, already decoded by kernel/input).
        constexpr hcpp::u16 key_esc = 1;
        constexpr hcpp::u16 key_backspace = 14;
        constexpr hcpp::u16 key_enter = 28;
        constexpr hcpp::u16 key_ctrl = 29;
        constexpr hcpp::u16 key_a = 30;
        constexpr hcpp::u16 key_c = 46;
        constexpr hcpp::u16 key_v = 47;
        constexpr hcpp::u16 key_f2 = 60;
        constexpr hcpp::u16 key_f5 = 63;
        constexpr hcpp::u16 key_home = 71;
        constexpr hcpp::u16 key_up = 72;
        constexpr hcpp::u16 key_pgup = 73;
        constexpr hcpp::u16 key_end = 79;
        constexpr hcpp::u16 key_down = 80;
        constexpr hcpp::u16 key_pgdn = 81;
        constexpr hcpp::u16 key_del = 83;

        struct FxSideEntry {
            char label[16];
            char path[fx_path_cap];
        };

        // ---- state ----------------------------------------------------

        hype::win::XAU fx_win = hype::win::XAU_INVALID;
        bool fx_input_hooked = false;
        // Requested target dir for the next window create (Start-menu
        // file results call hxfiles_nav before the window exists).
        char fx_pending_nav[fx_path_cap]{};

        HypePath fx_cwd{};
        char fx_cwd_text[fx_path_cap]{};

        FsStat fx_items[fx_cap];
        hcpp::u16 fx_order[fx_cap];   // sorted view order (dirs first)
        hcpp::u8 fx_sel[fx_cap];      // selection flags, view order
        hcpp::u32 fx_total = 0;       // entries fs_list reported
        hcpp::u32 fx_shown = 0;       // min(fx_total, fx_cap)
        hcpp::u32 fx_sel_count = 0;
        hcpp::u32 fx_sel_bytes = 0;
        hcpp::i32 fx_focus = -1;      // focused row, view order
        hcpp::i32 fx_scroll = 0;      // first visible row
        bool fx_icons = false;        // view toggle: list / icons

        hcpp::i32 fx_last_hit = -1;
        hcpp::u64 fx_last_hit_ms = 0;

        // Clipboard: a single copied path (file or dir; dirs copy
        // recursively on paste).
        bool fx_clip_valid = false;
        bool fx_clip_dir = false;
        char fx_clip_src[fx_path_cap]{};
        char fx_clip_leaf[fx_name_cap]{};

        // Inline text preview (double-click / Enter on a file).
        bool fx_preview_on = false;
        bool fx_preview_binary = false;
        hcpp::u32 fx_preview_len = 0;
        hcpp::u32 fx_preview_size = 0;
        char fx_preview_name[fx_name_cap]{};
        hcpp::u8 fx_preview_buf[fx_preview_cap];

        // Inline rename edit (ctx Rename / F2).
        hcpp::i32 fx_rename_view = -1;    // view-order index or -1
        char fx_rename_orig[fx_name_cap]{};
        char fx_rename_buf[fx_name_cap]{};
        hcpp::u32 fx_rename_len = 0;

        // ---- layout (client-relative rects; `fx_last_client` holds the
        //      screen-space client rect — origin for to_screen() and for
        //      translating pointer messages; w/h feed relayout in clicks)

        Rect fx_last_client{};
        Rect fx_r_toolbar{};
        Rect fx_r_addr{};
        Rect fx_r_side{};
        Rect fx_r_main{};
        Rect fx_r_status{};
        Rect fx_r_preview{};
        Rect fx_r_btn[BtnCount]{};
        Rect fx_r_crumb[10]{};
        hcpp::u32 fx_crumb_depth[10]{};   // component depth to nav to
        hcpp::u32 fx_crumb_count = 0;
        Rect fx_r_side_rows[8]{};
        hcpp::i32 fx_main_visible = 0;
        hcpp::i32 fx_main_cols = 1;
        hcpp::i32 fx_main_rows = 0;

        FxSideEntry fx_sides[8];
        hcpp::u32 fx_side_count = 0;

        // ---- small helpers --------------------------------------------

        bool str_equal(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
            return a[i] == b[i];
        }

        char str_upper(char c) noexcept {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A')
                                        : c;
        }

        bool str_less(const char* a, const char* b) noexcept {
            hcpp::u32 i = 0;
            while (a[i] != '\0' && b[i] != '\0') {
                const char ca = str_upper(a[i]);
                const char cb = str_upper(b[i]);
                if (ca != cb) return ca < cb;
                ++i;
            }
            return a[i] == '\0' && b[i] != '\0';
        }

        hcpp::u32 str_copy(char* dst, const char* src,
                           hcpp::u32 cap) noexcept {
            hcpp::u32 n = 0;
            while (n + 1 < cap && src[n] != '\0') {
                dst[n] = src[n];
                ++n;
            }
            dst[n] = '\0';
            return n;
        }

        hcpp::u32 str_append(char* dst, const char* src, hcpp::u32 cap,
                             hcpp::u32 at) noexcept {
            while (at + 1 < cap && *src != '\0') {
                dst[at++] = *src++;
            }
            dst[at] = '\0';
            return at;
        }

        // Valid path-component characters per hypepath (printable, no
        // separators, no space).
        bool name_char_ok(char c) noexcept {
            const unsigned char b = static_cast<unsigned char>(c);
            return b >= 0x21 && b <= 0x7E && c != '/' && c != '\\';
        }

        const char* status_name(FsStatus st) noexcept {
            switch (st) {
                case FsStatus::Ok:           return "ok";
                case FsStatus::BadPath:      return "badpath";
                case FsStatus::BadDisk:      return "baddisk";
                case FsStatus::NotFound:     return "notfound";
                case FsStatus::IsDir:        return "isdir";
                case FsStatus::Exists:       return "exists";
                case FsStatus::AccessDenied: return "denied";
                case FsStatus::NoSpace:      return "nospace";
                case FsStatus::BadFd:        return "badfd";
                case FsStatus::IoError:      return "ioerror";
                case FsStatus::Unsupported:  return "unsupported";
            }
            return "?";
        }

        void op_marker(const char* op, FsStatus st) noexcept {
            serial::write("hxfiles op=");
            serial::write(op);
            serial::write_char(' ');
            serial::write(st == FsStatus::Ok ? "ok" : "fail");
            if (st != FsStatus::Ok) {
                serial::write(" err=");
                serial::write(status_name(st));
            }
            serial::write("\r\n");
        }

        void toast(const char* title, const char* msg) noexcept {
            hype::ui::deskbar_notify(title, msg);
        }

        void invalidate() noexcept {
            if (fx_win != hype::win::XAU_INVALID) {
                hype::win::xau_invalidate(fx_win, nullptr);
            }
        }

        // ---- path / listing -------------------------------------------

        void cwd_format() noexcept {
            hype::vfs::hypepath_format(fx_cwd, fx_cwd_text,
                                       sizeof(fx_cwd_text));
        }

        void sel_clear() noexcept {
            hcpp::set(fx_sel, 0, sizeof(fx_sel));
            fx_sel_count = 0;
            fx_sel_bytes = 0;
        }

        void sel_count_recalc() noexcept {
            fx_sel_count = 0;
            fx_sel_bytes = 0;
            for (hcpp::u32 i = 0; i < fx_shown; ++i) {
                if (fx_sel[i] != 0) {
                    ++fx_sel_count;
                    fx_sel_bytes += fx_items[fx_order[i]].size;
                }
            }
        }

        // Rebuilds `fx_order`: directories first, then files, each sorted
        // case-insensitively by name.
        void sort_listing() noexcept {
            for (hcpp::u32 i = 0; i < fx_shown; ++i) fx_order[i] = i;
            for (hcpp::u32 i = 1; i < fx_shown; ++i) {
                const hcpp::u16 key = fx_order[i];
                const bool key_dir =
                    (fx_items[key].flags & hype::vfs::FS_DIR) != 0;
                hcpp::u32 j = i;
                while (j > 0) {
                    const hcpp::u16 prev = fx_order[j - 1];
                    const bool prev_dir =
                        (fx_items[prev].flags & hype::vfs::FS_DIR) != 0;
                    const bool before =
                        key_dir != prev_dir
                            ? key_dir
                            : str_less(fx_items[key].name,
                                       fx_items[prev].name);
                    if (!before) break;
                    fx_order[j] = prev;
                    --j;
                }
                fx_order[j] = key;
            }
        }

        void scroll_clamp() noexcept {
            if (fx_scroll > fx_main_rows - fx_main_visible) {
                fx_scroll = fx_main_rows - fx_main_visible;
            }
            if (fx_scroll < 0) fx_scroll = 0;
        }

        void refresh() noexcept {
            fx_total = hype::vfs::fs_list(fx_cwd_text, fx_items, fx_cap);
            fx_shown = fx_total < fx_cap ? fx_total : fx_cap;
            sort_listing();
            sel_clear();
            fx_focus = fx_shown > 0 ? 0 : -1;
            fx_rename_view = -1;
            scroll_clamp();
        }

        void nav_to(const HypePath& p) noexcept {
            fx_cwd = p;
            cwd_format();
            fx_scroll = 0;
            fx_preview_on = false;
            refresh();
            serial::write("hxfiles nav=");
            serial::write_line(fx_cwd_text);
        }

        void nav_up() noexcept {
            if (fx_cwd.depth == 0) return;
            --fx_cwd.depth;
            nav_to(fx_cwd);
        }

        void nav_into(const char* name) noexcept {
            if (fx_cwd.depth >= hype::vfs::hypepath_max_components) return;
            str_copy(fx_cwd.components[fx_cwd.depth], name,
                     hype::vfs::hypepath_component_size);
            ++fx_cwd.depth;
            nav_to(fx_cwd);
        }

        void nav_text(const char* path) noexcept {
            HypePath p;
            if (hype::vfs::hypepath_parse(path, p) !=
                hype::vfs::HypePathStatus::Ok) {
                serial::write_line("hxfiles nav=badpath");
                return;
            }
            nav_to(p);
        }

        // Full path "HYPEOS://U/dir/name" for a view-order index.
        hcpp::u32 entry_path(hcpp::u32 view, char* out,
                             hcpp::u32 cap) noexcept {
            if (view >= fx_shown) {
                out[0] = '\0';
                return 0;
            }
            hcpp::u32 n = str_copy(out, fx_cwd_text, cap);
            if (fx_cwd.depth != 0) {
                // cwd_text already ends in '/' only at the disk root.
                if (n == 0 || out[n - 1] != '/') {
                    n = str_append(out, "/", cap, n);
                }
            }
            return str_append(out, fx_items[fx_order[view]].name, cap, n);
        }

        hcpp::i32 first_selected() noexcept {
            for (hcpp::u32 i = 0; i < fx_shown; ++i) {
                if (fx_sel[i] != 0) return static_cast<hcpp::i32>(i);
            }
            return -1;
        }

        void ensure_visible(hcpp::i32 view) noexcept {
            if (view < 0) return;
            const hcpp::i32 row =
                fx_icons ? view / fx_main_cols : view;
            if (row < fx_scroll) {
                fx_scroll = row;
            } else if (row >= fx_scroll + fx_main_visible) {
                fx_scroll = row - fx_main_visible + 1;
            }
            scroll_clamp();
        }

        // ---- sidebar ---------------------------------------------------

        void side_add(const char* label, const char* path) noexcept {
            if (fx_side_count >= 8) return;
            FsStat st;
            if (hype::vfs::fs_stat(path, st) != FsStatus::Ok ||
                (st.flags & hype::vfs::FS_DIR) == 0) {
                return;
            }
            FxSideEntry& e = fx_sides[fx_side_count];
            str_copy(e.label, label, sizeof(e.label));
            str_copy(e.path, path, sizeof(e.path));
            ++fx_side_count;
        }

        void places_build() noexcept {
            fx_side_count = 0;
            serial::write("hxfiles mounts=");
            side_add("U: User", "HYPEOS://U");
            side_add("S: System", "HYPEOS://S");
            side_add("T: Temp", "HYPEOS://T");
            if (hype::vfs::fat32_mounted()) {
                side_add("F: FAT32", "HYPEOS://F");
            }
            serial::write_dec32(fx_side_count);
            serial::write_char(' ');
            // Quick places under the mounts that exist.
            side_add("Home", "HYPEOS://U/home");
            side_add("Wallpapers", "HYPEOS://U/wallpapers");
            side_add("Apps", "HYPEOS://S/apps");
            serial::write_dec32(fx_side_count);
            serial::write("\r\n");
        }

        // ---- preview ---------------------------------------------------

        void preview_close() noexcept { fx_preview_on = false; }

        void preview_open(hcpp::u32 view) noexcept {
            if (view >= fx_shown) return;
            const FsStat& it = fx_items[fx_order[view]];
            char path[fx_path_cap];
            entry_path(view, path, sizeof(path));
            fx_preview_on = false;
            fx_preview_binary = false;
            fx_preview_len = 0;
            fx_preview_size = it.size;
            str_copy(fx_preview_name, it.name, sizeof(fx_preview_name));
            hcpp::u32 got = 0;
            const hcpp::u32 want =
                it.size < fx_preview_cap ? it.size : fx_preview_cap;
            if (hype::vfs::fs_read(path, fx_preview_buf, want, &got) !=
                FsStatus::Ok) {
                serial::write_line("hxfiles preview=fail");
                return;
            }
            // fs_read's out_size reports the full file size; the buffer
            // only holds `want` bytes — clamp the rendered length.
            fx_preview_len = got < want ? got : want;
            // Binary heuristic: a NUL byte in the first chunk means it is
            // not text — show the marker instead of garbage glyphs.
            for (hcpp::u32 i = 0; i < fx_preview_len && i < 512; ++i) {
                if (fx_preview_buf[i] == 0) {
                    fx_preview_binary = true;
                    break;
                }
            }
            fx_preview_on = true;
            serial::write("hxfiles preview=");
            serial::write_line(fx_preview_name);
        }

        // ---- file operations --------------------------------------------

        bool path_has_prefix(const char* text, const char* prefix) noexcept {
            hcpp::u32 i = 0;
            while (prefix[i] != '\0' && text[i] == prefix[i]) ++i;
            return prefix[i] == '\0' &&
                   (text[i] == '\0' || text[i] == '/');
        }

        // Real recursive copy through the facade: files move via
        // fs_read -> fs_write (page-granular PMM scratch), directories
        // via fs_mkdir + fs_list recursion (bounded by the path depth
        // limit). Returns the first failing status.
        FsStatus copy_path(const char* src, const char* dst,
                           hcpp::u32 depth) noexcept {
            if (depth >= hype::vfs::hypepath_max_components) {
                return FsStatus::Unsupported;
            }
            FsStat st;
            FsStatus s = hype::vfs::fs_stat(src, st);
            if (s != FsStatus::Ok) return s;
            if ((st.flags & hype::vfs::FS_DIR) != 0) {
                s = hype::vfs::fs_mkdir(dst);
                if (s != FsStatus::Ok) return s;
                // fs_list always fills from index 0 — there is no
                // pagination — so per-level scratch stays on the stack
                // (this recurses) and dirs larger than the cap report
                // Unsupported instead of a silent partial copy.
                FsStat kids[64];
                const hcpp::u32 n = hype::vfs::fs_list(src, kids, 64);
                if (n > 64) return FsStatus::Unsupported;
                for (hcpp::u32 i = 0; i < n; ++i) {
                    char cs[fx_path_cap];
                    char cd[fx_path_cap];
                    hcpp::u32 a = str_copy(cs, src, sizeof(cs));
                    hcpp::u32 b = str_copy(cd, dst, sizeof(cd));
                    a = str_append(cs, "/", sizeof(cs), a);
                    b = str_append(cd, "/", sizeof(cd), b);
                    str_append(cs, kids[i].name, sizeof(cs), a);
                    str_append(cd, kids[i].name, sizeof(cd), b);
                    s = copy_path(cs, cd, depth + 1);
                    if (s != FsStatus::Ok) return s;
                }
                return FsStatus::Ok;
            }
            // Regular file: whole-body copy through a PMM scratch buffer
            // (files are capped by the disk size; fx_copy_max keeps a
            // FAT32 file from eating the physical map).
            if (st.size > fx_copy_max) return FsStatus::Unsupported;
            hcpp::uintptr_t mem = 0;
            hcpp::u8* buf = nullptr;
            hcpp::size_t pages = 0;
            if (st.size != 0) {
                pages = (static_cast<hcpp::size_t>(st.size) +
                         hype::memory::page_size - 1) /
                        hype::memory::page_size;
                mem = hype::memory::pmm_allocate_pages(pages);
                if (mem == 0) return FsStatus::NoSpace;
                buf = reinterpret_cast<hcpp::u8*>(mem);
            }
            hcpp::u32 got = 0;
            s = hype::vfs::fs_read(src, buf, st.size, &got);
            if (s == FsStatus::Ok && got != st.size) s = FsStatus::IoError;
            if (s == FsStatus::Ok) s = hype::vfs::fs_write(dst, buf, st.size);
            if (mem != 0) hype::memory::pmm_free_pages(mem, pages);
            return s;
        }

        FsStatus op_copy() noexcept {
            const hcpp::i32 v = first_selected();
            if (v < 0) {
                op_marker("copy", FsStatus::NotFound);
                return FsStatus::NotFound;
            }
            const FsStat& it = fx_items[fx_order[v]];
            entry_path(static_cast<hcpp::u32>(v), fx_clip_src,
                       sizeof(fx_clip_src));
            str_copy(fx_clip_leaf, it.name, sizeof(fx_clip_leaf));
            fx_clip_dir = (it.flags & hype::vfs::FS_DIR) != 0;
            fx_clip_valid = true;
            op_marker("copy", FsStatus::Ok);
            return FsStatus::Ok;
        }

        FsStatus op_paste() noexcept {
            if (!fx_clip_valid) {
                op_marker("paste", FsStatus::NotFound);
                return FsStatus::NotFound;
            }
            char dst[fx_path_cap];
            hcpp::u32 n = str_copy(dst, fx_cwd_text, sizeof(dst));
            if (n == 0 || dst[n - 1] != '/') {
                n = str_append(dst, "/", sizeof(dst), n);
            }
            str_append(dst, fx_clip_leaf, sizeof(dst), n);
            // Refuse copying a directory into its own subtree and
            // clobbering an existing name.
            if (path_has_prefix(dst, fx_clip_src)) {
                op_marker("paste", FsStatus::BadPath);
                return FsStatus::BadPath;
            }
            FsStat existing;
            if (hype::vfs::fs_stat(dst, existing) == FsStatus::Ok) {
                op_marker("paste", FsStatus::Exists);
                toast("HxFiles", "Paste failed: name exists");
                return FsStatus::Exists;
            }
            const FsStatus st = copy_path(fx_clip_src, dst, 0);
            op_marker("paste", st);
            if (st == FsStatus::Ok) {
                refresh();
                toast("HxFiles", "Pasted");
            } else {
                toast("HxFiles", "Paste failed");
            }
            return st;
        }

        FsStatus op_delete() noexcept {
            if (fx_sel_count == 0) {
                op_marker("del", FsStatus::NotFound);
                return FsStatus::NotFound;
            }
            FsStatus worst = FsStatus::Ok;
            hcpp::u32 done = 0;
            for (hcpp::u32 i = 0; i < fx_shown; ++i) {
                if (fx_sel[i] == 0) continue;
                char path[fx_path_cap];
                entry_path(i, path, sizeof(path));
                const FsStatus st = hype::vfs::fs_delete(path);
                if (st == FsStatus::Ok) {
                    ++done;
                } else {
                    worst = st;
                }
            }
            op_marker("del", done == fx_sel_count ? FsStatus::Ok : worst);
            refresh();
            toast("HxFiles",
                  done == fx_sel_count ? "Deleted" : "Delete incomplete");
            return done == fx_sel_count ? FsStatus::Ok : worst;
        }

        FsStatus op_newdir() noexcept {
            char path[fx_path_cap];
            char leaf[fx_name_cap];
            for (hcpp::u32 k = 0; k < 10; ++k) {
                hcpp::u32 m = str_copy(leaf, "NewFolder", sizeof(leaf));
                if (k != 0) {
                    char digit[4];
                    hcpp::u32 dn = 0;
                    hcpp::u32 v = k;
                    if (v >= 10) digit[dn++] = '0' + (v / 10);
                    digit[dn++] = static_cast<char>('0' + (v % 10));
                    digit[dn] = '\0';
                    str_append(leaf, digit, sizeof(leaf), m);
                }
                hcpp::u32 n = str_copy(path, fx_cwd_text, sizeof(path));
                if (n == 0 || path[n - 1] != '/') {
                    n = str_append(path, "/", sizeof(path), n);
                }
                str_append(path, leaf, sizeof(path), n);
                const FsStatus st = hype::vfs::fs_mkdir(path);
                if (st == FsStatus::Exists) continue;
                op_marker("mkdir", st);
                if (st == FsStatus::Ok) {
                    refresh();
                    // Select the new folder.
                    for (hcpp::u32 i = 0; i < fx_shown; ++i) {
                        if (str_equal(fx_items[fx_order[i]].name, leaf)) {
                            fx_sel[i] = 1;
                            fx_focus = static_cast<hcpp::i32>(i);
                            sel_count_recalc();
                            ensure_visible(fx_focus);
                            break;
                        }
                    }
                    toast("HxFiles", "Folder created");
                } else {
                    toast("HxFiles", "New folder failed");
                }
                return st;
            }
            op_marker("mkdir", FsStatus::Exists);
            return FsStatus::Exists;
        }

        void rename_begin(hcpp::i32 view) noexcept {
            if (view < 0 || static_cast<hcpp::u32>(view) >= fx_shown) {
                return;
            }
            fx_rename_view = view;
            const FsStat& it = fx_items[fx_order[view]];
            fx_rename_len =
                str_copy(fx_rename_buf, it.name, sizeof(fx_rename_buf));
            str_copy(fx_rename_orig, it.name, sizeof(fx_rename_orig));
        }

        void rename_cancel() noexcept { fx_rename_view = -1; }

        FsStatus rename_commit() noexcept {
            if (fx_rename_view < 0) return FsStatus::BadPath;
            if (fx_rename_len == 0 ||
                str_equal(fx_rename_buf, fx_rename_orig)) {
                rename_cancel();
                return FsStatus::Ok;
            }
            char src[fx_path_cap];
            char dst[fx_path_cap];
            const hcpp::u32 view =
                static_cast<hcpp::u32>(fx_rename_view);
            entry_path(view, src, sizeof(src));
            hcpp::u32 n = str_copy(dst, fx_cwd_text, sizeof(dst));
            if (n == 0 || dst[n - 1] != '/') {
                n = str_append(dst, "/", sizeof(dst), n);
            }
            str_append(dst, fx_rename_buf, sizeof(dst), n);
            const FsStatus st = hype::vfs::fs_rename(src, dst);
            op_marker("rename", st);
            rename_cancel();
            if (st == FsStatus::Ok) {
                refresh();
                toast("HxFiles", "Renamed");
            } else {
                toast("HxFiles", "Rename failed");
            }
            return st;
        }

        // ---- view open -------------------------------------------------

        void open_entry(hcpp::u32 view) noexcept {
            if (view >= fx_shown) return;
            const FsStat& it = fx_items[fx_order[view]];
            if ((it.flags & hype::vfs::FS_DIR) != 0) {
                nav_into(it.name);
            } else {
                preview_open(view);
            }
        }

        // ---- layout + hit testing ---------------------------------------

        // Recomputes the client-relative layout rects for a client area
        // of `w`×`h`. Does not touch fx_last_client — callers keep the
        // screen-space origin consistent themselves.
        void layout(hcpp::i32 w, hcpp::i32 h) noexcept {
            fx_r_toolbar = {0, 0, w, fx_toolbar_h};
            fx_r_addr = {0, fx_toolbar_h + 2, w, fx_addr_h};
            const hcpp::i32 top = fx_toolbar_h + fx_addr_h + 6;
            const hcpp::i32 bottom = h - fx_status_h;
            const hcpp::i32 body_h =
                bottom - top > 0 ? bottom - top : 0;
            fx_r_side = {0, top, fx_side_w, body_h};
            const hcpp::i32 preview_h =
                fx_preview_on ? fx_preview_h : 0;
            fx_r_main = {fx_side_w + 4, top,
                         w - fx_side_w - 4,
                         body_h - preview_h > 0 ? body_h - preview_h
                                                : 0};
            fx_r_preview = {fx_side_w + 4, top + fx_r_main.h,
                            w - fx_side_w - 4, preview_h};
            fx_r_status = {0, bottom, w, fx_status_h};
            hcpp::i32 bx = 6;
            const hcpp::i32 bw[BtnCount] = {46, 62, 88, 60};
            for (hcpp::u32 i = 0; i < BtnCount; ++i) {
                fx_r_btn[i] = {bx, 4, bw[i], fx_toolbar_h - 8};
                bx += bw[i] + 6;
            }
            const hcpp::i32 rh =
                fx_icons ? fx_cell_h : fx_row_h;
            fx_main_visible =
                fx_r_main.h > 0
                    ? (fx_icons ? (fx_r_main.h + 2) / rh
                                : (fx_r_main.h - fx_head_h) / rh)
                    : 0;
            if (fx_main_visible < 1) fx_main_visible = 1;
            fx_main_cols = fx_r_main.w > fx_sb_w
                               ? (fx_r_main.w - fx_sb_w) / fx_cell_w
                               : 1;
            if (fx_main_cols < 1) fx_main_cols = 1;
            fx_main_rows =
                fx_icons
                    ? (static_cast<hcpp::i32>(fx_shown) + fx_main_cols -
                       1) /
                          fx_main_cols
                    : static_cast<hcpp::i32>(fx_shown);
            scroll_clamp();
        }

        Rect to_screen(const Rect& client_rel) noexcept {
            return client_rel.translated(fx_last_client.origin());
        }

        // -1 = empty pane, -2 = scrollbar zone, >=0 view-order index.
        hcpp::i32 hit_item(hcpp::i32 x, hcpp::i32 y) noexcept {
            const Point p{x, y};
            if (!fx_r_main.contains(p)) return -1;
            if (x >= fx_r_main.right() - fx_sb_w) return -2;
            if (fx_icons) {
                const hcpp::i32 col =
                    (x - fx_r_main.x - 4) / fx_cell_w;
                const hcpp::i32 row =
                    (y - fx_r_main.y - 4) / fx_cell_h;
                if (col < 0 || col >= fx_main_cols || row < 0) return -1;
                const hcpp::i32 v =
                    (row + fx_scroll) * fx_main_cols + col;
                return v < static_cast<hcpp::i32>(fx_shown) ? v : -1;
            }
            const hcpp::i32 row =
                (y - fx_r_main.y - fx_head_h) / fx_row_h;
            if (row < 0) return -1;
            const hcpp::i32 v = row + fx_scroll;
            return v < static_cast<hcpp::i32>(fx_shown) ? v : -1;
        }

        Rect row_rect(hcpp::u32 view) noexcept {
            if (fx_icons) {
                const hcpp::i32 v = static_cast<hcpp::i32>(view);
                const hcpp::i32 col = v % fx_main_cols;
                const hcpp::i32 row = v / fx_main_cols;
                return {fx_r_main.x + 4 + col * fx_cell_w,
                        fx_r_main.y + 4 +
                            (row - fx_scroll) * fx_cell_h,
                        fx_cell_w - 6, fx_cell_h - 4};
            }
            const hcpp::i32 v = static_cast<hcpp::i32>(view);
            return {fx_r_main.x + 1,
                    fx_r_main.y + fx_head_h +
                        (v - fx_scroll) * fx_row_h,
                    fx_r_main.w - fx_sb_w - 1, fx_row_h};
        }

        // ---- painting ----------------------------------------------------

        void paint_button(Canvas& canvas, const Theme& theme, hcpp::u32 b,
                          const char* label, bool enabled) noexcept {
            const Rect r = to_screen(fx_r_btn[b]);
            const hcpp::u32 fill =
                enabled ? theme.secondary
                        : hype::ui::rui_shade(theme.muted, 30);
            hype::ui::rui_control_bg(canvas, r, fill,
                                     enabled ? theme.border : theme.muted);
            hcpp::u32 len = 0;
            while (label[len] != '\0') ++len;
            const hcpp::i32 tx = r.x + (r.w - static_cast<hcpp::i32>(len) * 8) / 2;
            canvas.draw_text(tx < r.x + 4 ? r.x + 4 : tx, r.y + 16,
                             label,
                             canvas.color(enabled ? theme.fg
                                                  : theme.muted_fg));
        }

        void paint_toolbar(Canvas& canvas, const Theme& theme) noexcept {
            const Rect bar = to_screen(fx_r_toolbar);
            canvas.fill_rect(bar, canvas.color(
                                      hype::ui::rui_shade(theme.card, 6)));
            canvas.draw_line(bar.x, bar.bottom(), bar.right(), bar.bottom(),
                             canvas.color(theme.border));
            paint_button(canvas, theme, BtnUp, "Up", fx_cwd.depth > 0);
            paint_button(canvas, theme, BtnRefresh, "Refresh", true);
            paint_button(canvas, theme, BtnNewDir, "New Folder", true);
            paint_button(canvas, theme, BtnView,
                         fx_icons ? "List" : "Icons", true);
            // Paste lives in the context menu; the toolbar keeps to the
            // issue's required controls.
        }

        void paint_address(Canvas& canvas, const Theme& theme) noexcept {
            const Rect bar = to_screen(fx_r_addr);
            canvas.fill_rect(
                bar, canvas.color(hype::ui::rui_shade(theme.card, 12)));
            canvas.stroke_rect(bar, canvas.color(theme.border));
            // Breadcrumb segments are laid out in client coords so the
            // click handler can hit-test them without a second walk.
            hcpp::i32 cx = 8;
            canvas.draw_text(bar.x + cx, bar.y + 17, "HYPEOS://",
                             canvas.color(theme.muted_fg));
            cx += 9 * 8 + 6;
            fx_crumb_count = 0;
            // Disk segment navigates to the disk root (depth 0).
            char disk_seg[4] = {fx_cwd.disk, ':', '\0', '\0'};
            fx_r_crumb[fx_crumb_count] =
                Rect{cx - 4, fx_r_addr.y, 2 * 8 + 8, fx_addr_h};
            fx_crumb_depth[fx_crumb_count] = 0;
            ++fx_crumb_count;
            canvas.fill_rect_alpha(
                to_screen(fx_r_crumb[fx_crumb_count - 1])
                    .inset(Insets{0, 2, 0, 2}),
                canvas.color(theme.muted), 60);
            canvas.draw_text(bar.x + cx, bar.y + 17, disk_seg,
                             canvas.color(theme.accent));
            cx += 2 * 8 + 8;
            for (hcpp::u32 d = 0; d < fx_cwd.depth && fx_crumb_count < 10;
                 ++d) {
                canvas.draw_text(bar.x + cx, bar.y + 17, "/",
                                 canvas.color(theme.muted_fg));
                cx += 8;
                const char* comp = fx_cwd.components[d];
                hcpp::u32 len = 0;
                while (comp[len] != '\0' && len < fx_name_cap) ++len;
                const hcpp::i32 w = static_cast<hcpp::i32>(len) * 8 + 8;
                fx_r_crumb[fx_crumb_count] =
                    Rect{cx - 4, fx_r_addr.y, w, fx_addr_h};
                fx_crumb_depth[fx_crumb_count] = d + 1;
                ++fx_crumb_count;
                canvas.fill_rect_alpha(
                    to_screen(fx_r_crumb[fx_crumb_count - 1])
                        .inset(Insets{0, 2, 0, 2}),
                    canvas.color(theme.muted), 60);
                canvas.draw_text(bar.x + cx, bar.y + 17,
                                 hcpp::StringView{comp, len},
                                 canvas.color(theme.fg));
                cx += w;
            }
        }

        void paint_sidebar(Canvas& canvas, const Theme& theme) noexcept {
            const Rect box = to_screen(fx_r_side);
            canvas.fill_rect(box, canvas.color(
                                      hype::ui::rui_shade(theme.card, 10)));
            canvas.draw_line(box.right(), box.y, box.right(), box.bottom(),
                             canvas.color(theme.border));
            canvas.draw_text(box.x + 8, box.y + 16, "Places",
                             canvas.color(theme.muted_fg));
            hcpp::i32 y = 24;
            for (hcpp::u32 i = 0; i < fx_side_count; ++i) {
                const Rect row{fx_r_side.x + 2, fx_r_side.y + y,
                               fx_r_side.w - 4, 20};
                fx_r_side_rows[i] = row;
                const Rect drawn = to_screen(row);
                // Highlight the place matching the current dir root.
                const bool active =
                    fx_sides[i].path[9] == fx_cwd.disk &&
                    (fx_sides[i].path[10] == '\0'
                         ? fx_cwd.depth == 0
                         : path_has_prefix(fx_cwd_text, fx_sides[i].path));
                if (active) {
                    canvas.fill_rect_alpha(
                        drawn, canvas.color(theme.accent), fx_sel_alpha);
                }
                canvas.draw_text(
                    drawn.x + 8, drawn.y + 14, fx_sides[i].label,
                    canvas.color(active ? theme.fg : theme.muted_fg));
                y += 22;
            }
        }

        void paint_dir_glyph(Canvas& canvas, const Theme& theme,
                             const Rect& g) noexcept {
            // Folder: tab + body.
            const Rect tab{g.x, g.y + 2, g.w / 2, 6};
            const Rect body{g.x, g.y + 8, g.w, g.h - 8};
            canvas.fill_rect(tab, canvas.color(
                                      hype::ui::rui_lift(theme.accent, 1)));
            canvas.fill_rect(body, canvas.color(theme.accent));
            canvas.stroke_rect(body, canvas.color(
                                         hype::ui::rui_shade(theme.accent, 40)));
        }

        void paint_file_glyph(Canvas& canvas, const Theme& theme,
                              const Rect& g) noexcept {
            const Rect page{g.x + g.w / 2 - 8, g.y + 2, 17, 22};
            canvas.fill_rect(page, canvas.color(theme.card));
            canvas.stroke_rect(page, canvas.color(theme.border));
            const hcpp::u32 ink = canvas.color(theme.muted_fg);
            canvas.fill_rect(Rect{page.x + 3, page.y + 7, 11, 1}, ink);
            canvas.fill_rect(Rect{page.x + 3, page.y + 11, 11, 1}, ink);
            canvas.fill_rect(Rect{page.x + 3, page.y + 15, 7, 1}, ink);
        }

        void format_size(char* out, hcpp::u32 cap,
                         hcpp::u32 bytes) noexcept {
            hcpp::TextWriter w;
            if (bytes >= 1024 * 1024) {
                w.append_dec32(bytes / (1024 * 1024));
                w.append(" MiB");
            } else if (bytes >= 1024) {
                w.append_dec32(bytes / 1024);
                w.append(" KiB");
            } else {
                w.append_dec32(bytes);
                w.append(" B");
            }
            str_copy(out, w.c_str(), cap);
        }

        void paint_list_row(Canvas& canvas, const Theme& theme,
                            hcpp::u32 view) noexcept {
            const Rect row = to_screen(row_rect(view));
            if (row.y + row.h <= fx_last_client.y + fx_r_main.y ||
                row.y >= fx_last_client.y + fx_r_main.bottom()) {
                return;
            }
            const FsStat& it = fx_items[fx_order[view]];
            const bool sel = fx_sel[view] != 0;
            const bool focus = static_cast<hcpp::i32>(view) == fx_focus;
            if (sel) {
                canvas.fill_rect_alpha(row, canvas.color(theme.accent),
                                       fx_sel_alpha);
            }
            if (focus) {
                canvas.stroke_rect(row, canvas.color(theme.border));
            }
            const Rect glyph{row.x + 4, row.y + 2, 16, 14};
            if ((it.flags & hype::vfs::FS_DIR) != 0) {
                paint_dir_glyph(canvas, theme, glyph);
            } else {
                paint_file_glyph(canvas, theme, glyph);
            }
            const hcpp::i32 name_x = row.x + 26;
            const hcpp::i32 size_x = row.right() - 170;
            const hcpp::i32 kind_x = row.right() - 72;
            if (static_cast<hcpp::i32>(view) == fx_rename_view) {
                // Inline rename edit box over the name cell.
                const Rect edit{name_x - 2, row.y + 1,
                                size_x - name_x - 4, row.h - 2};
                canvas.fill_rect(edit, canvas.color(theme.card));
                canvas.stroke_rect(edit, canvas.color(theme.accent));
                canvas.draw_text(edit.x + 4, edit.y + 15,
                                 hcpp::StringView{fx_rename_buf,
                                                  fx_rename_len},
                                 canvas.color(theme.fg));
                const hcpp::i32 cx =
                    edit.x + 4 + static_cast<hcpp::i32>(fx_rename_len) * 8;
                canvas.fill_rect(Rect{cx, edit.y + 4, 1, edit.h - 8},
                                 canvas.color(theme.accent));
            } else {
                canvas.draw_text(name_x, row.y + 15, it.name,
                                 canvas.color(theme.fg));
            }
            if (size_x > name_x + 40) {
                char size_buf[24];
                if ((it.flags & hype::vfs::FS_DIR) != 0) {
                    str_copy(size_buf, "--", sizeof(size_buf));
                } else {
                    format_size(size_buf, sizeof(size_buf), it.size);
                }
                canvas.draw_text(size_x, row.y + 15, size_buf,
                                 canvas.color(theme.muted_fg));
                const char* kind =
                    (it.flags & hype::vfs::FS_DIR) != 0
                        ? "dir"
                        : ((it.flags & hype::vfs::FS_SYS) != 0 ? "sys"
                                                             : "file");
                canvas.draw_text(kind_x, row.y + 15, kind,
                                 canvas.color(theme.muted_fg));
            }
        }

        void paint_icon_cell(Canvas& canvas, const Theme& theme,
                             hcpp::u32 view) noexcept {
            const Rect cell = to_screen(row_rect(view));
            if (cell.y + cell.h <= fx_last_client.y + fx_r_main.y ||
                cell.y >= fx_last_client.y + fx_r_main.bottom()) {
                return;
            }
            const FsStat& it = fx_items[fx_order[view]];
            if (fx_sel[view] != 0) {
                const Rect selbox = cell.inset(Insets::uniform(2));
                canvas.fill_rect_alpha(selbox, canvas.color(theme.accent),
                                       fx_sel_alpha);
                canvas.stroke_rounded_rect(
                    selbox, static_cast<hcpp::i32>(theme.radius_sm),
                    canvas.color(theme.accent));
            }
            const Rect glyph{cell.x + (cell.w - 28) / 2, cell.y + 6, 28,
                             28};
            if ((it.flags & hype::vfs::FS_DIR) != 0) {
                paint_dir_glyph(canvas, theme, glyph);
            } else {
                paint_file_glyph(canvas, theme, glyph);
            }
            char label[12];
            hcpp::u32 len = 0;
            while (len < 11 && it.name[len] != '\0') {
                label[len] = it.name[len];
                ++len;
            }
            label[len] = '\0';
            const hcpp::i32 tw = static_cast<hcpp::i32>(len) * 8;
            hcpp::i32 tx = cell.x + (cell.w - tw) / 2;
            if (tx < cell.x) tx = cell.x;
            canvas.draw_text(tx, cell.y + cell.h - 10,
                             hcpp::StringView{label, len},
                             canvas.color(theme.fg));
        }

        void paint_scrollbar(Canvas& canvas, const Theme& theme) noexcept {
            if (fx_main_rows <= fx_main_visible) return;
            const Rect track{fx_last_client.x + fx_r_main.right() - fx_sb_w,
                             fx_last_client.y + fx_r_main.y, fx_sb_w,
                             fx_r_main.h};
            canvas.fill_rect(track, canvas.color(
                                        hype::ui::rui_shade(theme.card, 16)));
            const hcpp::i32 thumb_h =
                track.h * fx_main_visible / fx_main_rows;
            const hcpp::i32 span = fx_main_rows - fx_main_visible;
            const hcpp::i32 thumb_y =
                track.y +
                (span > 0 ? (track.h - thumb_h) * fx_scroll / span : 0);
            canvas.fill_rect(Rect{track.x + 2, thumb_y, track.w - 4,
                                  thumb_h > 12 ? thumb_h : 12},
                             canvas.color(theme.muted_fg));
        }

        void paint_main(Canvas& canvas, const Theme& theme) noexcept {
            const Rect box = to_screen(fx_r_main);
            canvas.fill_rect(box, canvas.color(theme.card));
            canvas.stroke_rect(box, canvas.color(theme.border));
            if (!fx_icons) {
                // Column header.
                canvas.draw_text(box.x + 30, box.y + 15, "Name",
                                 canvas.color(theme.muted_fg));
                canvas.draw_text(box.right() - 170, box.y + 15, "Size",
                                 canvas.color(theme.muted_fg));
                canvas.draw_text(box.right() - 72, box.y + 15, "Kind",
                                 canvas.color(theme.muted_fg));
                canvas.draw_line(box.x, box.y + fx_head_h, box.right(),
                                 box.y + fx_head_h,
                                 canvas.color(theme.border));
            }
            if (fx_shown == 0) {
                canvas.draw_text(box.x + 16, box.y + fx_head_h + 24,
                                 "empty folder",
                                 canvas.color(theme.muted_fg));
            }
            const hcpp::i32 first =
                fx_icons ? fx_scroll * fx_main_cols : fx_scroll;
            const hcpp::i32 last_row = fx_scroll + fx_main_visible;
            const hcpp::i32 last =
                fx_icons ? last_row * fx_main_cols : last_row;
            for (hcpp::i32 v = first;
                 v < last && v < static_cast<hcpp::i32>(fx_shown); ++v) {
                if (fx_icons) {
                    paint_icon_cell(canvas, theme,
                                    static_cast<hcpp::u32>(v));
                } else {
                    paint_list_row(canvas, theme,
                                   static_cast<hcpp::u32>(v));
                }
            }
            paint_scrollbar(canvas, theme);
        }

        void paint_preview(Canvas& canvas, const Theme& theme) noexcept {
            if (!fx_preview_on) return;
            const Rect box = to_screen(fx_r_preview);
            canvas.fill_rect(box, canvas.color(
                                      hype::ui::rui_shade(theme.card, 14)));
            canvas.draw_line(box.x, box.y, box.right(), box.y,
                             canvas.color(theme.border));
            hcpp::TextWriter head;
            head.append("Preview  ");
            head.append(fx_preview_name);
            head.append("  ");
            head.append_dec32(fx_preview_size);
            head.append(" B");
            canvas.draw_text(box.x + 8, box.y + 16, head.view(),
                             canvas.color(theme.muted_fg));
            if (fx_preview_binary) {
                canvas.draw_text(box.x + 8, box.y + 38,
                                 "binary file - no preview",
                                 canvas.color(theme.muted_fg));
                return;
            }
            hcpp::i32 y = box.y + 36;
            hcpp::u32 line_start = 0;
            for (hcpp::u32 i = 0; i <= fx_preview_len; ++i) {
                const bool eol = i == fx_preview_len ||
                                 fx_preview_buf[i] == '\n';
                if (!eol) continue;
                hcpp::u32 len = i - line_start;
                if (len > 0 && fx_preview_buf[i - 1] == '\r') --len;
                // Sanitize control bytes to keep glyphs printable.
                char line[96];
                hcpp::u32 shown = len < 92 ? len : 92;
                for (hcpp::u32 k = 0; k < shown; ++k) {
                    const hcpp::u8 ch = fx_preview_buf[line_start + k];
                    line[k] = (ch == '\t')
                                  ? ' '
                                  : (ch < 0x20 || ch > 0x7E ? '.' : static_cast<char>(ch));
                }
                canvas.draw_text(box.x + 8, y,
                                 hcpp::StringView{line, shown},
                                 canvas.color(theme.fg));
                y += 15;
                if (y > box.bottom() - 14) break;
                line_start = i + 1;
            }
        }

        void paint_status(Canvas& canvas, const Theme& theme) noexcept {
            const Rect bar = to_screen(fx_r_status);
            canvas.fill_rect(bar, canvas.color(
                                      hype::ui::rui_shade(theme.card, 6)));
            canvas.draw_line(bar.x, bar.y, bar.right(), bar.y,
                             canvas.color(theme.border));
            hcpp::TextWriter left;
            left.append_dec32(fx_total);
            left.append(" item");
            if (fx_total != 1) left.append('s');
            if (fx_total > fx_shown) {
                left.append(" (showing ");
                left.append_dec32(fx_shown);
                left.append(')');
            }
            if (fx_sel_count > 0) {
                left.append("   ");
                left.append_dec32(fx_sel_count);
                left.append(" selected  ");
                left.append_dec32(fx_sel_bytes);
                left.append(" B");
            }
            if (fx_clip_valid) {
                left.append("   clip:");
                left.append(fx_clip_leaf);
                if (fx_clip_dir) left.append('/');
            }
            canvas.draw_text(bar.x + 8, bar.y + 15, left.view(),
                             canvas.color(theme.muted_fg));
            hcpp::TextWriter right;
            right.append(fx_cwd.disk);
            right.append(": ");
            right.append_dec32(hype::vfs::fs_disk_free(fx_cwd.disk) / 1024);
            right.append(" KiB free");
            const hcpp::i32 rw =
                static_cast<hcpp::i32>(right.size()) * 8;
            canvas.draw_text(bar.right() - rw - 8, bar.y + 15, right.view(),
                             canvas.color(theme.muted_fg));
        }

        void paint_all(hcpp::graphics::Framebuffer& fb, const Rect& client,
                       const Rect& clip) noexcept {
            // `client` arrives in screen coords; layout rects are
            // client-relative so LBUTTONDOWN lparams hit-test directly.
            // fx_last_client keeps the screen-space origin for
            // to_screen() and for ctx-menu anchoring.
            layout(client.w, client.h);
            fx_last_client = client;
            Canvas canvas(fb);
            canvas.set_clip(clip.empty() ? client : clip);
            const Theme& theme = hype::ui::theme_active();
            hype::ui::rui_panel(canvas, client, hype::ui::rui_elev_window);
            paint_toolbar(canvas, theme);
            paint_address(canvas, theme);
            paint_sidebar(canvas, theme);
            paint_main(canvas, theme);
            paint_preview(canvas, theme);
            paint_status(canvas, theme);
        }

        // ---- input ------------------------------------------------------

        void click(hcpp::i32 x, hcpp::i32 y) noexcept {
            const Point p{x, y};
            layout(fx_last_client.w, fx_last_client.h);
            if (fx_r_toolbar.contains(p)) {
                if (fx_r_btn[BtnUp].contains(p)) {
                    nav_up();
                } else if (fx_r_btn[BtnRefresh].contains(p)) {
                    refresh();
                    serial::write("hxfiles nav=");
                    serial::write_line(fx_cwd_text);
                } else if (fx_r_btn[BtnNewDir].contains(p)) {
                    op_newdir();
                } else if (fx_r_btn[BtnView].contains(p)) {
                    fx_icons = !fx_icons;
                    fx_scroll = 0;
                    scroll_clamp();
                    serial::write("hxfiles view=");
                    serial::write_line(fx_icons ? "icons" : "list");
                }
                invalidate();
                return;
            }
            if (fx_r_addr.contains(p)) {
                for (hcpp::u32 i = 0; i < fx_crumb_count; ++i) {
                    if (fx_r_crumb[i].contains(p) &&
                        fx_crumb_depth[i] < fx_cwd.depth) {
                        fx_cwd.depth = fx_crumb_depth[i];
                        nav_to(fx_cwd);
                        break;
                    }
                }
                invalidate();
                return;
            }
            if (fx_r_side.contains(p)) {
                for (hcpp::u32 i = 0; i < fx_side_count; ++i) {
                    if (fx_r_side_rows[i].contains(p)) {
                        nav_text(fx_sides[i].path);
                        break;
                    }
                }
                invalidate();
                return;
            }
            if (fx_r_preview.contains(p)) {
                // Click the preview strip to close it.
                preview_close();
                invalidate();
                return;
            }
            const hcpp::i32 hit = hit_item(x, y);
            if (hit == -2) {
                // Scrollbar track click: page toward the pointer.
                const hcpp::i32 mid = fx_r_main.y +
                                      fx_r_main.h / 2;
                fx_scroll += (y < mid ? -1 : 1) * fx_main_visible;
                scroll_clamp();
                invalidate();
                return;
            }
            if (hit < 0) {
                sel_clear();
                fx_focus = -1;
                invalidate();
                return;
            }
            const hcpp::u32 v = static_cast<hcpp::u32>(hit);
            const bool ctrl = hype::input::scancode_key_pressed(key_ctrl);
            if (ctrl) {
                fx_sel[v] ^= 1;
            } else {
                sel_clear();
                fx_sel[v] = 1;
                serial::write("hxfiles sel=");
                serial::write_line(fx_items[fx_order[v]].name);
            }
            fx_focus = hit;
            sel_count_recalc();
            const hcpp::u64 now = hype::time::uptime_ms();
            const bool dbl = !ctrl && fx_last_hit == hit &&
                             now - fx_last_hit_ms <= fx_dblclick_ms;
            fx_last_hit = hit;
            fx_last_hit_ms = now;
            if (dbl) {
                fx_last_hit = -1;
                open_entry(v);
            }
            invalidate();
        }

        void ctx_pick(hcpp::u32 id, void* user) noexcept {
            (void)user;
            switch (id) {
                case fx_ctx_open: {
                    const hcpp::i32 v = first_selected();
                    if (v >= 0) open_entry(static_cast<hcpp::u32>(v));
                    break;
                }
                case fx_ctx_copy:   op_copy(); break;
                case fx_ctx_paste:  op_paste(); break;
                case fx_ctx_newdir: op_newdir(); break;
                case fx_ctx_delete: op_delete(); break;
                case fx_ctx_rename:
                    rename_begin(first_selected());
                    break;
                default: break;
            }
            invalidate();
        }

        void rclick(hcpp::i32 x, hcpp::i32 y) noexcept {
            layout(fx_last_client.w, fx_last_client.h);
            const hcpp::i32 hit = hit_item(x, y);
            if (hit >= 0 && fx_sel[hit] == 0) {
                sel_clear();
                fx_sel[hit] = 1;
                fx_focus = hit;
                sel_count_recalc();
            }
            const bool have_sel = fx_sel_count > 0;
            const hype::win::WmCtxItem items[] = {
                {"Open", fx_ctx_open,
                 static_cast<hcpp::u8>(have_sel ? 0 : hype::win::wm_ctx_disabled)},
                {"Copy", fx_ctx_copy,
                 static_cast<hcpp::u8>(have_sel ? 0 : hype::win::wm_ctx_disabled)},
                {"Paste", fx_ctx_paste,
                 static_cast<hcpp::u8>(fx_clip_valid ? 0
                                                     : hype::win::wm_ctx_disabled)},
                {"New Folder", fx_ctx_newdir, 0},
                {nullptr, 0, hype::win::wm_ctx_separator},
                {"Delete", fx_ctx_delete,
                 static_cast<hcpp::u8>(have_sel ? 0 : hype::win::wm_ctx_disabled)},
                {"Rename", fx_ctx_rename,
                 static_cast<hcpp::u8>(
                     fx_sel_count == 1 ? 0 : hype::win::wm_ctx_disabled)},
            };
            // wm_ctx_open wants screen coords; the message carries
            // client-space ones.
            hype::win::wm_ctx_open(fx_last_client.x + x,
                                   fx_last_client.y + y, items, 7,
                                   &ctx_pick, nullptr);
            invalidate();
        }

        void key(hcpp::u32 scancode) noexcept {
            if (fx_rename_view >= 0) {
                if (scancode == key_enter) {
                    rename_commit();
                } else if (scancode == key_esc) {
                    rename_cancel();
                } else if (scancode == key_backspace &&
                           fx_rename_len > 0) {
                    fx_rename_buf[--fx_rename_len] = '\0';
                }
                invalidate();
                return;
            }
            const bool ctrl = hype::input::scancode_key_pressed(key_ctrl);
            switch (scancode) {
                case key_enter: {
                    const hcpp::i32 v = first_selected();
                    if (v >= 0) open_entry(static_cast<hcpp::u32>(v));
                    break;
                }
                case key_backspace: nav_up(); break;
                case key_del:       op_delete(); break;
                case key_f5:        refresh(); break;
                case key_f2:
                    if (fx_sel_count == 1) rename_begin(first_selected());
                    break;
                case key_esc:
                    if (fx_preview_on) {
                        preview_close();
                    } else {
                        sel_clear();
                        fx_focus = -1;
                    }
                    break;
                case key_up:
                case key_down: {
                    if (fx_shown == 0) break;
                    hcpp::i32 v = fx_focus;
                    v += scancode == key_up ? -1 : 1;
                    if (v < 0) v = 0;
                    if (v >= static_cast<hcpp::i32>(fx_shown)) {
                        v = static_cast<hcpp::i32>(fx_shown) - 1;
                    }
                    sel_clear();
                    fx_sel[v] = 1;
                    fx_focus = v;
                    sel_count_recalc();
                    ensure_visible(v);
                    break;
                }
                case key_pgup:
                case key_pgdn:
                    fx_scroll += (scancode == key_pgup ? -1 : 1) *
                                 fx_main_visible;
                    scroll_clamp();
                    break;
                case key_home: fx_scroll = 0; break;
                case key_end:
                    fx_scroll = fx_main_rows - fx_main_visible;
                    scroll_clamp();
                    break;
                case key_c:
                    if (ctrl) op_copy();
                    break;
                case key_v:
                    if (ctrl) op_paste();
                    break;
                case key_a:
                    if (ctrl) {
                        for (hcpp::u32 i = 0; i < fx_shown; ++i) {
                            fx_sel[i] = 1;
                        }
                        sel_count_recalc();
                    }
                    break;
                default: break;
            }
            invalidate();
        }

        void chard(hcpp::u32 c) noexcept {
            if (fx_rename_view < 0) return;
            const char ch = static_cast<char>(c & 0xFF);
            if (name_char_ok(ch) && fx_rename_len + 1 < fx_name_cap) {
                fx_rename_buf[fx_rename_len++] = ch;
                fx_rename_buf[fx_rename_len] = '\0';
                invalidate();
            }
        }

        // ---- XAU plumbing ------------------------------------------------

        hcpp::i32 hxfiles_wndproc(hype::win::XAU xau_win, hcpp::u32 msg,
                                  hcpp::u64 wparam,
                                  hcpp::u64 lparam) noexcept {
            switch (msg) {
                case hype::win::XAU_MSG_CREATE:
                    fx_win = xau_win;
                    places_build();
                    // Start at the user disk; fall back to S: when the
                    // user mount is missing.
                    FsStat root_probe;
                    if (hype::vfs::hypepath_parse("HYPEOS://U", fx_cwd) !=
                            hype::vfs::HypePathStatus::Ok ||
                        hype::vfs::fs_stat("HYPEOS://U", root_probe) !=
                            FsStatus::Ok) {
                        hype::vfs::hypepath_parse("HYPEOS://S", fx_cwd);
                    }
                    cwd_format();
                    refresh();
                    // A pending navigation (Start-menu file result)
                    // overrides the default landing dir.
                    if (fx_pending_nav[0] != '\0') {
                        char pending[fx_path_cap];
                        str_copy(pending, fx_pending_nav,
                                 static_cast<hcpp::u32>(sizeof(pending)));
                        fx_pending_nav[0] = '\0';
                        nav_text(pending);
                    }
                    hype::win::xau_set_title(xau_win, "Files");
                    serial::write("hxfiles nav=");
                    serial::write_line(fx_cwd_text);
                    serial::write_line("hxfiles open=ok");
                    {
                        // Emit the client rect so the QEMU e2e driver can
                        // compute click coordinates without guessing the
                        // wm cascade position.
                        hype::ui::Rect cr;
                        if (hype::win::xau_client_rect(xau_win, cr)) {
                            serial::write("hxfiles rect=");
                            serial::write_dec32(
                                static_cast<hcpp::u32>(cr.x));
                            serial::write_char(',');
                            serial::write_dec32(
                                static_cast<hcpp::u32>(cr.y));
                            serial::write_char(',');
                            serial::write_dec32(
                                static_cast<hcpp::u32>(cr.w));
                            serial::write_char(',');
                            serial::write_dec32(
                                static_cast<hcpp::u32>(cr.h));
                            serial::write_line("");
                        }
                    }
                    return 0;
                case hype::win::XAU_MSG_DESTROY:
                    if (xau_win == fx_win) {
                        fx_win = hype::win::XAU_INVALID;
                        fx_clip_valid = false;
                        fx_preview_on = false;
                        fx_rename_view = -1;
                    }
                    return 0;
                case hype::win::XAU_MSG_PAINT: {
                    const hype::win::XauDC* dc =
                        reinterpret_cast<const hype::win::XauDC*>(lparam);
                    if (dc != nullptr && dc->fb != nullptr &&
                        dc->fb->valid() && !dc->client.empty()) {
                        paint_all(*dc->fb, dc->client, dc->clip);
                    }
                    return 0;
                }
                case hype::win::XAU_MSG_SIZE:
                case hype::win::XAU_MSG_MAXIMIZE:
                    scroll_clamp();
                    invalidate();
                    return 0;
                case hype::win::XAU_MSG_LBUTTONDOWN: {
                    const hcpp::i32 x =
                        static_cast<hcpp::i16>(lparam & 0xFFFF);
                    const hcpp::i32 y =
                        static_cast<hcpp::i16>((lparam >> 16) & 0xFFFF);
                    click(x, y);
                    return 0;
                }
                case hype::win::XAU_MSG_RBUTTONDOWN: {
                    const hcpp::i32 x =
                        static_cast<hcpp::i16>(lparam & 0xFFFF);
                    const hcpp::i32 y =
                        static_cast<hcpp::i16>((lparam >> 16) & 0xFFFF);
                    rclick(x, y);
                    return 0;
                }
                case hype::win::XAU_MSG_KEYDOWN:
                    key(static_cast<hcpp::u32>(wparam));
                    return 0;
                case hype::win::XAU_MSG_CHAR:
                    chard(static_cast<hcpp::u32>(wparam));
                    return 0;
                default:
                    return hype::win::xau_defproc(xau_win, msg, wparam,
                                                  lparam);
            }
        }

        // Wheel events ride the shared input bus (XAU has no wheel
        // message); scroll when the focused window is this app. Positive
        // delta = wheel up = toward the top of the listing.
        void fx_input_event(void*, const hype::input::InputEvent& e) noexcept {
            if (e.type != hype::input::InputEventType::Relative ||
                e.code != hype::input::rel_wheel_code) {
                return;
            }
            if (fx_win == hype::win::XAU_INVALID ||
                hype::session::session_locked()) {
                return;
            }
            const hype::win::Win* w = hype::win::wm_focused();
            if (w == nullptr || !str_equal(w->app_name, "HxFiles")) {
                return;
            }
            fx_scroll -= static_cast<hcpp::i32>(e.value) * 3;
            scroll_clamp();
            serial::write("hxfiles scroll=");
            serial::write_dec32(static_cast<hcpp::u32>(fx_scroll));
            serial::write("\r\n");
            invalidate();
        }

        void hxfiles_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            // Non-XAU fallback (adoption failed): render the current
            // state once through the same painter.
            paint_all(*ctx.fb, ctx.region, ctx.paint_clip());
        }

        void hxfiles_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "HxFiles", "HYPEOS://S/apps/HxFiles.hxersys", hxfiles_entry,
            hxfiles_close, nullptr};

    }  // namespace

    const HxerHeader* hxfiles_app() noexcept { return &desc; }

    // Navigates HxFiles to `path` (HYPEOS:// text form). Live-navigates
    // the open window; when no window is bound yet the path is stashed
    // and consumed by the next XAU_MSG_CREATE, so callers can fire
    // hxfiles_nav + desk_open_app in either order (issue #129).
    void hxfiles_nav(const char* path) noexcept {
        if (path == nullptr || path[0] == '\0') {
            return;
        }
        if (fx_win != hype::win::XAU_INVALID) {
            nav_text(path);
            invalidate();
            return;
        }
        str_copy(fx_pending_nav, path,
                 static_cast<hcpp::u32>(sizeof(fx_pending_nav)));
    }

    // Registers the "HxFiles" XAU class plus the wheel-scroll input
    // subscription (subscribe_keys has no unsubscribe, so hook once).
    void hxfiles_register_xau() noexcept {
        hype::win::xau_register_class("HxFiles", &hxfiles_wndproc, 0, 0);
        if (!fx_input_hooked) {
            fx_input_hooked =
                hype::input::subscribe_keys(&fx_input_event, nullptr);
        }
    }

}  // namespace hype::hxrt
