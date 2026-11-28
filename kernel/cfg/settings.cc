#include "kernel/cfg/settings.h"

#include "kernel/audio/pullmix.h"
#include "kernel/hxrt/apps/dxbg.h"
#include "kernel/serial.hpp"
#include "kernel/session/idlelock.h"
#include "kernel/time/timer.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/theme.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/win/deskbg.h"
#include "kernel/win/winman.h"

namespace {

    using hype::cfg::settings_key_max;
    using hype::cfg::settings_val_max;

    enum class CfgType : hcpp::u8 { Str, Int, Bool };

    struct CfgKey {
        const char* name;
        CfgType type;
        const char* def;
        hcpp::i32 lo;
        hcpp::i32 hi;
    };

    // Registered keys: name -> type, default, int range. Order here is the
    // serialization order in hype.cfg.
    const CfgKey keys[] = {
        {"audio.volume", CfgType::Int, "100", 0, 100},
        {"ui.brightness", CfgType::Int, "75", 0, 100},
        {"ui.theme", CfgType::Str, "dark", 0, 0},
        {"ui.accent", CfgType::Str, "blue", 0, 0},
        {"ui.hard_shadow", CfgType::Bool, "0", 0, 1},
        {"ui.lock_timeout_min", CfgType::Int, "5", 0, 1440},
        {"desktop.wallpaper", CfgType::Str, "", 0, 0},
        {"ui.vdesk_count", CfgType::Int, "4", 2, 9},
    };
    constexpr hcpp::u32 key_count = sizeof(keys) / sizeof(keys[0]);

    constexpr hcpp::u32 cfg_file_cap = 4096;
    constexpr hcpp::u32 cfg_line_max = settings_key_max + settings_val_max + 2;
    constexpr hcpp::u64 cfg_save_debounce_ms = 500;

    char values[key_count][settings_val_max];
    bool cfg_dirty = false;
    hcpp::u64 last_save_ms = 0;

    hcpp::u32 str_len(const char* s) noexcept {
        hcpp::u32 n = 0;
        while (s[n] != '\0') ++n;
        return n;
    }

    bool str_eq(const char* a, const char* b) noexcept {
        hcpp::u32 i = 0;
        while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
        return a[i] == b[i];
    }

    void str_copy(char* dst, const char* src, hcpp::u32 cap) noexcept {
        hcpp::u32 i = 0;
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
        dst[i] = '\0';
    }

    void reset_defaults() noexcept {
        for (hcpp::u32 i = 0; i < key_count; ++i) {
            str_copy(values[i], keys[i].def, settings_val_max);
        }
    }

    hcpp::i32 key_index(const char* key) noexcept {
        for (hcpp::u32 i = 0; i < key_count; ++i) {
            if (str_eq(keys[i].name, key)) {
                return static_cast<hcpp::i32>(i);
            }
        }
        return -1;
    }

    // Strict decimal parse: optional '-', at least one digit, nothing else.
    bool parse_int(const char* text, hcpp::i32& out) noexcept {
        hcpp::u32 i = 0;
        bool neg = false;
        if (text[i] == '-') {
            neg = true;
            ++i;
        }
        if (text[i] < '0' || text[i] > '9') return false;
        hcpp::i32 v = 0;
        while (text[i] >= '0' && text[i] <= '9') {
            if (v > 1000000) return false;  // bounded: no overflow games
            v = v * 10 + (text[i] - '0');
            ++i;
        }
        if (text[i] != '\0') return false;
        out = neg ? -v : v;
        return true;
    }

    hcpp::u32 uint_text(hcpp::u32 v, char* out, hcpp::u32 cap) noexcept {
        char tmp[12];
        hcpp::u32 m = 0;
        do {
            tmp[m++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        } while (v != 0 && m < sizeof(tmp));
        hcpp::u32 n = 0;
        while (m != 0 && n + 1 < cap) {
            out[n++] = tmp[--m];
        }
        out[n] = '\0';
        return n;
    }

    void int_text(hcpp::i32 v, char* out, hcpp::u32 cap) noexcept {
        if (v < 0 && cap > 1) {
            out[0] = '-';
            uint_text(static_cast<hcpp::u32>(-v), out + 1, cap - 1);
            return;
        }
        uint_text(static_cast<hcpp::u32>(v), out, cap);
    }

    bool is_bool_true(const char* text) noexcept {
        return str_eq(text, "1") || str_eq(text, "true") ||
               str_eq(text, "on") || str_eq(text, "yes");
    }

    bool is_bool_false(const char* text) noexcept {
        return str_eq(text, "0") || str_eq(text, "false") ||
               str_eq(text, "off") || str_eq(text, "no");
    }

    // Wallpaper leaf names: printable filename chars, no separators, no
    // "." / ".." escape names, bounded by the vfs name field.
    bool wallpaper_name_ok(const char* text) noexcept {
        const hcpp::u32 n = str_len(text);
        if (n == 0) return true;  // empty clears the stored wallpaper
        if (str_eq(text, "gradient")) return true;
        if (n >= hype::vfs::fs_name_size) return false;
        if (str_eq(text, ".") || str_eq(text, "..")) return false;
        for (hcpp::u32 i = 0; i < n; ++i) {
            const char c = text[i];
            const bool ok =
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // Canonicalizes `text` into `out` per the key's type. Returns false
    // when the value is rejected outright; ints clamp into range instead.
    bool canonical(const CfgKey& key, const char* text, char* out,
                   hcpp::u32 cap) noexcept {
        switch (key.type) {
            case CfgType::Int: {
                hcpp::i32 v = 0;
                if (!parse_int(text, v)) return false;
                if (v < key.lo) v = key.lo;
                if (v > key.hi) v = key.hi;
                int_text(v, out, cap);
                return true;
            }
            case CfgType::Bool:
                if (is_bool_true(text)) {
                    str_copy(out, "1", cap);
                    return true;
                }
                if (is_bool_false(text)) {
                    str_copy(out, "0", cap);
                    return true;
                }
                return false;
            case CfgType::Str:
                if (str_eq(key.name, "ui.theme")) {
                    if (!str_eq(text, "dark") && !str_eq(text, "light")) {
                        return false;
                    }
                } else if (str_eq(key.name, "ui.accent")) {
                    // Fixed accent palette (issue #177):
                    // blue/violet/emerald/rose.
                    if (!hype::ui::theme_accent_valid(text)) return false;
                } else if (str_eq(key.name, "desktop.wallpaper")) {
                    if (!wallpaper_name_ok(text)) return false;
                } else if (str_len(text) >= settings_val_max) {
                    return false;
                }
                str_copy(out, text, cap);
                return true;
        }
        return false;
    }

    void mark_dirty() noexcept { cfg_dirty = true; }

    bool store(const char* key, const char* text) noexcept {
        const hcpp::i32 idx = key_index(key);
        if (idx < 0) return false;
        char canon[settings_val_max];
        if (!canonical(keys[idx], text, canon, sizeof(canon))) return false;
        if (!str_eq(values[idx], canon)) {
            str_copy(values[idx], canon, settings_val_max);
            mark_dirty();
        }
        return true;
    }

    void serial_kv(const char* key, const char* val) noexcept {
        serial::write("cfg ");
        serial::write(key);
        serial::write("=");
        serial::write_line(val);
    }

}  // namespace

namespace hype::cfg {

    const char* settings_get(const char* key) noexcept {
        const hcpp::i32 idx = key_index(key);
        return idx < 0 ? nullptr : values[idx];
    }

    hcpp::i32 settings_get_int(const char* key) noexcept {
        const char* text = settings_get(key);
        hcpp::i32 v = 0;
        if (text == nullptr || !parse_int(text, v)) return 0;
        return v;
    }

    bool settings_get_bool(const char* key) noexcept {
        const char* text = settings_get(key);
        return text != nullptr && is_bool_true(text);
    }

    bool settings_set(const char* key, const char* value) noexcept {
        if (key == nullptr || value == nullptr) return false;
        return store(key, value);
    }

    bool settings_set_int(const char* key, hcpp::i32 value) noexcept {
        char text[settings_val_max];
        int_text(value, text, sizeof(text));
        return store(key, text);
    }

    bool settings_set_bool(const char* key, bool value) noexcept {
        return store(key, value ? "1" : "0");
    }

    hcpp::u32 settings_count() noexcept { return key_count; }

    const char* settings_key_name(hcpp::u32 index) noexcept {
        return index < key_count ? keys[index].name : nullptr;
    }

    hcpp::u32 settings_load() noexcept {
        reset_defaults();
        cfg_dirty = false;
        static char buf[cfg_file_cap + 1];
        hcpp::u32 got = 0;
        if (hype::vfs::fs_disk_count() == 0 ||
            hype::vfs::fs_read(settings_path, buf, cfg_file_cap, &got) !=
                hype::vfs::FsStatus::Ok ||
            got == 0) {
            serial::write_line("cfg loaded=0");
            return 0;
        }
        buf[got < cfg_file_cap ? got : cfg_file_cap] = '\0';

        // Bounded "key=value" parser: '#' comments, blank lines and
        // over-long lines are skipped; unknown keys are ignored.
        hcpp::u32 applied = 0;
        hcpp::u32 pos = 0;
        while (pos < got) {
            hcpp::u32 end = pos;
            while (end < got && buf[end] != '\n') ++end;
            char line[cfg_line_max];
            hcpp::u32 len = end - pos;
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            for (hcpp::u32 i = 0; i < len; ++i) line[i] = buf[pos + i];
            line[len] = '\0';
            pos = end + 1;

            hcpp::u32 l = 0;
            while (line[l] == ' ' || line[l] == '\t') ++l;
            if (line[l] == '\0' || line[l] == '#') continue;
            hcpp::u32 eq = l;
            while (line[eq] != '\0' && line[eq] != '=') ++eq;
            if (line[eq] != '=') continue;
            // Trim trailing space/CR off key and value in place.
            hcpp::u32 kend = eq;
            while (kend > l &&
                   (line[kend - 1] == ' ' || line[kend - 1] == '\t')) {
                --kend;
            }
            line[kend] = '\0';
            char* val = line + eq + 1;
            while (*val == ' ' || *val == '\t') ++val;
            hcpp::u32 vend = str_len(val);
            while (vend > 0 &&
                   (val[vend - 1] == ' ' || val[vend - 1] == '\t' ||
                    val[vend - 1] == '\r')) {
                val[--vend] = '\0';
            }
            if (line[l] == '\0' || !store(line + l, val)) continue;
            serial_kv(line + l, val);
            ++applied;
        }
        serial::write("cfg loaded=");
        serial::write_dec32(applied);
        serial::write("\r\n");
        cfg_dirty = false;  // parsed state is already on disk
        return applied;
    }

    bool settings_save() noexcept {
        if (hype::vfs::fs_disk_count() == 0) {
            cfg_dirty = false;
            serial::write_line("cfg save=fail");
            return false;
        }
        static char out[key_count * (settings_key_max + settings_val_max) + 32];
        hcpp::u32 n = 0;
        const char header[] = "# hype.cfg - HypeAI OS settings\n";
        for (hcpp::u32 i = 0; header[i] != '\0' && n + 1 < sizeof(out); ++i) {
            out[n++] = header[i];
        }
        for (hcpp::u32 i = 0; i < key_count; ++i) {
            for (const char* p = keys[i].name; *p != '\0' && n + 1 < sizeof(out);
                 ++p) {
                out[n++] = *p;
            }
            if (n + 1 < sizeof(out)) out[n++] = '=';
            for (const char* p = values[i]; *p != '\0' && n + 1 < sizeof(out);
                 ++p) {
                out[n++] = *p;
            }
            if (n + 1 < sizeof(out)) out[n++] = '\n';
        }
        out[n] = '\0';
        const bool ok =
            hype::vfs::fs_write(settings_path, out, n) ==
            hype::vfs::FsStatus::Ok;
        last_save_ms = hype::time::uptime_ms();
        if (ok) cfg_dirty = false;
        serial::write("cfg save=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

    void settings_request_save() noexcept {
        if (!cfg_dirty) return;
        const hcpp::u64 now = hype::time::uptime_ms();
        if (now - last_save_ms < cfg_save_debounce_ms) return;
        settings_save();
    }

    void settings_pump() noexcept {
        if (!cfg_dirty) return;
        const hcpp::u64 now = hype::time::uptime_ms();
        if (now - last_save_ms < cfg_save_debounce_ms) return;
        settings_save();
    }

    bool settings_apply(const char* key) noexcept {
        if (str_eq(key, "audio.volume")) {
            const hcpp::i32 pct = settings_get_int("audio.volume");
            // pullmix master gain is Q8: 256 == unity (100%).
            hype::audio::pullmix_set_master_gain(
                static_cast<hcpp::u16>(pct * 256 / 100));
            hype::ui::deskbar_set_volume(pct);
            return true;
        }
        if (str_eq(key, "ui.brightness")) {
            hype::ui::deskbar_set_brightness(
                settings_get_int("ui.brightness"));
            return true;
        }
        if (str_eq(key, "ui.theme")) {
            hype::ui::theme_set(str_eq(settings_get("ui.theme"), "light")
                                    ? hype::ui::theme_light()
                                    : hype::ui::theme_dark());
            hype::win::wm_invalidate_all();
            return true;
        }
        if (str_eq(key, "ui.accent")) {
            // Accent palette (issue #177): rewrites the accent_*/ring
            // tokens of the live theme; prints "theme accent=<name>".
            hype::ui::theme_set_accent(settings_get("ui.accent"));
            hype::win::wm_invalidate_all();
            return true;
        }
        if (str_eq(key, "ui.hard_shadow")) {
            hype::ui::theme_set_hard_shadow(
                settings_get_bool("ui.hard_shadow"));
            hype::win::wm_invalidate_all();
            return true;
        }
        if (str_eq(key, "ui.lock_timeout_min")) {
            // Idle-timeout auto-lock (issue #134): pushes the minute
            // value into the idlelock armed cache and re-stamps the
            // activity clock (prints "idlelock arm=<min>m").
            return hype::session::idle_lock_apply();
        }
        if (str_eq(key, "ui.vdesk_count")) {
            // Virtual desktops (issue #135): resize the workspace count;
            // windows on dropped desktops merge into the last one.
            hype::win::vdesk_set_count(
                static_cast<hcpp::u32>(
                    settings_get_int("ui.vdesk_count")));
            return true;
        }
        if (str_eq(key, "desktop.wallpaper")) {
            const char* name = settings_get("desktop.wallpaper");
            if (name == nullptr || name[0] == '\0') return true;
            if (str_eq(name, "gradient")) {
                hype::win::desk_bg_reset();
                return true;
            }
            char path[hype::vfs::hypepath_max_length];
            hcpp::u32 n = 0;
            const char dir[] = "HYPEOS://U/wallpapers/";
            for (hcpp::u32 i = 0; dir[i] != '\0' && n + 1 < sizeof(path);
                 ++i) {
                path[n++] = dir[i];
            }
            for (hcpp::u32 i = 0; name[i] != '\0' && n + 1 < sizeof(path);
                 ++i) {
                path[n++] = name[i];
            }
            path[n] = '\0';
            return hype::hxrt::dxbg_apply(path);
        }
        return false;
    }

    void settings_apply_all() noexcept {
        for (hcpp::u32 i = 0; i < key_count; ++i) {
            if (str_eq(keys[i].name, "desktop.wallpaper")) continue;
            settings_apply(keys[i].name);
        }
    }

    void settings_apply_wallpaper() noexcept {
        const char* name = settings_get("desktop.wallpaper");
        if (name == nullptr || name[0] == '\0') return;
        if (!settings_apply("desktop.wallpaper")) {
            serial::write_line("cfg wallpaper=fail");
        }
    }

}  // namespace hype::cfg
