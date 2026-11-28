#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

// Screenshots (issue #131): PrtSc captures the composited desktop to an
// uncompressed 24-bit BMP under HYPEOS://U/shots/shotNNN.bmp.
//
// The capture source is the XDP compositor surface when the render
// manager is live — it already holds the final frame (wallpaper, icons,
// windows, deskbar) that xdp_present() flips to scanout, minus the
// cursor sprite which is drawn straight into the LFB. Without XDP the
// bound scanout framebuffer is read back through get_pixel/decode_rgb
// so arbitrary VBE channel layouts still encode correctly.
//
// Keyboard delivery: the PS/2/USB decode layer folds the set-1 PrtSc
// sequence into input::key_prtsc; the WM chord maps PrtSc to a full
// capture and Ctrl/Alt+PrtSc to the focused window. The key handler only
// sets a flag — the ~2.4 MB encode+write is far too heavy for IRQ
// context, so screenshot_pump() performs it on the next desktop-loop
// pass (and the compositor surface still holds the frame that was on
// screen at press time).
namespace hype::ui {

    // Synchronous capture entry points (also usable by a future deskbar
    // button). screenshot_window() clips `rect` to the capture surface.
    // Both return false and log "shot fail=<why>" on any failure.
    bool screenshot_full() noexcept;
    bool screenshot_window(Rect rect) noexcept;

    // Request a capture from an input/IRQ context. `window` selects the
    // focused-window capture (falls back to full when nothing is
    // focused). Coalesced: repeated presses before the pump runs count
    // once, with a window request winning over a full one.
    void screenshot_request(bool window) noexcept;

    // Deferred worker — call once per desktop-loop pass while the shell
    // is active. No-op when nothing is pending or the session is locked.
    void screenshot_pump() noexcept;

}
