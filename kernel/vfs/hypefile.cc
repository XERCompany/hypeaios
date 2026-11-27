#include "kernel/vfs/hypefile.h"

#include "hcpp/memory.hpp"
#include "kernel/hxrt/hxpack.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"
#include "kernel/vfs/hypeblob.h"
#include "kernel/vfs/hypefs.h"

namespace {

    using hype::hxrt::HxerHeader;
    using hype::hxrt::HxerKind;

    constexpr hcpp::u32 hypefile_max_blob = 256;
    constexpr hcpp::u32 hypefile_list_cap = 64;

    hype::hxrt::HxerHeader loaded_apps[hype::hxrt::hxrt_max_apps];
    hcpp::u32 loaded_count = 0;
    bool selftest_exec_flag = false;

    hcpp::u32 text_len(const char* text) noexcept {
        hcpp::u32 i = 0;
        while (text != nullptr && text[i] != '\0') {
            ++i;
        }
        return i;
    }

    void text_copy(char* dest, const char* source, hcpp::u32 limit) noexcept {
        hcpp::u32 i = 0;
        while (source[i] != '\0' && i + 1 < limit) {
            dest[i] = source[i];
            ++i;
        }
        dest[i] = '\0';
    }

    bool text_equal(const char* left, const char* right) noexcept {
        hcpp::u32 i = 0;
        while (left[i] != '\0' && right[i] != '\0') {
            if (left[i] != right[i]) return false;
            ++i;
        }
        return left[i] == right[i];
    }

    bool text_has_suffix(const char* text, const char* suffix) noexcept {
        const hcpp::u32 text_size = text_len(text);
        const hcpp::u32 suffix_size = text_len(suffix);
        if (suffix_size == 0 || text_size < suffix_size) return false;
        for (hcpp::u32 i = 0; i < suffix_size; ++i) {
            if (text[text_size - suffix_size + i] != suffix[i]) return false;
        }
        return true;
    }

    hcpp::u32 build_app_path(char* out, hcpp::u32 capacity, char disk,
                             const char* leaf) noexcept {
        const char prefix[] = "HYPEOS://";
        const char dir[] = "/apps/";
        hcpp::u32 used = 0;
        const auto emit = [&out, capacity, &used](char value) noexcept -> bool {
            if (used + 1 >= capacity) return false;
            out[used] = value;
            ++used;
            return true;
        };
        for (hcpp::u32 i = 0; prefix[i] != '\0'; ++i) {
            if (!emit(prefix[i])) return 0;
        }
        if (!emit(disk)) return 0;
        for (hcpp::u32 i = 0; dir[i] != '\0'; ++i) {
            if (!emit(dir[i])) return 0;
        }
        for (hcpp::u32 i = 0; leaf[i] != '\0'; ++i) {
            if (!emit(leaf[i])) return 0;
        }
        out[used] = '\0';
        return used;
    }

    void selftest_entry(hype::hxrt::AppCtx& ctx) noexcept {
        (void)ctx;
        selftest_exec_flag = true;
    }

    const HxerHeader selftest_app = {
        hype::hxrt::hxer_magic, 1,
        static_cast<hcpp::u8>(HxerKind::User), 0,
        "hxf_probe", "HYPEOS://U/apps/hxf_probe.hxer",
        selftest_entry, nullptr};

}

namespace hype::vfs {

    bool hypefile_install(const hype::hxrt::HxerHeader* header) noexcept {
        if (header == nullptr || header->name[0] == '\0') {
            return false;
        }
        const bool system_app =
            header->kind == static_cast<hcpp::u8>(
                                hype::hxrt::HxerKind::System);
        hcpp::u8 blob[hype::hxrt::hxpack_blob_size];
        const hcpp::u32 blob_size =
            hype::hxrt::hxpack_encode(header, blob, sizeof(blob));
        if (blob_size == 0) {
            return false;
        }
        if (!system_app) {
            const FsStatus made = fs_mkdir("HYPEOS://U/apps");
            if (made != FsStatus::Ok && made != FsStatus::Exists) {
                return false;
            }
        }
        char leaf[hype::vfs::fs_name_size];
        hcpp::u32 used = 0;
        for (hcpp::u32 i = 0; header->name[i] != '\0'; ++i) {
            if (used + 1 >= sizeof(leaf)) return false;
            leaf[used] = header->name[i];
            ++used;
        }
        const char* ext = system_app ? ".hxersys" : ".hxer";
        for (hcpp::u32 i = 0; ext[i] != '\0'; ++i) {
            if (used + 1 >= sizeof(leaf)) return false;
            leaf[used] = ext[i];
            ++used;
        }
        leaf[used] = '\0';
        char path[hype::vfs::hypepath_max_length];
        if (build_app_path(path, sizeof(path), system_app ? 'S' : 'U',
                           leaf) == 0) {
            return false;
        }
        const bool written = fs_write(path, blob, blob_size) == FsStatus::Ok;
        if (written) {
            // Persist the new .hxer pack to the HYPEDATA blob. No-op until
            // the blob has been mounted, so boot-time installs are cheap.
            hypeblob_sync();
        }
        return written;
    }

    bool hypefile_load(const char* path) noexcept {
        hype::vfs::FsStat info{};
        if (fs_stat(path, info) != FsStatus::Ok ||
            (info.flags & FS_DIR) != 0 ||
            info.size < hype::hxrt::hxpack_blob_size ||
            info.size > hypefile_max_blob) {
            return false;
        }
        static hcpp::u8 blob[hypefile_max_blob];
        hcpp::u32 size = 0;
        if (fs_read(path, blob, sizeof(blob), &size) != FsStatus::Ok ||
            size != info.size) {
            return false;
        }
        HxerHeader decoded{};
        if (!hype::hxrt::hxpack_decode(blob, size, decoded)) {
            return false;
        }
        if (hype::hxrt::hxrt_find(decoded.name) != nullptr) {
            return true;
        }
        if (loaded_count >= hype::hxrt::hxrt_max_apps) {
            return false;
        }
        text_copy(decoded.path, path, sizeof(decoded.path));
        loaded_apps[loaded_count] = decoded;
        if (!hype::hxrt::hxrt_register(&loaded_apps[loaded_count])) {
            return false;
        }
        ++loaded_count;
        return true;
    }

    hcpp::u32 hypefile_sync_dir(char disk) noexcept {
        static hype::vfs::FsStat listing[hypefile_list_cap];
        char dir_path[24];
        const char prefix[] = "HYPEOS://";
        const char dir[] = "/apps";
        hcpp::u32 pos = 0;
        for (hcpp::u32 i = 0; prefix[i] != '\0'; ++i) dir_path[pos++] = prefix[i];
        dir_path[pos++] = disk;
        for (hcpp::u32 i = 0; dir[i] != '\0'; ++i) dir_path[pos++] = dir[i];
        dir_path[pos] = '\0';
        const hcpp::u32 count =
            fs_list(dir_path, listing, hypefile_list_cap);
        const hcpp::u32 bound =
            count < hypefile_list_cap ? count : hypefile_list_cap;
        hcpp::u32 loaded = 0;
        for (hcpp::u32 i = 0; i < bound; ++i) {
            if ((listing[i].flags & FS_DIR) != 0) continue;
            if (!text_has_suffix(listing[i].name, ".hxersys") &&
                !text_has_suffix(listing[i].name, ".hxer")) {
                continue;
            }
            char path[hype::vfs::hypepath_max_length];
            if (build_app_path(path, sizeof(path), disk, listing[i].name) ==
                0) {
                continue;
            }
            if (hypefile_load(path)) {
                ++loaded;
            }
        }
        return loaded;
    }

    hcpp::u32 hypefile_sync() noexcept {
        // S:/apps holds system headers installed at boot; U:/apps holds user
        // apps plus the app-pack copies shipped in the HYPEDATA blob.
        return hypefile_sync_dir('S') + hypefile_sync_dir('U');
    }

    bool hypefile_selftest(HypefileSelftest& out) noexcept {
        out = {};

        hcpp::u8 blob[hype::hxrt::hxpack_blob_size];
        const hcpp::u32 packed =
            hype::hxrt::hxpack_encode(&selftest_app, blob, sizeof(blob));
        HxerHeader decoded{};
        out.pack_ok =
            packed == hype::hxrt::hxpack_blob_size &&
            hype::hxrt::hxpack_validate(blob, packed) &&
            hype::hxrt::hxpack_decode(blob, packed, decoded) &&
            decoded.magic == hype::hxrt::hxer_magic &&
            decoded.kind == selftest_app.kind &&
            text_equal(decoded.name, selftest_app.name) &&
            decoded.entry == selftest_app.entry;
        serial::write("hxer pack=");
        serial::write_line(out.pack_ok ? "ok" : "fail");

        bool file_ok = hypefile_install(&selftest_app);
        if (file_ok) {
            hype::vfs::FsStat info{};
            file_ok =
                fs_stat("HYPEOS://U/apps/hxf_probe.hxer", info) ==
                    FsStatus::Ok &&
                info.size == hype::hxrt::hxpack_blob_size;
        }
        if (file_ok) {
            file_ok = hypefile_load("HYPEOS://U/apps/hxf_probe.hxer");
        }
        if (file_ok) {
            selftest_exec_flag = false;
            hype::hxrt::AppCtx ctx{};
            file_ok = hype::hxrt::hxrt_exec("hxf_probe", ctx) &&
                      selftest_exec_flag;
        }
        out.file_ok = file_ok;
        serial::write("hxer file=");
        serial::write_line(out.file_ok ? "ok" : "fail");

        out.guard_ok =
            fs_delete("HYPEOS://S/apps/HxSettings.hxersys") ==
                FsStatus::AccessDenied &&
            fs_delete("HYPEOS://U/apps/hxf_probe.hxer") == FsStatus::Ok;
        serial::write("hxer guard=");
        serial::write_line(out.guard_ok ? "ok" : "fail");

        hype::hxrt::hxrt_unregister("hxf_probe");
        // The install above synced hxf_probe into the blob; rewrite it now
        // that the probe file is deleted so no ghost entry persists.
        hypeblob_sync();
        return out.pack_ok && out.file_ok && out.guard_ok;
    }

}
