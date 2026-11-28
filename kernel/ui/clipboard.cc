// System clipboard — text store + history ring (issue #133).
//
// Linear history: entries[0] is the newest clip; clip_set() shifts the
// rest one slot down and drops the tail at clip_history_max. Fixed
// static cost of 16 x 4 KB.
//
// Paste delivery goes through the focused XAU window's WndProc —
// the clipboard never pokes app state directly; apps pull the text
// back out through clip_get()/clip_history() while handling
// XAU_MSG_COPY/CUT/PASTE.

#include "kernel/ui/clipboard.h"

#include "kernel/serial.hpp"
#include "kernel/win/xau.h"

namespace hype::ui {

    namespace {

        struct ClipEntry {
            char text[clip_entry_max + 1]{};
            hcpp::u32 len = 0;
        };

        ClipEntry entries[clip_history_max];
        hcpp::u32 count = 0;

    }

    void clip_set(const char* text, hcpp::u32 len) noexcept {
        if (text == nullptr || len == 0) {
            return;
        }
        if (len > clip_entry_max) {
            len = clip_entry_max;
        }
        // Collapse an identical consecutive copy: re-copying the same
        // selection must not grow the history with a duplicate.
        if (count != 0 && entries[0].len == len) {
            hcpp::u32 i = 0;
            while (i < len && entries[0].text[i] == text[i]) {
                ++i;
            }
            if (i == len) {
                return;
            }
        }
        const hcpp::u32 keep =
            count < clip_history_max ? count : clip_history_max - 1;
        for (hcpp::u32 i = keep; i > 0; --i) {
            entries[i] = entries[i - 1];
        }
        for (hcpp::u32 i = 0; i < len; ++i) {
            entries[0].text[i] = text[i];
        }
        entries[0].text[len] = '\0';
        entries[0].len = len;
        if (count < clip_history_max) {
            ++count;
        }
        serial::write("clip set=");
        serial::write_dec32(len);
        serial::write("\r\n");
    }

    const char* clip_get(hcpp::u32& len) noexcept {
        return clip_history(0, len);
    }

    const char* clip_history(hcpp::u32 index, hcpp::u32& len) noexcept {
        len = 0;
        if (index >= count) {
            return nullptr;
        }
        len = entries[index].len;
        return entries[index].text;
    }

    hcpp::u32 clip_count() noexcept { return count; }

    void clip_copy() noexcept {
        const hype::win::XAU w = hype::win::xau_focused();
        if (w == hype::win::XAU_INVALID) {
            return;
        }
        hype::win::xau_send(w, hype::win::XAU_MSG_COPY, 0, 0);
    }

    void clip_cut() noexcept {
        const hype::win::XAU w = hype::win::xau_focused();
        if (w == hype::win::XAU_INVALID) {
            return;
        }
        hype::win::xau_send(w, hype::win::XAU_MSG_CUT, 0, 0);
    }

    void clip_paste() noexcept {
        if (count == 0) {
            return;
        }
        const hype::win::XAU w = hype::win::xau_focused();
        if (w == hype::win::XAU_INVALID) {
            return;
        }
        serial::write("clip paste=");
        serial::write_dec32(entries[0].len);
        serial::write("\r\n");
        hype::win::xau_send(w, hype::win::XAU_MSG_PASTE, 0, 0);
    }

    bool clip_paste_at(hcpp::u32 index) noexcept {
        if (index >= count) {
            return false;
        }
        if (index != 0) {
            const ClipEntry pick = entries[index];
            for (hcpp::u32 i = index; i > 0; --i) {
                entries[i] = entries[i - 1];
            }
            entries[0] = pick;
        }
        clip_paste();
        return true;
    }

}
