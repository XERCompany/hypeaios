#pragma once

// Persistent settings store (issue #130). A small typed key/value table
// with registered defaults, serialized to HYPEOS://U/hype.cfg as plain
// "key=value" lines. The U: memfs is write-through to the HYPEDATA blob,
// so a settings save reaches disk through the same debounced
// hypeblob_sync() pipeline every other file uses — values survive reboot.
//
// Consumers:
//   audio.volume      0..100   QS volume slider -> pullmix master gain
//   ui.brightness     0..100   QS brightness slider (stub tunable)
//   ui.theme          dark|light -> theme_set(theme_dark()/theme_light())
//   ui.hard_shadow    bool      -> theme_set_hard_shadow()
//   ui.lock_timeout_min 0..1440 -> idlelock auto-lock minutes (0 = off)
//   desktop.wallpaper leaf name in U:/wallpapers, "gradient" or "" clears
//
// Boot flow: stage_vfs calls settings_load() once the hypeblob mount has
// repopulated U:, then settings_apply_all() pushes every restored value
// into its consumer. The wallpaper is deferred to
// settings_apply_wallpaper() in stage_hxrt because the dxbg decoder needs
// the seeded wallpaper pack (first boot) and a PMM arena.
//
// Serial markers: "cfg loaded=N" (keys applied), "cfg <key>=<val>" per
// restored key, "cfg save=ok|fail" on flush.

#include "hcpp/types.hpp"

namespace hype::cfg {

    constexpr hcpp::u32 settings_key_max = 32;   // bytes incl. NUL
    constexpr hcpp::u32 settings_val_max = 64;   // bytes incl. NUL
    constexpr const char* settings_path = "HYPEOS://U/hype.cfg";

    const char* settings_get(const char* key) noexcept;
    hcpp::i32 settings_get_int(const char* key) noexcept;
    bool settings_get_bool(const char* key) noexcept;

    // Validation per registered type: ints clamp to their range, bools
    // take 1/0/true/false/on/off/yes/no, enum strings (ui.theme) take
    // their listed tokens, wallpaper takes a bounded leaf name.
    // Returns false for unknown keys and rejected values.
    bool settings_set(const char* key, const char* value) noexcept;
    bool settings_set_int(const char* key, hcpp::i32 value) noexcept;
    bool settings_set_bool(const char* key, bool value) noexcept;

    hcpp::u32 settings_count() noexcept;
    const char* settings_key_name(hcpp::u32 index) noexcept;

    // Parses settings_path into the store. Prints "cfg <key>=<val>" per
    // applied line and "cfg loaded=N" at the end (N=0 when the file is
    // absent or the disk is missing). Returns the applied-key count.
    hcpp::u32 settings_load() noexcept;

    // Serializes the store back to settings_path. Prints
    // "cfg save=ok|fail". The fs write-through debounce pushes the memfs
    // to the blob afterwards, so this returns after the memfs write.
    bool settings_save() noexcept;

    // Debounced save for interactive callers (slider drags, shell `set`):
    // flushes immediately when the window elapsed, otherwise leaves the
    // store dirty for settings_pump() in the idle loop.
    void settings_request_save() noexcept;
    void settings_pump() noexcept;

    // Pushes one stored value into its consumer (mixer gain, deskbar
    // slider state, theme slot, wallpaper decode). Returns false for
    // unknown keys or applies that failed (e.g. missing wallpaper file).
    bool settings_apply(const char* key) noexcept;

    // Boot-time apply of every registered key except the wallpaper (the
    // dxbg decoder and seeded pack are not up until stage_hxrt).
    void settings_apply_all() noexcept;
    void settings_apply_wallpaper() noexcept;

}
