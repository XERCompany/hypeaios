#include "kernel/hxbvm/hxbvm.h"

#include "hcpp/memory.hpp"
#include "hcpp/string_view.hpp"
#include "kernel/hxrt/appctx.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/deskbar.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/winman.h"

namespace hype::hxbvm {

    namespace {

        // ---- bounded decoders (little-endian) -----------------------------------
        bool rd8(const VmProgram& p, hcpp::u32 at, hcpp::u8& out) noexcept {
            if (at >= p.code_size) return false;
            out = p.code[at];
            return true;
        }

        bool rd16(const VmProgram& p, hcpp::u32 at, hcpp::u16& out) noexcept {
            if (at + 2 > p.code_size) return false;
            out = static_cast<hcpp::u16>(p.code[at]) |
                  (static_cast<hcpp::u16>(p.code[at + 1]) << 8);
            return true;
        }

        bool rd64(const VmProgram& p, hcpp::u32 at, hcpp::u64& out) noexcept {
            if (at + 8 > p.code_size) return false;
            out = 0;
            for (hcpp::u32 i = 0; i < 8; ++i) {
                out |= static_cast<hcpp::u64>(p.code[at + i])
                       << (i * 8);
            }
            return true;
        }

        bool push(VmProgram& p, hcpp::u64 v) noexcept {
            if (p.sp >= vm_stack_cells) return false;
            p.stack[p.sp++] = v;
            return true;
        }

        bool pop(VmProgram& p, hcpp::u64& out) noexcept {
            if (p.sp == 0) return false;
            out = p.stack[--p.sp];
            return true;
        }

        // Walks the {u16 len, bytes} pool to entry `index`.
        bool str_at(const VmProgram& p, hcpp::u16 index, const char*& out,
                    hcpp::u32& len) noexcept {
            if (index >= p.str_count || p.str_pool == nullptr) return false;
            hcpp::u32 off = 0;
            for (hcpp::u16 i = 0; i <= index; ++i) {
                if (off + 2 > p.str_pool_size) return false;
                const hcpp::u32 n =
                    static_cast<hcpp::u32>(p.str_pool[off]) |
                    (static_cast<hcpp::u32>(p.str_pool[off + 1]) << 8);
                off += 2;
                if (off + n > p.str_pool_size) return false;
                if (i == index) {
                    out = reinterpret_cast<const char*>(p.str_pool + off);
                    len = n;
                    return true;
                }
                off += n;
            }
            return false;
        }

        bool heap_at(const VmProgram& p, hcpp::u64 handle, hcpp::u8*& out,
                     hcpp::u32* len) noexcept {
            const hcpp::u32 slot = static_cast<hcpp::u32>(handle & 0xFF);
            if (p.heap == nullptr || slot >= vm_heap_slots ||
                !p.heap_slots[slot].live) {
                return false;
            }
            const VmHeapSlot& s = p.heap_slots[slot];
            if (s.off + s.len > vm_heap_bytes) return false;
            out = p.heap + s.off;
            if (len != nullptr) *len = s.len;
            return true;
        }

        hcpp::u32 fmt_len(const char* text) noexcept {
            hcpp::u32 n = 0;
            while (n < vm_fmt_size && text[n] != '\0') ++n;
            return n;
        }

        // Copies a resolved string into a NUL-terminated bounded buffer.
        bool str_copy(const VmProgram& p, hcpp::u64 cell, char* out,
                      hcpp::u32 cap) noexcept {
            const char* src = nullptr;
            hcpp::u32 len = 0;
            if (!vm_str(p, cell, src, len) || len >= cap) return false;
            hcpp::copy(out, src, len);
            out[len] = '\0';
            return true;
        }

        hcpp::u64 arg_or(const hcpp::u64* a, hcpp::u8 argc, hcpp::u8 i,
                         hcpp::u64 fallback) noexcept {
            return i < argc ? a[i] : fallback;
        }

        // ---- natives ------------------------------------------------------------
        // Every native is bounds-checked; nothing here trusts a handle.

        hcpp::u64 n_serial_str(VmProgram& p, const hcpp::u64* a,
                               hcpp::u8 argc) noexcept {
            const char* s = nullptr;
            hcpp::u32 len = 0;
            if (!vm_str(p, arg_or(a, argc, 0, 0), s, len)) return vm_cell_fail;
            serial::write(hcpp::StringView{s, len});
            return 0;
        }

        hcpp::u64 n_serial_dec(VmProgram&, const hcpp::u64* a,
                               hcpp::u8 argc) noexcept {
            serial::write_dec32(
                static_cast<hcpp::u32>(arg_or(a, argc, 0, 0)));
            return 0;
        }

        hcpp::u64 n_ctx_rect(VmProgram& p, const hcpp::u64* a,
                             hcpp::u8 argc) noexcept {
            if (p.ctx == nullptr) return 0;
            switch (arg_or(a, argc, 0, 0)) {
                case 0: return static_cast<hcpp::u64>(p.ctx->region.x) & 0xFFFFFFFFull;
                case 1: return static_cast<hcpp::u64>(p.ctx->region.y) & 0xFFFFFFFFull;
                case 2: return static_cast<hcpp::u64>(p.ctx->region.w) & 0xFFFFFFFFull;
                case 3: return static_cast<hcpp::u64>(p.ctx->region.h) & 0xFFFFFFFFull;
                default: return 0;
            }
        }

        bool canvas_ok(const VmProgram& p) noexcept {
            return p.ctx != nullptr && p.ctx->fb != nullptr &&
                   p.ctx->fb->valid();
        }

        hcpp::u64 n_fill_rect(VmProgram& p, const hcpp::u64* a,
                              hcpp::u8 argc) noexcept {
            if (!canvas_ok(p)) return 0;
            const hype::ui::Rect r{
                static_cast<hcpp::i32>(arg_or(a, argc, 0, 0)),
                static_cast<hcpp::i32>(arg_or(a, argc, 1, 0)),
                static_cast<hcpp::i32>(arg_or(a, argc, 2, 0)),
                static_cast<hcpp::i32>(arg_or(a, argc, 3, 0))};
            hype::ui::Canvas canvas(*p.ctx->fb);
            canvas.set_clip(p.ctx->paint_clip());
            canvas.fill_rect(r, canvas.color(
                static_cast<hcpp::u32>(arg_or(a, argc, 4, 0)) & 0xFFFFFFu));
            return 1;
        }

        hcpp::u64 n_draw_text(VmProgram& p, const hcpp::u64* a,
                              hcpp::u8 argc) noexcept {
            if (!canvas_ok(p)) return 0;
            const char* s = nullptr;
            hcpp::u32 len = 0;
            if (!vm_str(p, arg_or(a, argc, 2, 0), s, len)) return 0;
            hype::ui::Canvas canvas(*p.ctx->fb);
            canvas.set_clip(p.ctx->paint_clip());
            canvas.draw_text(static_cast<hcpp::i32>(arg_or(a, argc, 0, 0)),
                             static_cast<hcpp::i32>(arg_or(a, argc, 1, 0)),
                             hcpp::StringView{s, len},
                             canvas.color(static_cast<hcpp::u32>(
                                 arg_or(a, argc, 3, 0)) & 0xFFFFFFu));
            return 1;
        }

        hcpp::u64 n_app_register(VmProgram& p, const hcpp::u64* a,
                                 hcpp::u8 argc) noexcept {
            if (p.register_app == nullptr) return 0;
            char name[24]{};
            // Empty/absent name keeps the module's HXBC header name.
            if (argc > 0 && !str_copy(p, a[0], name, sizeof(name))) {
                return 0;
            }
            return p.register_app(p.host, name[0] ? name : nullptr) ? 1 : 0;
        }

        hcpp::u64 n_fs_read(VmProgram& p, const hcpp::u64* a,
                            hcpp::u8 argc) noexcept {
            char path[128]{};
            if (!str_copy(p, arg_or(a, argc, 0, 0), path, sizeof(path))) {
                return vm_cell_fail;
            }
            hype::vfs::FsStat st;
            if (hype::vfs::fs_stat(path, st) != hype::vfs::FsStatus::Ok ||
                (st.flags & hype::vfs::FS_DIR) != 0) {
                return vm_cell_fail;
            }
            const hcpp::i32 slot = vm_heap_alloc(p, st.size);
            if (slot < 0) return vm_cell_fail;
            const hcpp::u64 handle = vm_tag_heap |
                                     static_cast<hcpp::u32>(slot);
            hcpp::u8* dst = nullptr;
            hcpp::u32 cap = 0;
            if (!heap_at(p, handle, dst, &cap)) return vm_cell_fail;
            hcpp::u32 got = 0;
            if (st.size > 0 &&
                hype::vfs::fs_read(path, dst, cap, &got) !=
                    hype::vfs::FsStatus::Ok) {
                return vm_cell_fail;
            }
            return handle;
        }

        hcpp::u64 n_fs_write(VmProgram& p, const hcpp::u64* a,
                             hcpp::u8 argc) noexcept {
            char path[128]{};
            if (!str_copy(p, arg_or(a, argc, 0, 0), path, sizeof(path))) {
                return vm_cell_fail;
            }
            hcpp::u8* data = nullptr;
            hcpp::u32 len = 0;
            if (!heap_at(p, arg_or(a, argc, 1, 0), data, &len)) {
                return vm_cell_fail;
            }
            const hype::vfs::FsStatus st =
                hype::vfs::fs_write(path, data, len);
            return st == hype::vfs::FsStatus::Ok
                       ? 0
                       : static_cast<hcpp::u64>(st) + 1;
        }

        hcpp::u64 n_heap_alloc(VmProgram& p, const hcpp::u64* a,
                               hcpp::u8 argc) noexcept {
            const hcpp::i32 slot =
                vm_heap_alloc(p, static_cast<hcpp::u32>(arg_or(a, argc, 0, 0)));
            return slot < 0 ? vm_cell_fail
                            : vm_tag_heap | static_cast<hcpp::u32>(slot);
        }

        hcpp::u64 n_heap_len(VmProgram& p, const hcpp::u64* a,
                             hcpp::u8 argc) noexcept {
            hcpp::u8* ptr = nullptr;
            hcpp::u32 len = 0;
            return heap_at(p, arg_or(a, argc, 0, 0), ptr, &len)
                       ? len
                       : vm_cell_fail;
        }

        hcpp::u64 n_heap_r8(VmProgram& p, const hcpp::u64* a,
                            hcpp::u8 argc) noexcept {
            hcpp::u8* ptr = nullptr;
            hcpp::u32 len = 0;
            const hcpp::u64 off = arg_or(a, argc, 1, 0);
            if (!heap_at(p, arg_or(a, argc, 0, 0), ptr, &len) ||
                off >= len) {
                return vm_cell_fail;
            }
            return ptr[off];
        }

        hcpp::u64 n_heap_w8(VmProgram& p, const hcpp::u64* a,
                            hcpp::u8 argc) noexcept {
            hcpp::u8* ptr = nullptr;
            hcpp::u32 len = 0;
            const hcpp::u64 off = arg_or(a, argc, 1, 0);
            if (!heap_at(p, arg_or(a, argc, 0, 0), ptr, &len) ||
                off >= len) {
                return vm_cell_fail;
            }
            ptr[off] = static_cast<hcpp::u8>(arg_or(a, argc, 2, 0));
            return 0;
        }

        hcpp::u64 n_notify(VmProgram& p, const hcpp::u64* a,
                           hcpp::u8 argc) noexcept {
            char title[48]{};
            char msg[96]{};
            if (!str_copy(p, arg_or(a, argc, 0, 0), title, sizeof(title)) ||
                !str_copy(p, arg_or(a, argc, 1, 0), msg, sizeof(msg))) {
                return vm_cell_fail;
            }
            hype::ui::deskbar_notify(title, msg);
            return 0;
        }

        hcpp::u64 n_invalidate(VmProgram&, const hcpp::u64*,
                               hcpp::u8) noexcept {
            hype::win::wm_invalidate_all();
            return 0;
        }

        hcpp::u64 n_uptime(VmProgram&, const hcpp::u64*, hcpp::u8) noexcept {
            return hype::time::uptime_ms();
        }

        hcpp::u64 n_fmt_int(VmProgram& p, const hcpp::u64* a,
                            hcpp::u8 argc) noexcept {
            const hcpp::u8 slot = p.fmt_ring;
            p.fmt_ring = (p.fmt_ring + 1) % vm_fmt_slots;
            char* out = p.fmt[slot];
            hcpp::set(out, 0, vm_fmt_size);
            hcpp::i64 v = static_cast<hcpp::i64>(arg_or(a, argc, 0, 0));
            hcpp::u32 n = 0;
            if (v < 0) {
                out[n++] = '-';
                // Unsigned magnitude avoids the INT64_MIN corner.
                hcpp::u64 mag = static_cast<hcpp::u64>(-(v + 1)) + 1;
                char tmp[24]{};
                hcpp::u32 t = 0;
                do {
                    tmp[t++] = static_cast<char>('0' + (mag % 10));
                    mag /= 10;
                } while (mag != 0 && t < sizeof(tmp));
                while (t > 0 && n + 1 < vm_fmt_size) out[n++] = tmp[--t];
            } else {
                char tmp[24]{};
                hcpp::u32 t = 0;
                hcpp::u64 mag = static_cast<hcpp::u64>(v);
                do {
                    tmp[t++] = static_cast<char>('0' + (mag % 10));
                    mag /= 10;
                } while (mag != 0 && t < sizeof(tmp));
                while (t > 0 && n + 1 < vm_fmt_size) out[n++] = tmp[--t];
            }
            out[n] = '\0';
            return vm_tag_fmt | slot;
        }

        using NativeFn = hcpp::u64 (*)(VmProgram&, const hcpp::u64*,
                                       hcpp::u8) noexcept;

        NativeFn native_table[N_COUNT] = {
            /* 0 */ [](VmProgram&, const hcpp::u64*, hcpp::u8) noexcept -> hcpp::u64 { return 0; },
            /* 1 */ n_serial_str,
            /* 2 */ n_serial_dec,
            /* 3 */ n_ctx_rect,
            /* 4 */ n_fill_rect,
            /* 5 */ n_draw_text,
            /* 6 */ n_app_register,
            /* 7 */ n_fs_read,
            /* 8 */ n_fs_write,
            /* 9 */ n_heap_alloc,
            /* 10 */ n_heap_len,
            /* 11 */ n_heap_r8,
            /* 12 */ n_heap_w8,
            /* 13 */ n_notify,
            /* 14 */ n_invalidate,
            /* 15 */ n_uptime,
            /* 16 */ n_fmt_int,
        };

        bool jump_to(VmProgram& p, hcpp::u32& pc, hcpp::i16 off,
                     hcpp::i32& err) noexcept {
            const hcpp::i64 target =
                static_cast<hcpp::i64>(pc) + static_cast<hcpp::i64>(off);
            if (target < 0 || target >= p.code_size) {
                err = ERR_JUMP;
                return false;
            }
            pc = static_cast<hcpp::u32>(target);
            return true;
        }

    }

    // ---- public helpers ------------------------------------------------------

    hcpp::i32 vm_heap_alloc(VmProgram& prog, hcpp::u32 size) noexcept {
        if (prog.heap == nullptr || size == 0 ||
            size > vm_heap_bytes - prog.heap_bump) {
            return -1;
        }
        for (hcpp::u32 i = 0; i < vm_heap_slots; ++i) {
            if (prog.heap_slots[i].live) continue;
            prog.heap_slots[i] = {prog.heap_bump, size, 1};
            prog.heap_bump += size;
            return static_cast<hcpp::i32>(i);
        }
        return -1;
    }

    hcpp::u8* vm_heap_ptr(VmProgram& prog, hcpp::u64 handle,
                        hcpp::u32* len) noexcept {
        hcpp::u8* out = nullptr;
        return heap_at(prog, handle, out, len) ? out : nullptr;
    }

    bool vm_str(const VmProgram& prog, hcpp::u64 cell, const char*& out,
                hcpp::u32& len) noexcept {
        switch (cell >> 62) {
            case 2:  // vm_tag_str: const-pool string
                return str_at(prog, static_cast<hcpp::u16>(cell & 0xFFFF),
                              out, len);
            case 1: {  // vm_tag_heap: arena blob (not NUL-terminated)
                hcpp::u8* ptr = nullptr;
                if (!heap_at(prog, cell, ptr, &len)) return false;
                out = reinterpret_cast<const char*>(ptr);
                return true;
            }
            case 3: {  // vm_tag_fmt: per-module fmt scratch
                const hcpp::u32 slot = static_cast<hcpp::u32>(cell & 3);
                out = prog.fmt[slot];
                len = fmt_len(out);
                return true;
            }
            default:
                return false;
        }
    }

    // ---- interpreter ----------------------------------------------------------

    VmResult vm_exec(VmProgram& p, hcpp::u16 fn_id, const hcpp::u64* args,
                     hcpp::u8 argc, hcpp::u64 ops_limit) noexcept {
        VmResult res{};
        hcpp::i32& err = res.err;
        if (fn_id >= p.fn_count || p.code == nullptr) {
            err = ERR_CALL;
            return res;
        }
        p.sp = 0;
        p.depth = 0;
        p.locals_top = 0;

        // Initial frame: fn.locals cells of locals, args copied into the
        // first slots.
        hcpp::u16 cur_fn = fn_id;
        hcpp::u32 locals_base = 0;
        {
            const VmFn& f = p.fns[cur_fn];
            if (f.code_off >= p.code_size ||
                f.locals > vm_locals_per_fn || f.args > f.locals) {
                err = ERR_CALL;
                return res;
            }
            for (hcpp::u32 i = 0; i < f.locals; ++i) {
                p.locals[i] = i < f.args && i < argc ? args[i] : 0;
            }
            p.locals_top = f.locals;
        }
        hcpp::u32 pc = p.fns[cur_fn].code_off;

        bool running = true;
        while (running && err == ERR_NONE) {
            if (++res.ops > ops_limit) {
                err = ERR_LIMIT;
                break;
            }
            hcpp::u8 op = 0;
            if (!rd8(p, pc, op)) {
                err = ERR_BOUNDS;
                break;
            }
            ++pc;
            switch (op) {
                case OP_NOP:
                    break;
                case OP_HALT:
                    running = false;
                    if (p.sp > 0) res.value = p.stack[p.sp - 1];
                    break;
                case OP_PUSH: {
                    hcpp::u64 v = 0;
                    if (!rd64(p, pc, v)) { err = ERR_OPERAND; break; }
                    pc += 8;
                    if (!push(p, v)) err = ERR_STACK;
                    break;
                }
                case OP_PUSHC: {
                    hcpp::u16 i = 0;
                    if (!rd16(p, pc, i)) { err = ERR_OPERAND; break; }
                    pc += 2;
                    if (i >= p.const_count) { err = ERR_POOL; break; }
                    if (!push(p, p.consts[i])) err = ERR_STACK;
                    break;
                }
                case OP_PUSHSTR: {
                    hcpp::u16 i = 0;
                    if (!rd16(p, pc, i)) { err = ERR_OPERAND; break; }
                    pc += 2;
                    if (i >= p.str_count) { err = ERR_POOL; break; }
                    if (!push(p, vm_tag_str | i)) err = ERR_STACK;
                    break;
                }
                case OP_LOADL:
                case OP_STOREL: {
                    hcpp::u8 i = 0;
                    if (!rd8(p, pc, i)) { err = ERR_OPERAND; break; }
                    ++pc;
                    if (i >= p.fns[cur_fn].locals ||
                        locals_base + i >= p.locals_top) {
                        err = ERR_LOCAL;
                        break;
                    }
                    if (op == OP_LOADL) {
                        if (!push(p, p.locals[locals_base + i])) {
                            err = ERR_STACK;
                        }
                    } else {
                        hcpp::u64 v = 0;
                        if (!pop(p, v)) { err = ERR_STACK; break; }
                        p.locals[locals_base + i] = v;
                    }
                    break;
                }
                case OP_LOADG:
                case OP_STOREG: {
                    hcpp::u8 i = 0;
                    if (!rd8(p, pc, i)) { err = ERR_OPERAND; break; }
                    ++pc;
                    if (i >= vm_globals) { err = ERR_GLOBAL; break; }
                    if (op == OP_LOADG) {
                        if (!push(p, p.globals[i])) err = ERR_STACK;
                    } else {
                        hcpp::u64 v = 0;
                        if (!pop(p, v)) { err = ERR_STACK; break; }
                        p.globals[i] = v;
                    }
                    break;
                }
                case OP_POP: {
                    hcpp::u64 v = 0;
                    if (!pop(p, v)) err = ERR_STACK;
                    break;
                }
                case OP_DUP: {
                    hcpp::u64 v = 0;
                    if (!pop(p, v) || !push(p, v) || !push(p, v)) {
                        err = ERR_STACK;
                    }
                    break;
                }
                case OP_SWAP: {
                    hcpp::u64 a = 0, b = 0;
                    if (!pop(p, a) || !pop(p, b) || !push(p, a) ||
                        !push(p, b)) {
                        err = ERR_STACK;
                    }
                    break;
                }
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                case OP_MOD:
                case OP_AND:
                case OP_OR:
                case OP_XOR:
                case OP_SHL:
                case OP_SHR:
                case OP_CEQ:
                case OP_CNE:
                case OP_CLT:
                case OP_CLE:
                case OP_CGT:
                case OP_CGE: {
                    hcpp::u64 b = 0, a = 0;
                    if (!pop(p, b) || !pop(p, a)) { err = ERR_STACK; break; }
                    hcpp::u64 r = 0;
                    const hcpp::i64 sa = static_cast<hcpp::i64>(a);
                    const hcpp::i64 sb = static_cast<hcpp::i64>(b);
                    switch (op) {
                        case OP_ADD: r = a + b; break;
                        case OP_SUB: r = a - b; break;
                        case OP_MUL: r = a * b; break;
                        case OP_DIV:
                            if (b == 0 ||
                                (sa == (-9223372036854775807ll - 1) &&
                                 sb == -1)) {
                                err = ERR_ARITH;
                                break;
                            }
                            r = static_cast<hcpp::u64>(sa / sb);
                            break;
                        case OP_MOD:
                            if (b == 0) { err = ERR_ARITH; break; }
                            r = static_cast<hcpp::u64>(sa % sb);
                            break;
                        case OP_AND: r = a & b; break;
                        case OP_OR:  r = a | b; break;
                        case OP_XOR: r = a ^ b; break;
                        case OP_SHL: r = a << (b & 63); break;
                        case OP_SHR: r = a >> (b & 63); break;
                        case OP_CEQ: r = sa == sb ? 1 : 0; break;
                        case OP_CNE: r = sa != sb ? 1 : 0; break;
                        case OP_CLT: r = sa < sb ? 1 : 0; break;
                        case OP_CLE: r = sa <= sb ? 1 : 0; break;
                        case OP_CGT: r = sa > sb ? 1 : 0; break;
                        case OP_CGE: r = sa >= sb ? 1 : 0; break;
                        default: break;
                    }
                    if (err == ERR_NONE && !push(p, r)) err = ERR_STACK;
                    break;
                }
                case OP_NEG:
                case OP_NOT: {
                    hcpp::u64 a = 0;
                    if (!pop(p, a)) { err = ERR_STACK; break; }
                    const hcpp::u64 r =
                        op == OP_NEG
                            ? static_cast<hcpp::u64>(
                                  -static_cast<hcpp::i64>(a))
                            : ~a;
                    if (!push(p, r)) err = ERR_STACK;
                    break;
                }
                case OP_JMP:
                case OP_JZ:
                case OP_JNZ: {
                    hcpp::u16 raw = 0;
                    if (!rd16(p, pc, raw)) { err = ERR_OPERAND; break; }
                    pc += 2;
                    const hcpp::i16 off = static_cast<hcpp::i16>(raw);
                    bool take = true;
                    if (op != OP_JMP) {
                        hcpp::u64 v = 0;
                        if (!pop(p, v)) { err = ERR_STACK; break; }
                        take = (op == OP_JZ) ? (v == 0) : (v != 0);
                    }
                    if (take) jump_to(p, pc, off, err);
                    break;
                }
                case OP_CALL: {
                    hcpp::u16 target = 0;
                    if (!rd16(p, pc, target)) { err = ERR_OPERAND; break; }
                    pc += 2;
                    if (target >= p.fn_count || p.depth >= vm_frames) {
                        err = ERR_CALL;
                        break;
                    }
                    const VmFn& f = p.fns[target];
                    if (f.code_off >= p.code_size ||
                        f.locals > vm_locals_per_fn ||
                        f.args > f.locals ||
                        p.locals_top + f.locals > vm_locals_cells) {
                        err = ERR_CALL;
                        break;
                    }
                    if (p.sp < f.args) { err = ERR_STACK; break; }
                    // Callee locals: args popped in order (first declared
                    // arg sits deepest on the stack).
                    p.frames[p.depth++] = {pc, locals_base, cur_fn};
                    const hcpp::u32 nb = p.locals_top;
                    for (hcpp::u32 i = 0; i < f.locals; ++i) {
                        hcpp::u64 v = 0;
                        if (i < f.args) {
                            v = p.stack[p.sp - f.args + i];
                        }
                        p.locals[nb + i] = v;
                    }
                    p.sp -= f.args;
                    p.locals_top = nb + f.locals;
                    locals_base = nb;
                    cur_fn = target;
                    pc = f.code_off;
                    break;
                }
                case OP_RET: {
                    if (p.depth == 0) {
                        running = false;
                        if (p.sp > 0) res.value = p.stack[p.sp - 1];
                        break;
                    }
                    const VmFrame& fr = p.frames[--p.depth];
                    pc = fr.ret_pc;
                    p.locals_top = locals_base;
                    locals_base = fr.locals_base;
                    cur_fn = fr.fn;
                    break;
                }
                case OP_NCALL: {
                    hcpp::u16 id = 0;
                    hcpp::u8 nargc = 0;
                    if (!rd16(p, pc, id) ||
                        !rd8(p, pc + 2, nargc)) {
                        err = ERR_OPERAND;
                        break;
                    }
                    pc += 3;
                    if (id >= N_COUNT ||
                        ((p.natives_mask >> id) & 1ull) == 0) {
                        err = ERR_NATIVE;
                        break;
                    }
                    if (nargc > vm_max_native_args || p.sp < nargc) {
                        err = ERR_STACK;
                        break;
                    }
                    hcpp::u64 argv[vm_max_native_args]{};
                    for (hcpp::u32 i = 0; i < nargc; ++i) {
                        argv[i] = p.stack[p.sp - nargc + i];
                    }
                    p.sp -= nargc;
                    const hcpp::u64 r = native_table[id](p, argv, nargc);
                    if (!push(p, r)) err = ERR_STACK;
                    break;
                }
                default:
                    err = ERR_BAD_OP;
                    break;
            }
        }
        // Leave the locals stack consistent for the next exec.
        p.locals_top = 0;
        p.depth = 0;
        return res;
    }

}
