#pragma once

// Idle-timeout auto-lock (issue #134). Tracks the timestamp of the last
// real user activity — keyboard scancodes (PS/2 IRQ1 + xHCI HID both
// decode through scancode_decode) and pointer packets (PS/2 + USB mouse
// through cursor_poll) — and drops the session lock veil once
// ui.lock_timeout_min minutes pass without any. Synthetic emitters
// (gdxf selftest emit_key_event, input_device_report_key) bypass the
// pet points, so scripted input cannot keep the desktop awake.
//
// ui.lock_timeout_min: int minutes, 0 disables the auto-lock.
//
// Serial markers: "idlelock arm=<min>m" when the armed timeout is
// (re)applied, "idle lock=ok" when the pump actually locks.

#include "hcpp/types.hpp"

namespace hype::session {

    // Stamps "now" as the last-activity time. Called from real input
    // choke points only: scancode_decode (IRQ + USB HID) and the
    // cursor_poll packet loops — IRQ-context safe (TSC/HPET reads).
    void idle_lock_pet() noexcept;

    // Idle-loop hook (main.cc): locks the session once
    // ui.lock_timeout_min minutes elapsed since the last pet. No-op
    // unless the session is LoggedIn (greeter, registering and an
    // already-locked session never trigger it).
    void idle_lock_pump() noexcept;

    // Re-reads ui.lock_timeout_min into the armed cache; emits
    // "idlelock arm=<min>m" on change and treats the apply itself as
    // activity so a shortened timeout does not slam the veil. Called
    // from settings_apply (boot apply_all + `set` in the shell).
    bool idle_lock_apply() noexcept;

    // Resets the idle clock outright — used on login/unlock so the
    // veil cannot drop again instantly.
    void idle_lock_reset() noexcept;

}
