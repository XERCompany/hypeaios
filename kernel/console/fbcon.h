#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

// Framebuffer console (issue #139): a serial-like text console rendered
// straight into the VBE linear framebuffer. It exists so boot output is
// visible on screen before the UI shell starts, and so a full-screen
// kernel log can be toggled at runtime with Ctrl+Alt+F1.
//
// Bytes reach the console through a parallel tap in
// serial::write_char(): every serial byte is appended to the console's
// bounded scrollback and rendered only while the console is visible.
// Cost while hidden is one ring append per byte — no drawing.
//
// The console never owns the screen by itself: fbcon_show()/the key
// chord flip ownership, and the main loop suppresses shell paint and
// the pointer sprite while fbcon_active() is true. Hiding invalidates
// the whole WM so the shell repaints cleanly.
namespace hype::console {

    // Bind the scanout and the default bitmap font; records geometry,
    // drains the kring tail into the scrollback so early boot markers
    // are already present, and prints "fbcon init=ok WxH". Draws
    // nothing — call fbcon_show() to take over the screen.
    void fbcon_init(hcpp::graphics::Framebuffer& fb) noexcept;

    bool fbcon_ready() noexcept;

    // Show/hide the console view. Applies immediately; only call from
    // main-loop context (the key hook uses fbcon_toggle() + fbcon_poll()
    // so repaints never run inside IRQ dispatch).
    void fbcon_show(bool visible) noexcept;

    // Flag-only toggle for the key hook; applied by fbcon_poll().
    void fbcon_toggle() noexcept;

    // True while the console owns the scanout (shell must not paint).
    bool fbcon_active() noexcept;

    // Serial tap: append one byte to the scrollback; draws it only when
    // the console is visible and viewing the live tail. Bounded, no
    // locks, safe from any context fb writes are safe from.
    void fbcon_putc(char value) noexcept;
    void fbcon_write(hcpp::StringView text) noexcept;

    // Apply pending toggle/scrollback requests and the "lock veil wins"
    // rule; call once per idle pass.
    void fbcon_poll() noexcept;

    // Subscribe the Ctrl+Alt+F1 chord (and PageUp/PageDown scrollback
    // paging while the console is up). Must run after
    // input::initialize_key_emitter().
    bool fbcon_bind_keys() noexcept;

}  // namespace hype::console
