#pragma once

// System clipboard — text store + history ring (issue #133).
//
// A CF_TEXT-style in-memory clipboard: clip_set() pushes a new entry
// (<= 4 KB) onto a 16-deep history; the Win+V flyout in deskbar.cc
// renders the ring and pastes a picked row. Nothing is persisted —
// Windows clears the clipboard on reboot too.
//
// Chord dispatch lives in winman's key hook: Ctrl+C/X/V reaches
// clip_copy/cut/paste, which forwards XAU_MSG_COPY/CUT/PASTE to the
// focused XAU window's WndProc. Apps own what "selection" means:
// hxshell copies its input line, hxfiles copies the selected paths.

#include "hcpp/types.hpp"

namespace hype::ui {

    constexpr hcpp::u32 clip_entry_max = 4096;   // bytes per entry
    constexpr hcpp::u32 clip_history_max = 16;   // Win+V ring depth

    // Pushes `text` (len bytes) as the newest entry, truncating to
    // clip_entry_max. Consecutive identical pushes collapse into one
    // entry. Emits the "clip set=<len>" serial marker.
    void clip_set(const char* text, hcpp::u32 len) noexcept;

    // Newest entry text (nullptr when the clipboard is empty); `len`
    // receives its byte length.
    const char* clip_get(hcpp::u32& len) noexcept;

    // History row `index` (0 = newest); nullptr when out of range.
    const char* clip_history(hcpp::u32 index, hcpp::u32& len) noexcept;

    hcpp::u32 clip_count() noexcept;

    // Chord entry points, called from winman's scancode hook: deliver
    // the matching XAU_MSG_* to the focused XAU window. clip_paste
    // emits "clip paste=<len>" when a focused window takes the text.
    void clip_copy() noexcept;
    void clip_cut() noexcept;
    void clip_paste() noexcept;

    // Win+V pick: promotes history row `index` to the top of the ring
    // (real clipboard-history behavior) and pastes it into the focused
    // window. Returns false when the row does not exist.
    bool clip_paste_at(hcpp::u32 index) noexcept;

}
