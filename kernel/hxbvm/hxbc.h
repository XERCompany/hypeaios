#pragma once

// HXBC — on-disk bytecode container for hxbvm (issue #136).
//
// File layout (all little-endian, 64-byte header):
//   0   u32 magic        'HXBC' = 0x43425848
//   4   u16 version      = 1
//   6   u8  kind         0 = program, 1 = app (registers a .hxer app)
//   7   u8  flags        (reserved)
//   8   char name[24]
//   32  u64 natives_mask imports bitmask — bit N allows native id N
//   40  u16 entry_fn     run-once main, 0xFFFF = none
//   42  u16 msg_fn       wm message handler fn(msg,w,l), 0xFFFF = none
//   44  u16 click_fn     optional click fn, 0xFFFF = route to msg_fn
//   46  u16 fn_count     <= 32
//   48  u16 str_count    <= 255
//   50  u16 const_count  <= 256
//   52  u32 code_size    <= 16 KiB
//   56  u32 body_sum     additive checksum of bytes [64, size)
//   60  u32 reserved
// then, in order:
//   fn table    fn_count   x { u32 code_off; u8 args; u8 locals; u16 pad }
//   const pool  const_count x u64
//   str pool    str_count  x { u16 len; u8 bytes[len] }
//   code        code_size bytes
//
// App modules (kind=1) get an HxerHeader whose entry/on_click/on_close
// thunks dispatch XAU-style message ids into msg_fn — the .hxer app
// machinery (wm_open, desk_open_app, Start menu) picks them up
// unchanged.

#include "hcpp/types.hpp"

namespace hype::hxbvm {

    constexpr hcpp::u32 hxbc_magic = 0x43425848;
    constexpr hcpp::u16 hxbc_version = 1;
    constexpr hcpp::u32 hxbc_header_size = 64;
    constexpr hcpp::u32 hxbc_max_blob = 32 * 1024;
    constexpr hcpp::u32 hxbc_max_code = 16 * 1024;
    constexpr hcpp::u32 hxbc_max_modules = 4;
    constexpr hcpp::u16 hxbc_fn_none = 0xFFFF;

    constexpr hcpp::u8 hxbc_kind_program = 0;
    constexpr hcpp::u8 hxbc_kind_app = 1;

    // XAU message ids the dispatcher feeds to msg_fn (mirror of
    // kernel/win/xau.h — duplicated so blobs need no XAU include).
    constexpr hcpp::u32 hxbc_msg_create = 1;
    constexpr hcpp::u32 hxbc_msg_destroy = 2;
    constexpr hcpp::u32 hxbc_msg_paint = 3;
    constexpr hcpp::u32 hxbc_msg_close = 4;
    constexpr hcpp::u32 hxbc_msg_lbuttondown = 7;

    // Loads + validates a .hxbc file through the VFS, installs it into a
    // module slot and runs entry_fn once. Prints
    // "hxbvm load=<name> ops=N". Returns the module index or -1.
    hcpp::i32 hxbc_load(const char* path) noexcept;
    // Same, for an in-memory blob (selftest path). The blob is copied.
    hcpp::i32 hxbc_load_blob(const void* blob, hcpp::u32 size) noexcept;

    // Registers module `idx` (kind=app) into the .hxer registry so
    // wm_open/desk_open_app can launch it. Idempotent; prints
    // "hxbc app=<name>" on first success.
    bool hxbc_app_register(hcpp::u32 idx) noexcept;

    // Re-runs entry_fn of a loaded module.
    bool hxbc_run(hcpp::u32 idx) noexcept;
    // Directly dispatches a message into msg_fn (no window ctx).
    bool hxbc_dispatch(hcpp::u32 idx, hcpp::u32 msg, hcpp::u64 w,
                       hcpp::u64 l) noexcept;

    hcpp::u32 hxbc_count() noexcept;
    const char* hxbc_name(hcpp::u32 idx) noexcept;

    // In-memory end-to-end check: assembles two blobs (sum/loop/CALL +
    // heap + fs round-trip program, and trap probes), executes them and
    // verifies results. Prints "hxbvm selftest=ok|fail".
    bool hxbvm_selftest() noexcept;

    // Boot wiring: selftest, seeds U:/apps/hxdemo.hxbc, loads it and
    // registers the app. Called from stage_hxrt after hxrt_boot().
    void hxbvm_boot() noexcept;

}
