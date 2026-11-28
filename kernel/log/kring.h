#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

// klog ring (issue #119, docs/research/gaps-kernel.md 6.1): a fixed
// 64 KiB in-memory log of the kernel's serial/klog output, kept so a
// panic can persist the recent log for post-mortem debugging.
// Everything lives in static storage — no heap, no locks — so writes
// are safe from early boot, IRQ handlers and the panic path itself.
// Best-effort under SMP: a torn byte/index is acceptable for a debug
// ring and never blocks the writer.
//
// Two taps feed the ring:
//   * serial.cc write_char() -> kring_putc(): the raw byte stream, so
//     every "marker=ok" line and the panic register dump land here even
//     though they bypass klog entirely.
//   * klog.h log() -> kring_line(): the same line in canonical
//     "<L> [tag] message" form. klog wraps its serial emission in
//     kring_tap_begin()/kring_tap_end() so the raw tap skips those
//     bytes and the line is recorded exactly once. The revsvc
//     set_sink() slot stays free — kring never touches it.
namespace kring {

    constexpr hcpp::size_t kring_capacity = 64u * 1024u;

    // Raw-byte tap called from serial::write_char(). Dropped while a
    // kring_tap_begin()/kring_tap_end() window is open.
    void kring_putc(char value) noexcept;
    void kring_tap_begin() noexcept;
    void kring_tap_end() noexcept;

    // Permanent internal tap called from klog::log(): stores the line
    // as "<L> [tag] message\n" where L is D/I/W/E for level 0..3.
    void kring_line(hcpp::u8 level, hcpp::StringView tag,
                    hcpp::StringView message) noexcept;

    // Appends `line` verbatim, adding a trailing '\n' when missing.
    void kring_write(hcpp::StringView line) noexcept;

    // "== <tag> ==" section separator inside the ring (e.g. the panic
    // path stamping where the crash record starts).
    void kring_mark(hcpp::StringView tag) noexcept;

    hcpp::u32 kring_size() noexcept;     // bytes currently retained
    hcpp::u64 kring_written() noexcept;  // bytes ever appended

    using KringChunk = void (*)(const void* data, hcpp::u32 size,
                                void* ctx) noexcept;

    // Streams the retained bytes oldest->newest through `cb` (at most
    // two chunks; a wrapped ring splits at the buffer end). When the
    // ring has wrapped, a partial oldest line is skipped so dumps begin
    // at a line boundary.
    void kring_copy_to(KringChunk cb, void* ctx) noexcept;

    // Same content copied into out_buf, truncated at `capacity`.
    // Returns the number of bytes written.
    hcpp::u32 kring_dump(char* out_buf, hcpp::u32 capacity) noexcept;

}  // namespace kring
