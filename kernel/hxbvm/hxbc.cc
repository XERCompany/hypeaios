#include "kernel/hxbvm/hxbc.h"

#include "hcpp/memory.hpp"
#include "kernel/hxbvm/hxbvm.h"
#include "kernel/hxrt/hxer.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/vfs/hypefs.h"

namespace hype::hxbvm {

    namespace {

        constexpr hcpp::u32 arena_pages = vm_heap_bytes / 4096;

        struct Module {
            bool used = false;
            hcpp::u8* blob = nullptr;
            hcpp::u32 blob_size = 0;
            char name[24]{};
            char path[64]{};
            hcpp::u8 kind = 0;
            hcpp::u16 entry_fn = hxbc_fn_none;
            hcpp::u16 msg_fn = hxbc_fn_none;
            hcpp::u16 click_fn = hxbc_fn_none;
            hxrt::HxerHeader hdr{};
            bool registered = false;
            VmResult last{};
            VmProgram prog{};
        };

        Module modules[hxbc_max_modules];

        // ---- bounded blob readers ----------------------------------------------
        hcpp::u16 rb16(const hcpp::u8* b, hcpp::u32 off) noexcept {
            return static_cast<hcpp::u16>(b[off]) |
                   (static_cast<hcpp::u16>(b[off + 1]) << 8);
        }

        hcpp::u32 rb32(const hcpp::u8* b, hcpp::u32 off) noexcept {
            hcpp::u32 v = 0;
            for (hcpp::u32 i = 0; i < 4; ++i) {
                v |= static_cast<hcpp::u32>(b[off + i]) << (i * 8);
            }
            return v;
        }

        hcpp::u64 rb64(const hcpp::u8* b, hcpp::u32 off) noexcept {
            hcpp::u64 v = 0;
            for (hcpp::u32 i = 0; i < 8; ++i) {
                v |= static_cast<hcpp::u64>(b[off + i]) << (i * 8);
            }
            return v;
        }

        void field_copy(char* dest, const char* src, hcpp::u32 cap) noexcept {
            hcpp::u32 i = 0;
            while (src != nullptr && src[i] != '\0' && i + 1 < cap) {
                dest[i] = src[i];
                ++i;
            }
            dest[i] = '\0';
        }

        // ---- HXBC validation + parse ---------------------------------------------
        // Fills m->prog (sections point into the caller-owned blob) and the
        // scalar header fields. Nothing executes here — pure layout checks.
        bool parse_blob(const hcpp::u8* b, hcpp::u32 size,
                        Module& m) noexcept {
            if (b == nullptr || size < hxbc_header_size ||
                size > hxbc_max_blob) {
                return false;
            }
            if (rb32(b, 0) != hxbc_magic || rb16(b, 4) != hxbc_version) {
                return false;
            }
            const hcpp::u8 kind = b[6];
            if (kind > hxbc_kind_app) return false;
            // name must be NUL-terminated inside its 24-byte field.
            bool named = false;
            for (hcpp::u32 i = 0; i < 24; ++i) {
                if (b[8 + i] == '\0') { named = i != 0; break; }
            }
            if (!named) return false;
            const hcpp::u64 natives = rb64(b, 32);
            const hcpp::u16 entry_fn = rb16(b, 40);
            const hcpp::u16 msg_fn = rb16(b, 42);
            const hcpp::u16 click_fn = rb16(b, 44);
            const hcpp::u16 fn_count = rb16(b, 46);
            const hcpp::u16 str_count = rb16(b, 48);
            const hcpp::u16 const_count = rb16(b, 50);
            const hcpp::u32 code_size = rb32(b, 52);
            const hcpp::u32 body_sum = rb32(b, 56);
            if (fn_count == 0 || fn_count > vm_max_fns ||
                str_count > vm_max_strs || const_count > vm_max_consts ||
                code_size == 0 || code_size > hxbc_max_code) {
                return false;
            }
            if ((entry_fn != hxbc_fn_none && entry_fn >= fn_count) ||
                (msg_fn != hxbc_fn_none && msg_fn >= fn_count) ||
                (click_fn != hxbc_fn_none && click_fn >= fn_count)) {
                return false;
            }
            // Section walk: fns -> consts -> strs -> code must tile the
            // file exactly.
            const hcpp::u32 fns_off = hxbc_header_size;
            const hcpp::u32 consts_off = fns_off + fn_count * 8;
            if (consts_off + const_count * 8 > size) return false;
            const hcpp::u32 strs_off = consts_off + const_count * 8;
            hcpp::u32 off = strs_off;
            for (hcpp::u16 i = 0; i < str_count; ++i) {
                if (off + 2 > size) return false;
                const hcpp::u32 len = rb16(b, off);
                off += 2;
                if (off + len > size) return false;
                off += len;
            }
            const hcpp::u32 code_off = off;
            if (code_off + code_size != size) return false;
            // Cheap integrity check — rejects truncated/corrupt blobs.
            hcpp::u32 sum = 0;
            for (hcpp::u32 i = hxbc_header_size; i < size; ++i) {
                sum += b[i];
            }
            if (sum != body_sum) return false;

            // Fill the program view.
            VmProgram& p = m.prog;
            p = VmProgram{};
            p.code = b + code_off;
            p.code_size = code_size;
            p.fn_count = fn_count;
            for (hcpp::u16 i = 0; i < fn_count; ++i) {
                const hcpp::u32 fo = fns_off + i * 8;
                const hcpp::u32 co = rb32(b, fo);
                const hcpp::u8 args = b[fo + 4];
                const hcpp::u8 locals = b[fo + 5];
                if (co >= code_size || locals > vm_locals_per_fn ||
                    args > locals) {
                    p = VmProgram{};
                    return false;
                }
                p.fns[i] = {co, args, locals};
            }
            p.const_count = const_count;
            for (hcpp::u16 i = 0; i < const_count; ++i) {
                p.consts[i] = rb64(b, consts_off + i * 8);
            }
            p.str_pool = b + strs_off;
            p.str_pool_size = code_off - strs_off;
            p.str_count = str_count;
            p.natives_mask = natives;

            m.kind = kind;
            m.entry_fn = entry_fn;
            m.msg_fn = msg_fn;
            m.click_fn = click_fn;
            field_copy(m.name, reinterpret_cast<const char*>(b + 8),
                       sizeof(m.name));
            return true;
        }

        // ---- window-manager bridge ------------------------------------------------
        Module* module_for_ctx(const hxrt::AppCtx& ctx) noexcept {
            if (ctx.app_id == 0 ||
                ctx.app_id - 1 >= hxrt::hxrt_count()) {
                return nullptr;
            }
            const hxrt::HxerHeader* h = hxrt::hxrt_list()[ctx.app_id - 1];
            for (hcpp::u32 i = 0; i < hxbc_max_modules; ++i) {
                if (modules[i].registered && &modules[i].hdr == h) {
                    return &modules[i];
                }
            }
            return nullptr;
        }

        void dispatch_ctx(Module& m, hcpp::u32 msg, hcpp::u64 w,
                          hcpp::u64 l, const hxrt::AppCtx* ctx) noexcept {
            const hcpp::u16 fn =
                (msg == hxbc_msg_lbuttondown &&
                 m.click_fn != hxbc_fn_none)
                    ? m.click_fn
                    : m.msg_fn;
            if (fn == hxbc_fn_none) return;
            m.prog.ctx = ctx;
            const hcpp::u64 args[3] = {msg, w, l};
            m.last = vm_exec(m.prog, fn, args, 3);
            m.prog.ctx = nullptr;
            if (m.last.err != ERR_NONE) {
                serial::write("hxbvm err=");
                serial::write_dec32(
                    static_cast<hcpp::u32>(m.last.err));
                serial::write("\r\n");
            }
        }

        void hxbc_thunk_entry(hxrt::AppCtx& ctx) noexcept {
            Module* m = module_for_ctx(ctx);
            if (m != nullptr) {
                dispatch_ctx(*m, hxbc_msg_paint, 0, 0, &ctx);
            }
        }

        void hxbc_thunk_click(hxrt::AppCtx& ctx, hcpp::i32 x,
                              hcpp::i32 y) noexcept {
            Module* m = module_for_ctx(ctx);
            if (m != nullptr) {
                const hcpp::u64 l =
                    (static_cast<hcpp::u64>(
                         static_cast<hcpp::u16>(y))
                     << 16) |
                    static_cast<hcpp::u16>(x);
                dispatch_ctx(*m, hxbc_msg_lbuttondown, 0, l, &ctx);
            }
        }

        void hxbc_thunk_close(hxrt::AppCtx& ctx) noexcept {
            Module* m = module_for_ctx(ctx);
            if (m != nullptr) {
                dispatch_ctx(*m, hxbc_msg_close, 0, 0, &ctx);
            }
        }

        // N_APP_REGISTER native -> host callback (set on prog.register_app).
        bool register_app_cb(void* host, const char* name) noexcept {
            Module& m = *static_cast<Module*>(host);
            if (m.registered) return true;
            if (m.msg_fn == hxbc_fn_none) return false;
            m.hdr = {};
            m.hdr.magic = hxrt::hxer_magic;
            m.hdr.ver = 1;
            m.hdr.kind =
                static_cast<hcpp::u8>(hxrt::HxerKind::User);
            m.hdr.flags = 0;
            field_copy(m.hdr.name,
                       name != nullptr && name[0] != '\0' ? name : m.name,
                       sizeof(m.hdr.name));
            field_copy(m.hdr.path, m.path, sizeof(m.hdr.path));
            m.hdr.entry = &hxbc_thunk_entry;
            m.hdr.on_close = &hxbc_thunk_close;
            m.hdr.on_click = &hxbc_thunk_click;
            if (!hxrt::hxrt_register(&m.hdr)) return false;
            m.registered = true;
            serial::write("hxbc app=");
            serial::write_line(m.hdr.name);
            return true;
        }

        // ---- module install -------------------------------------------------------
        hcpp::i32 install_blob(hcpp::u8* blob, hcpp::u32 size,
                               const char* path) noexcept {
            hcpp::u32 slot = hxbc_max_modules;
            for (hcpp::u32 i = 0; i < hxbc_max_modules; ++i) {
                if (!modules[i].used) {
                    slot = i;
                    break;
                }
            }
            if (slot >= hxbc_max_modules) return -1;
            Module& m = modules[slot];
            if (!parse_blob(blob, size, m)) return -1;
            // Bounded heap arena: 64 KiB of PMM, reclaimed on unload.
            const hcpp::uintptr_t arena =
                hype::memory::pmm_allocate_pages(arena_pages);
            if (arena == 0) {
                m.prog = VmProgram{};
                return -1;
            }
            m.used = true;
            m.blob = blob;
            m.blob_size = size;
            field_copy(m.path, path, sizeof(m.path));
            m.prog.heap = reinterpret_cast<hcpp::u8*>(arena);
            m.prog.host = &m;
            m.prog.register_app = &register_app_cb;
            // Run-once entry point (init code — e.g. app registration).
            if (m.entry_fn != hxbc_fn_none) {
                m.last = vm_exec(m.prog, m.entry_fn, nullptr, 0);
            }
            serial::write("hxbvm load=");
            serial::write(m.name);
            serial::write(" ops=");
            serial::write_dec32(
                static_cast<hcpp::u32>(m.last.ops));
            serial::write("\r\n");
            if (m.last.err != ERR_NONE) {
                serial::write("hxbvm err=");
                serial::write_dec32(
                    static_cast<hcpp::u32>(m.last.err));
                serial::write("\r\n");
            }
            // App modules self-register so desk_open_app finds them.
            if (m.kind == hxbc_kind_app && m.msg_fn != hxbc_fn_none) {
                hxbc_app_register(static_cast<hcpp::i32>(slot));
            }
            return static_cast<hcpp::i32>(slot);
        }

        // ---- tiny bytecode emitter (demo + selftest blobs) --------------------------
        struct Emit {
            hcpp::u8* b;
            hcpp::u32 n;
            hcpp::u32 here() const noexcept { return n; }
            void o(hcpp::u8 v) noexcept { b[n++] = v; }
            void o16(hcpp::u16 v) noexcept {
                o(static_cast<hcpp::u8>(v & 0xFF));
                o(static_cast<hcpp::u8>(v >> 8));
            }
            void o64(hcpp::u64 v) noexcept {
                for (hcpp::u32 i = 0; i < 8; ++i) {
                    o(static_cast<hcpp::u8>(v >> (i * 8)));
                }
            }
            void push(hcpp::u64 v) noexcept { o(OP_PUSH); o64(v); }
            void local(hcpp::u8 op, hcpp::u8 i) noexcept { o(op); o(i); }
            void ncall(hcpp::u16 id, hcpp::u8 argc) noexcept {
                o(OP_NCALL);
                o16(id);
                o(argc);
            }
            // Jump with a patchable operand: returns operand position.
            hcpp::u32 jfwd(hcpp::u8 op) noexcept {
                o(op);
                const hcpp::u32 at = n;
                o16(0);
                return at;
            }
            void jback(hcpp::u8 op, hcpp::u32 target) noexcept {
                o(op);
                const hcpp::i32 off = static_cast<hcpp::i32>(target) -
                                      static_cast<hcpp::i32>(n + 2);
                o16(static_cast<hcpp::u16>(off));
            }
            void patch(hcpp::u32 operand_at) noexcept {
                const hcpp::i32 off =
                    static_cast<hcpp::i32>(n) -
                    static_cast<hcpp::i32>(operand_at + 2);
                b[operand_at] = static_cast<hcpp::u8>(off & 0xFF);
                b[operand_at + 1] =
                    static_cast<hcpp::u8>((off >> 8) & 0xFF);
            }
        };

        // Serializes one module blob: header + fn table + (empty) const
        // pool + str pool + code + checksum.
        hcpp::u32 build_blob(hcpp::u8* out, const char* name,
                             hcpp::u8 kind, hcpp::u64 natives,
                             hcpp::u16 entry_fn, hcpp::u16 msg_fn,
                             const VmFn* fns, hcpp::u16 fn_count,
                             const char* const* strs, hcpp::u16 str_count,
                             const hcpp::u8* code,
                             hcpp::u32 code_size) noexcept {
            hcpp::set(out, 0, hxbc_header_size);
            hcpp::u32 off = hxbc_header_size;
            for (hcpp::u16 i = 0; i < fn_count; ++i) {
                out[off++] = static_cast<hcpp::u8>(fns[i].code_off);
                out[off++] = static_cast<hcpp::u8>(fns[i].code_off >> 8);
                out[off++] = static_cast<hcpp::u8>(fns[i].code_off >> 16);
                out[off++] = static_cast<hcpp::u8>(fns[i].code_off >> 24);
                out[off++] = fns[i].args;
                out[off++] = fns[i].locals;
                out[off++] = 0;
                out[off++] = 0;
            }
            for (hcpp::u16 i = 0; i < str_count; ++i) {
                hcpp::u32 len = 0;
                while (strs[i][len] != '\0') ++len;
                out[off++] = static_cast<hcpp::u8>(len & 0xFF);
                out[off++] = static_cast<hcpp::u8>(len >> 8);
                hcpp::copy(out + off, strs[i], len);
                off += len;
            }
            hcpp::copy(out + off, code, code_size);
            off += code_size;

            out[0] = 'H';
            out[1] = 'X';
            out[2] = 'B';
            out[3] = 'C';
            out[4] = static_cast<hcpp::u8>(hxbc_version & 0xFF);
            out[5] = static_cast<hcpp::u8>(hxbc_version >> 8);
            out[6] = kind;
            out[7] = 0;
            field_copy(reinterpret_cast<char*>(out + 8), name, 24);
            for (hcpp::u32 i = 0; i < 8; ++i) {
                out[32 + i] =
                    static_cast<hcpp::u8>(natives >> (i * 8));
            }
            out[40] = static_cast<hcpp::u8>(entry_fn & 0xFF);
            out[41] = static_cast<hcpp::u8>(entry_fn >> 8);
            out[42] = static_cast<hcpp::u8>(msg_fn & 0xFF);
            out[43] = static_cast<hcpp::u8>(msg_fn >> 8);
            out[44] = 0xFF;  // click_fn = none (routes through msg_fn)
            out[45] = 0xFF;
            out[46] = static_cast<hcpp::u8>(fn_count & 0xFF);
            out[47] = static_cast<hcpp::u8>(fn_count >> 8);
            out[48] = static_cast<hcpp::u8>(str_count & 0xFF);
            out[49] = static_cast<hcpp::u8>(str_count >> 8);
            out[50] = 0;  // const_count
            out[51] = 0;
            out[52] = static_cast<hcpp::u8>(code_size);
            out[53] = static_cast<hcpp::u8>(code_size >> 8);
            out[54] = static_cast<hcpp::u8>(code_size >> 16);
            out[55] = static_cast<hcpp::u8>(code_size >> 24);
            hcpp::u32 sum = 0;
            for (hcpp::u32 i = hxbc_header_size; i < off; ++i) {
                sum += out[i];
            }
            out[56] = static_cast<hcpp::u8>(sum);
            out[57] = static_cast<hcpp::u8>(sum >> 8);
            out[58] = static_cast<hcpp::u8>(sum >> 16);
            out[59] = static_cast<hcpp::u8>(sum >> 24);
            return off;
        }

        // ---- selftest blob ---------------------------------------------------------
        // fn0 main: acc = sum(1..10) via CALL add2 + heap r/w + fs round
        //           trip; penalties added on any failure -> expect 55.
        // fn1 div0: traps ERR_ARITH. fn2 add2: a+b. fn3 spin: unbounded
        //           loop -> ERR_LIMIT. fn4: calls a non-imported native
        //           -> ERR_NATIVE.
        hcpp::u32 build_selftest_blob(hcpp::u8* out) noexcept {
            hcpp::u8 code[512];
            Emit e{code, 0};
            const hcpp::u32 fn0_off = e.here();
            // acc = 0; i = 1
            e.push(0);
            e.local(OP_STOREL, 1);
            e.push(1);
            e.local(OP_STOREL, 0);
            const hcpp::u32 loop = e.here();
            e.local(OP_LOADL, 0);
            e.push(10);
            e.o(OP_CLE);
            const hcpp::u32 j_done = e.jfwd(OP_JZ);
            e.local(OP_LOADL, 1);
            e.local(OP_LOADL, 0);
            e.o(OP_CALL);
            e.o16(2);  // add2(acc, i)
            e.local(OP_STOREL, 1);
            e.local(OP_LOADL, 0);
            e.push(1);
            e.o(OP_ADD);
            e.local(OP_STOREL, 0);
            e.jback(OP_JMP, loop);
            e.patch(j_done);
            // heap: alloc 16 -> local2; [3] = 0xAB; readback check
            e.push(16);
            e.ncall(N_HEAP_ALLOC, 1);
            e.local(OP_STOREL, 2);
            e.local(OP_LOADL, 2);
            e.push(3);
            e.push(0xAB);
            e.ncall(N_HEAP_W8, 3);
            e.o(OP_POP);
            e.local(OP_LOADL, 2);
            e.push(3);
            e.ncall(N_HEAP_R8, 2);
            e.push(0xAB);
            e.o(OP_CEQ);
            const hcpp::u32 j_ok1 = e.jfwd(OP_JNZ);
            e.local(OP_LOADL, 1);
            e.push(100);
            e.o(OP_ADD);
            e.local(OP_STOREL, 1);
            e.patch(j_ok1);
            // fs_write("HYPEOS://U/home/hxbvm.tst", heap) must return 0
            e.o(OP_PUSHSTR);
            e.o16(0);
            e.local(OP_LOADL, 2);
            e.ncall(N_FS_WRITE, 2);
            e.push(0);
            e.o(OP_CEQ);
            const hcpp::u32 j_ok2 = e.jfwd(OP_JNZ);
            e.local(OP_LOADL, 1);
            e.push(200);
            e.o(OP_ADD);
            e.local(OP_STOREL, 1);
            e.patch(j_ok2);
            // fs_read back -> local3; byte 3 must still be 0xAB
            e.o(OP_PUSHSTR);
            e.o16(0);
            e.ncall(N_FS_READ, 1);
            e.local(OP_STOREL, 3);
            e.local(OP_LOADL, 3);
            e.push(3);
            e.ncall(N_HEAP_R8, 2);
            e.push(0xAB);
            e.o(OP_CEQ);
            const hcpp::u32 j_ok3 = e.jfwd(OP_JNZ);
            e.local(OP_LOADL, 1);
            e.push(400);
            e.o(OP_ADD);
            e.local(OP_STOREL, 1);
            e.patch(j_ok3);
            e.o(OP_PUSHSTR);
            e.o16(1);
            e.ncall(N_SERIAL_STR, 1);
            e.o(OP_POP);
            e.local(OP_LOADL, 1);
            e.o(OP_RET);
            // fn1: div by zero -> ERR_ARITH
            const hcpp::u32 fn1_off = e.here();
            e.push(1);
            e.push(0);
            e.o(OP_DIV);
            e.o(OP_RET);
            // fn2: add2(a, b)
            const hcpp::u32 fn2_off = e.here();
            e.local(OP_LOADL, 0);
            e.local(OP_LOADL, 1);
            e.o(OP_ADD);
            e.o(OP_RET);
            // fn3: unbounded loop -> ERR_LIMIT
            const hcpp::u32 fn3_off = e.here();
            e.jback(OP_JMP, fn3_off);
            // fn4: notify is not in the imports mask -> ERR_NATIVE
            const hcpp::u32 fn4_off = e.here();
            e.o(OP_PUSHSTR);
            e.o16(0);
            e.o(OP_PUSHSTR);
            e.o16(1);
            e.ncall(N_NOTIFY, 2);
            e.o(OP_RET);

            const VmFn fns[5] = {
                {fn0_off, 0, 4},
                {fn1_off, 0, 0},
                {fn2_off, 2, 2},
                {fn3_off, 0, 0},
                {fn4_off, 0, 0},
            };
            const char* strs[] = {"HYPEOS://U/home/hxbvm.tst",
                                  "hxbvm code=run\r\n"};
            const hcpp::u64 mask = (1ull << N_HEAP_ALLOC) |
                                   (1ull << N_HEAP_W8) |
                                   (1ull << N_HEAP_R8) |
                                   (1ull << N_FS_WRITE) |
                                   (1ull << N_FS_READ) |
                                   (1ull << N_SERIAL_STR) |
                                   (1ull << N_SERIAL_DEC);
            return build_blob(out, "hxbvm_selftest", hxbc_kind_program,
                              mask, 0, hxbc_fn_none, fns, 5, strs, 2,
                              code, e.n);
        }

        // ---- demo app blob ---------------------------------------------------------
        // fn0 main: register the app + zero the counter global.
        // fn1 on_msg(msg, w, l): LBUTTONDOWN bumps global 0 and
        //        invalidates; PAINT fills the client, draws the label
        //        and the counter through the ctx/canvas natives.
        hcpp::u32 build_demo_blob(hcpp::u8* out) noexcept {
            hcpp::u8 code[1024];
            Emit e{code, 0};
            const hcpp::u32 fn0_off = e.here();
            e.push(0);
            e.local(OP_STOREG, 0);
            e.o(OP_PUSHSTR);
            e.o16(0);  // "HxDemo"
            e.ncall(N_APP_REGISTER, 1);
            e.o(OP_POP);
            e.o(OP_PUSHSTR);
            e.o16(1);  // "hxdemo ready\r\n"
            e.ncall(N_SERIAL_STR, 1);
            e.o(OP_POP);
            e.push(0);
            e.o(OP_RET);

            const hcpp::u32 fn1_off = e.here();
            // if msg == LBUTTONDOWN: ++global0, invalidate
            e.local(OP_LOADL, 0);
            e.push(hxbc_msg_lbuttondown);
            e.o(OP_CEQ);
            const hcpp::u32 j_notclick = e.jfwd(OP_JZ);
            e.local(OP_LOADG, 0);
            e.push(1);
            e.o(OP_ADD);
            e.local(OP_STOREG, 0);
            e.ncall(N_INVALIDATE, 0);
            e.o(OP_POP);
            e.patch(j_notclick);
            // if msg != PAINT: done
            e.local(OP_LOADL, 0);
            e.push(hxbc_msg_paint);
            e.o(OP_CEQ);
            const hcpp::u32 j_end = e.jfwd(OP_JZ);
            // fill client bg
            e.push(0);
            e.ncall(N_CTX_RECT, 1);
            e.push(1);
            e.ncall(N_CTX_RECT, 1);
            e.push(2);
            e.ncall(N_CTX_RECT, 1);
            e.push(3);
            e.ncall(N_CTX_RECT, 1);
            e.push(0x1B2230);
            e.ncall(N_FILL_RECT, 5);
            e.o(OP_POP);
            // "hxbvm demo - clicks:" at (x+14, y+26)
            e.push(0);
            e.ncall(N_CTX_RECT, 1);
            e.push(14);
            e.o(OP_ADD);
            e.push(1);
            e.ncall(N_CTX_RECT, 1);
            e.push(26);
            e.o(OP_ADD);
            e.o(OP_PUSHSTR);
            e.o16(2);
            e.push(0xE8ECF4);
            e.ncall(N_DRAW_TEXT, 4);
            e.o(OP_POP);
            // counter value at (x+14, y+46)
            e.push(0);
            e.ncall(N_CTX_RECT, 1);
            e.push(14);
            e.o(OP_ADD);
            e.push(1);
            e.ncall(N_CTX_RECT, 1);
            e.push(46);
            e.o(OP_ADD);
            e.local(OP_LOADG, 0);
            e.ncall(N_FMT_INT, 1);
            e.push(0xFFC84D);
            e.ncall(N_DRAW_TEXT, 4);
            e.o(OP_POP);
            // hint line at (x+14, y+66)
            e.push(0);
            e.ncall(N_CTX_RECT, 1);
            e.push(14);
            e.o(OP_ADD);
            e.push(1);
            e.ncall(N_CTX_RECT, 1);
            e.push(66);
            e.o(OP_ADD);
            e.o(OP_PUSHSTR);
            e.o16(3);
            e.push(0x9AA4B8);
            e.ncall(N_DRAW_TEXT, 4);
            e.o(OP_POP);
            e.patch(j_end);
            e.push(0);
            e.o(OP_RET);

            const VmFn fns[2] = {
                {fn0_off, 0, 1},
                {fn1_off, 3, 3},
            };
            const char* strs[] = {"HxDemo", "hxdemo ready\r\n",
                                  "hxbvm demo - clicks:",
                                  "click client area to count"};
            const hcpp::u64 mask = (1ull << N_APP_REGISTER) |
                                   (1ull << N_SERIAL_STR) |
                                   (1ull << N_CTX_RECT) |
                                   (1ull << N_FILL_RECT) |
                                   (1ull << N_DRAW_TEXT) |
                                   (1ull << N_FMT_INT) |
                                   (1ull << N_INVALIDATE);
            return build_blob(out, "HxDemo", hxbc_kind_app, mask, 0, 1,
                              fns, 2, strs, 4, code, e.n);
        }

    }

    // ---- public API -------------------------------------------------------------

    hcpp::i32 hxbc_load(const char* path) noexcept {
        if (path == nullptr) return -1;
        hype::vfs::FsStat st;
        if (hype::vfs::fs_stat(path, st) != hype::vfs::FsStatus::Ok ||
            (st.flags & hype::vfs::FS_DIR) != 0 ||
            st.size < hxbc_header_size || st.size > hxbc_max_blob) {
            return -1;
        }
        hcpp::u8* blob = static_cast<hcpp::u8*>(
            hype::memory::heap_alloc(st.size));
        if (blob == nullptr) return -1;
        hcpp::u32 got = 0;
        if (hype::vfs::fs_read(path, blob, st.size, &got) !=
                hype::vfs::FsStatus::Ok ||
            got != st.size) {
            hype::memory::heap_free(blob);
            return -1;
        }
        const hcpp::i32 idx = install_blob(blob, st.size, path);
        if (idx < 0) hype::memory::heap_free(blob);
        return idx;
    }

    hcpp::i32 hxbc_load_blob(const void* src, hcpp::u32 size) noexcept {
        if (src == nullptr || size < hxbc_header_size ||
            size > hxbc_max_blob) {
            return -1;
        }
        hcpp::u8* blob = static_cast<hcpp::u8*>(
            hype::memory::heap_alloc(size));
        if (blob == nullptr) return -1;
        hcpp::copy(blob, src, size);
        const hcpp::i32 idx = install_blob(blob, size, "blob");
        if (idx < 0) hype::memory::heap_free(blob);
        return idx;
    }

    bool hxbc_app_register(hcpp::u32 idx) noexcept {
        if (idx < 0 || static_cast<hcpp::u32>(idx) >= hxbc_max_modules ||
            !modules[idx].used) {
            return false;
        }
        return register_app_cb(&modules[idx], nullptr);
    }

    bool hxbc_run(hcpp::u32 idx) noexcept {
        if (idx < 0 || static_cast<hcpp::u32>(idx) >= hxbc_max_modules ||
            !modules[idx].used ||
            modules[idx].entry_fn == hxbc_fn_none) {
            return false;
        }
        Module& m = modules[idx];
        m.last = vm_exec(m.prog, m.entry_fn, nullptr, 0);
        return m.last.err == ERR_NONE;
    }

    bool hxbc_dispatch(hcpp::u32 idx, hcpp::u32 msg, hcpp::u64 w,
                       hcpp::u64 l) noexcept {
        if (idx < 0 || static_cast<hcpp::u32>(idx) >= hxbc_max_modules ||
            !modules[idx].used) {
            return false;
        }
        dispatch_ctx(modules[idx], msg, w, l, nullptr);
        return modules[idx].last.err == ERR_NONE;
    }

    hcpp::u32 hxbc_count() noexcept {
        hcpp::u32 n = 0;
        for (hcpp::u32 i = 0; i < hxbc_max_modules; ++i) {
            if (modules[i].used) ++n;
        }
        return n;
    }

    const char* hxbc_name(hcpp::u32 idx) noexcept {
        if (idx < 0 || static_cast<hcpp::u32>(idx) >= hxbc_max_modules ||
            !modules[idx].used) {
            return "";
        }
        return modules[idx].name;
    }

    bool hxbvm_selftest() noexcept {
        hcpp::u8 blob[1024];
        const hcpp::u32 size = build_selftest_blob(blob);
        const hcpp::i32 idx = hxbc_load_blob(blob, size);
        if (idx < 0) {
            serial::write("hxbvm selftest=fail\r\n");
            return false;
        }
        Module& m = modules[idx];
        // install_blob already ran entry_fn: result must be exactly 55
        // (sum 1..10; any heap/fs failure adds a penalty).
        bool ok = m.last.err == ERR_NONE && m.last.value == 55 &&
                  m.last.ops > 0;
        // Trap probes: div-by-zero, unbounded loop, non-imported native.
        const VmResult div0 = vm_exec(m.prog, 1, nullptr, 0);
        ok = ok && div0.err == ERR_ARITH;
        const VmResult spin = vm_exec(m.prog, 3, nullptr, 0, 1000);
        ok = ok && spin.err == ERR_LIMIT && spin.ops > 1000;
        const VmResult denied = vm_exec(m.prog, 4, nullptr, 0);
        ok = ok && denied.err == ERR_NATIVE;
        serial::write("hxbvm acc=");
        serial::write_dec32(
            static_cast<hcpp::u32>(m.last.value));
        serial::write("\r\n");
        serial::write("hxbvm selftest=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

    void hxbvm_boot() noexcept {
        serial::write("hxbvm isa=37 natives=");
        serial::write_dec32(N_COUNT - 1);
        serial::write("\r\n");
        hxbvm_selftest();
        // Seed the demo app onto the user disk, then load it back
        // through the real fs_read path — same route a third-party
        // .hxbc would take.
        const hype::vfs::FsStatus mk =
            hype::vfs::fs_mkdir("HYPEOS://U/apps");
        if (mk != hype::vfs::FsStatus::Ok &&
            mk != hype::vfs::FsStatus::Exists) {
            serial::write("hxbvm seed=fail\r\n");
            return;
        }
        hcpp::u8 blob[2048];
        const hcpp::u32 size = build_demo_blob(blob);
        const hype::vfs::FsStatus st = hype::vfs::fs_write(
            "HYPEOS://U/apps/hxdemo.hxbc", blob, size);
        serial::write("hxbvm seed=");
        serial::write_line(st == hype::vfs::FsStatus::Ok ? "ok" : "fail");
        if (st != hype::vfs::FsStatus::Ok) return;
        const hcpp::i32 idx = hxbc_load("HYPEOS://U/apps/hxdemo.hxbc");
        serial::write("hxbvm mods=");
        serial::write_dec32(hxbc_count());
        serial::write("\r\n");
        if (idx < 0) {
            serial::write("hxbc app=fail\r\n");
            return;
        }
        // on_msg probe: dispatch PAINT + CLOSE into the loaded program's
        // msg_fn (ctx=nullptr — the draw natives degrade to 0). Proves
        // the wm->VM message path the HxDemo window will use.
        const bool paint_ok = hxbc_dispatch(idx, hxbc_msg_paint, 0, 0);
        const bool close_ok = hxbc_dispatch(idx, hxbc_msg_close, 0, 0);
        serial::write("hxbvm msg=");
        serial::write_line(paint_ok && close_ok ? "ok" : "fail");
    }

}
