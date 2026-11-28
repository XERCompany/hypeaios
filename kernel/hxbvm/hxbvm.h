#pragma once

// hxbvm — compact stack-based bytecode VM (issue #136).
//
// .hxer apps are compiled into the kernel today; hxbvm interprets a
// small bytecode so apps can ship as .hxbc files on disk and run
// confined: the ISA has no raw pointers — programs touch the outside
// world only through a bounded native-call table (serial, fs, canvas
// draw, deskbar, wm invalidate) and every memory access is checked
// against fixed limits.
//
// Cells are u64. Three handle tags ride in the top two bits:
//   0x8000_...  const-pool string index (low 16 bits)
//   0x4000_...  heap arena allocation slot (low 8 bits)
//   0xC000_...  fmt scratch string slot (low 2 bits)
// Anything else is a plain integer cell.

#include "hcpp/types.hpp"

namespace hype::hxrt {
    struct AppCtx;
}

namespace hype::hxbvm {

    // ---- limits -------------------------------------------------------------
    constexpr hcpp::u32 vm_stack_cells = 256;
    constexpr hcpp::u32 vm_locals_cells = 512;
    constexpr hcpp::u32 vm_frames = 32;
    constexpr hcpp::u32 vm_locals_per_fn = 32;
    constexpr hcpp::u64 vm_ops_limit = 100000;
    constexpr hcpp::u32 vm_globals = 16;
    constexpr hcpp::u32 vm_heap_bytes = 64 * 1024;
    constexpr hcpp::u32 vm_heap_slots = 64;
    constexpr hcpp::u32 vm_max_fns = 32;
    constexpr hcpp::u32 vm_max_strs = 255;
    constexpr hcpp::u32 vm_max_consts = 256;
    constexpr hcpp::u32 vm_fmt_slots = 4;
    constexpr hcpp::u32 vm_fmt_size = 32;
    constexpr hcpp::u8 vm_max_native_args = 8;

    constexpr hcpp::u64 vm_tag_str = 0x8000000000000000ull;
    constexpr hcpp::u64 vm_tag_heap = 0x4000000000000000ull;
    constexpr hcpp::u64 vm_tag_fmt = 0xC000000000000000ull;
    constexpr hcpp::u64 vm_cell_fail = ~0ull;  // -1 sentinel pushed on errors

    // ---- ISA -----------------------------------------------------------------
    // Operand encodings are little-endian, operands follow the opcode byte.
    enum HxOp : hcpp::u8 {
        OP_NOP = 0x00,
        OP_HALT = 0x01,     // stop; result = stack top (0 when empty)
        OP_PUSH = 0x02,     // imm i64
        OP_PUSHC = 0x03,    // u16 const-pool index -> u64 cell
        OP_PUSHSTR = 0x04,  // u16 str-pool index -> tagged handle
        OP_LOADL = 0x05,    // u8 local index -> push
        OP_STOREL = 0x06,   // u8 local index <- pop
        OP_LOADG = 0x07,    // u8 global index (per module, persistent)
        OP_STOREG = 0x08,   // u8 global index <- pop
        OP_POP = 0x09,
        OP_DUP = 0x0A,
        OP_SWAP = 0x0B,
        OP_ADD = 0x10,      // i64 arithmetic
        OP_SUB = 0x11,
        OP_MUL = 0x12,
        OP_DIV = 0x13,      // traps on /0 and INT64_MIN/-1
        OP_MOD = 0x14,
        OP_NEG = 0x15,
        OP_AND = 0x18,      // bitwise
        OP_OR = 0x19,
        OP_XOR = 0x1A,
        OP_NOT = 0x1B,
        OP_SHL = 0x1C,      // shift amount masked to 0..63
        OP_SHR = 0x1D,      // logical shift on the u64 cell
        OP_CEQ = 0x20,      // signed i64 compare -> pushes 1/0
        OP_CNE = 0x21,
        OP_CLT = 0x22,
        OP_CLE = 0x23,
        OP_CGT = 0x24,
        OP_CGE = 0x25,
        OP_JMP = 0x30,      // i16 offset relative to the next instruction
        OP_JZ = 0x31,       // pop; jump when 0
        OP_JNZ = 0x32,      // pop; jump when != 0
        OP_NCALL = 0x38,    // u16 native id, u8 argc: pop argc, push result
        OP_RET = 0x39,      // pop frame (halts at depth 0)
        OP_CALL = 0x3A,     // u16 fn id; args already on stack
    };

    // ---- native table ----------------------------------------------------------
    enum HxNative : hcpp::u16 {
        N_NOP = 0,
        N_SERIAL_STR = 1,    // (strh) -> 0
        N_SERIAL_DEC = 2,    // (i64) -> 0, decimal on COM1
        N_CTX_RECT = 3,      // (i 0..3) -> ctx region x/y/w/h, 0 w/o ctx
        N_FILL_RECT = 4,     // (x,y,w,h,0xRRGGBB) -> 1/0 via ctx clip
        N_DRAW_TEXT = 5,     // (x,baseline_y,strh,0xRRGGBB) -> 1/0
        N_APP_REGISTER = 6,  // (name strh) -> 1/0; registers caller as .hxer app
        N_FS_READ = 7,       // (path strh) -> heap handle, vm_cell_fail on error
        N_FS_WRITE = 8,      // (path strh, heap handle) -> 0 ok, else status+1
        N_HEAP_ALLOC = 9,    // (size) -> heap handle, vm_cell_fail when full
        N_HEAP_LEN = 10,     // (heap handle) -> len, vm_cell_fail on bad handle
        N_HEAP_R8 = 11,      // (heap handle, off) -> byte, vm_cell_fail oob
        N_HEAP_W8 = 12,      // (heap handle, off, val) -> 0 ok, vm_cell_fail oob
        N_NOTIFY = 13,       // (title strh, msg strh) -> 0, deskbar toast
        N_INVALIDATE = 14,   // () -> 0; wm repaint everything
        N_UPTIME = 15,       // () -> ms since boot
        N_FMT_INT = 16,      // (i64) -> fmt-scratch strh of the decimal text
        N_COUNT = 17,
    };

    // ---- program view ---------------------------------------------------------
    struct VmFn {
        hcpp::u32 code_off;
        hcpp::u8 args;    // cells popped from the caller stack into locals
        hcpp::u8 locals;  // total local slots reserved (includes args)
    };

    struct VmHeapSlot {
        hcpp::u32 off;
        hcpp::u32 len;
        hcpp::u8 live;
    };

    struct VmFrame {
        hcpp::u32 ret_pc;
        hcpp::u32 locals_base;
        hcpp::u16 fn;
    };

    // Everything the interpreter needs: the parsed (validated) sections plus
    // the execution state. Embedded in the module record so the 7 KB of
    // stacks/frames lives in BSS, not on the kernel stack.
    struct VmProgram {
        const hcpp::u8* code = nullptr;
        hcpp::u32 code_size = 0;
        VmFn fns[vm_max_fns]{};
        hcpp::u16 fn_count = 0;
        const hcpp::u8* str_pool = nullptr;   // {u16 len, bytes} sequence
        hcpp::u32 str_pool_size = 0;
        hcpp::u16 str_count = 0;
        hcpp::u64 consts[vm_max_consts]{};
        hcpp::u16 const_count = 0;
        // One bit per native id: the module may only NCALL whitelisted
        // natives (parsed from the HXBC imports mask).
        hcpp::u64 natives_mask = 0;
        hcpp::u64 globals[vm_globals]{};

        // Bounded heap arena (64 KB PMM, bump allocator, freed on unload).
        hcpp::u8* heap = nullptr;
        VmHeapSlot heap_slots[vm_heap_slots]{};
        hcpp::u32 heap_bump = 0;
        char fmt[vm_fmt_slots][vm_fmt_size]{};
        hcpp::u8 fmt_ring = 0;

        // Set while a window-manager message is being dispatched so the
        // draw/ctx natives can reach the framebuffer + clip.
        const hype::hxrt::AppCtx* ctx = nullptr;
        // Back-pointer the N_APP_REGISTER native forwards to (hxbc.cc).
        void* host = nullptr;
        bool (*register_app)(void* host, const char* name) noexcept = nullptr;

        // Execution state (only meaningful inside vm_exec).
        hcpp::u64 stack[vm_stack_cells]{};
        hcpp::u32 sp = 0;
        hcpp::u64 locals[vm_locals_cells]{};
        hcpp::u32 locals_top = 0;
        VmFrame frames[vm_frames]{};
        hcpp::u32 depth = 0;
    };

    enum HxErr : hcpp::i32 {
        ERR_NONE = 0,
        ERR_BOUNDS = 1,     // instruction fetch past code end
        ERR_BAD_OP = 2,
        ERR_STACK = 3,      // under/overflow
        ERR_LOCAL = 4,
        ERR_GLOBAL = 5,
        ERR_POOL = 6,       // bad const/str index
        ERR_ARITH = 7,      // /0 or i64 overflow
        ERR_JUMP = 8,       // jump outside the code region
        ERR_CALL = 9,       // bad fn id or frame depth overflow
        ERR_NATIVE = 10,    // unknown / non-imported native
        ERR_OPERAND = 11,   // truncated operand
        ERR_LIMIT = 12,     // ops budget exhausted
    };

    struct VmResult {
        hcpp::u64 value = 0;   // stack top at HALT (0 when empty)
        hcpp::u64 ops = 0;     // instructions executed
        hcpp::i32 err = ERR_NONE;
    };

    // Runs fns[fn_id] with `argc` argument cells until HALT/RET at depth 0.
    // Bounded: at most `ops_limit` instructions, 256-cell stack, 32-deep
    // call frames, 512 locals cells — no recursion or looping can hang the
    // kernel.
    VmResult vm_exec(VmProgram& prog, hcpp::u16 fn_id, const hcpp::u64* args,
                     hcpp::u8 argc, hcpp::u64 ops_limit = vm_ops_limit) noexcept;

    // Resolves a tagged string handle (const-pool, heap or fmt scratch) to a
    // bounded pointer+len. False for plain cells / dead handles.
    bool vm_str(const VmProgram& prog, hcpp::u64 cell, const char*& out,
                hcpp::u32& len) noexcept;

    // Heap arena primitives shared by the loader and the natives.
    hcpp::i32 vm_heap_alloc(VmProgram& prog, hcpp::u32 size) noexcept;
    hcpp::u8* vm_heap_ptr(VmProgram& prog, hcpp::u64 handle,
                        hcpp::u32* len) noexcept;

}
