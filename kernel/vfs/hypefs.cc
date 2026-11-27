#include "kernel/vfs/hypefs.h"

#include "hcpp/memory.hpp"
#include "kernel/codecapps/arena.h"
#include "kernel/memory/pmm.h"

namespace {

    constexpr hcpp::u32 disk_count_max = 3;
    constexpr hcpp::size_t disk_pages[disk_count_max] = {
        hype::vfs::fs_disk_s_bytes / hype::memory::page_size,
        hype::vfs::fs_disk_u_bytes / hype::memory::page_size,
        hype::vfs::fs_disk_t_bytes / hype::memory::page_size,
    };
    constexpr char disk_letters[disk_count_max] = {'S', 'U', 'T'};
    constexpr hcpp::u16 no_entry = 0xFFFF;

    struct FsEntry {
        char name[hype::vfs::fs_name_size];
        hcpp::u32 flags;
        hcpp::u32 size;
        hcpp::u32 offset;
        hcpp::u16 parent;
        char disk;
        hcpp::u8 in_use;
    };

    FsEntry entries[hype::vfs::fs_max_entries];
    hype::codec::Arena arenas[disk_count_max];
    hcpp::u16 disk_roots[disk_count_max];
    hcpp::u32 disks_ready = 0;

    hcpp::u32 disk_index(char disk) noexcept {
        switch (disk) {
            case 'S': return 0;
            case 'U': return 1;
            case 'T': return 2;
            default: return disk_count_max;
        }
    }

    bool name_equal(const char* left, const char* right) noexcept {
        hcpp::u32 i = 0;
        while (left[i] != '\0' && right[i] != '\0') {
            if (left[i] != right[i]) return false;
            ++i;
        }
        return left[i] == right[i];
    }

    void name_copy(char* dest, const char* source) noexcept {
        hcpp::u32 i = 0;
        while (source[i] != '\0' && i + 1 < hype::vfs::fs_name_size) {
            dest[i] = source[i];
            ++i;
        }
        dest[i] = '\0';
    }

    hcpp::u16 alloc_entry() noexcept {
        for (hcpp::u32 i = 0; i < hype::vfs::fs_max_entries; ++i) {
            if (entries[i].in_use) continue;
            entries[i] = {};
            entries[i].in_use = 1;
            entries[i].parent = no_entry;
            return static_cast<hcpp::u16>(i);
        }
        return no_entry;
    }

    hcpp::u16 find_child(hcpp::u16 dir, char disk, const char* name) noexcept {
        for (hcpp::u32 i = 0; i < hype::vfs::fs_max_entries; ++i) {
            if (entries[i].in_use && entries[i].disk == disk &&
                entries[i].parent == dir && name_equal(entries[i].name, name)) {
                return static_cast<hcpp::u16>(i);
            }
        }
        return no_entry;
    }

    bool dir_has_children(hcpp::u16 dir, char disk) noexcept {
        for (hcpp::u32 i = 0; i < hype::vfs::fs_max_entries; ++i) {
            if (entries[i].in_use && entries[i].disk == disk &&
                entries[i].parent == dir) {
                return true;
            }
        }
        return false;
    }

    hcpp::u16 resolve_dir(const hype::vfs::HypePath& path, hcpp::u32 depth) noexcept {
        const hcpp::u32 di = disk_index(path.disk);
        if (di >= disk_count_max) return no_entry;
        hcpp::u16 current = disk_roots[di];
        for (hcpp::u32 d = 0; d < depth; ++d) {
            current = find_child(current, path.disk, path.components[d]);
            if (current == no_entry) return no_entry;
            if ((entries[current].flags & hype::vfs::FS_DIR) == 0) return no_entry;
        }
        return current;
    }

    hcpp::u16 resolve_entry(const hype::vfs::HypePath& path) noexcept {
        const hcpp::u16 dir =
            resolve_dir(path, path.depth == 0 ? 0 : path.depth - 1);
        if (dir == no_entry) return no_entry;
        if (path.depth == 0) return dir;
        return find_child(dir, path.disk, path.components[path.depth - 1]);
    }

}

namespace hype::vfs {

    void fs_init() noexcept {
        hcpp::set(entries, 0, sizeof(entries));
        disks_ready = 0;
        for (hcpp::u32 d = 0; d < disk_count_max; ++d) {
            arenas[d] = {};
            disk_roots[d] = no_entry;
            if (!hype::codec::arena_from_pmm(arenas[d], disk_pages[d])) continue;
            const hcpp::u16 root = alloc_entry();
            if (root == no_entry) {
                hype::codec::arena_release_pmm(arenas[d], disk_pages[d]);
                continue;
            }
            entries[root].flags = FS_DIR;
            entries[root].disk = disk_letters[d];
            disk_roots[d] = root;
            ++disks_ready;
        }
    }

    hcpp::u32 fs_disk_count() noexcept { return disks_ready; }

    hcpp::u32 fs_disk_file_count(char disk) noexcept {
        hcpp::u32 count = 0;
        for (hcpp::u32 i = 0; i < fs_max_entries; ++i) {
            if (entries[i].in_use && entries[i].disk == disk &&
                (entries[i].flags & FS_DIR) == 0) {
                ++count;
            }
        }
        return count;
    }

    bool fs_seed() noexcept {
        if (fs_mkdir("HYPEOS://S/apps") != FsStatus::Ok) return false;
        if (fs_mkdir("HYPEOS://S/sys") != FsStatus::Ok) return false;
        if (fs_mkdir("HYPEOS://U/home") != FsStatus::Ok) return false;
        return disks_ready == disk_count_max;
    }

    FsStatus fs_mkdir(const char* path_text) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        if (disk_index(path.disk) >= disk_count_max) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::Exists;
        const hcpp::u16 dir = resolve_dir(path, path.depth - 1);
        if (dir == no_entry) return FsStatus::NotFound;
        const char* leaf = path.components[path.depth - 1];
        if (find_child(dir, path.disk, leaf) != no_entry) return FsStatus::Exists;
        const hcpp::u16 index = alloc_entry();
        if (index == no_entry) return FsStatus::NoSpace;
        name_copy(entries[index].name, leaf);
        entries[index].flags = FS_DIR;
        entries[index].disk = path.disk;
        entries[index].parent = dir;
        return FsStatus::Ok;
    }

    FsStatus fs_write(const char* path_text, const void* data, hcpp::u32 size) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        const hcpp::u32 di = disk_index(path.disk);
        if (di >= disk_count_max) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::IsDir;
        const hcpp::u16 dir = resolve_dir(path, path.depth - 1);
        if (dir == no_entry) return FsStatus::NotFound;
        const char* leaf = path.components[path.depth - 1];
        const hcpp::u16 existing = find_child(dir, path.disk, leaf);
        if (existing != no_entry) {
            FsEntry& entry = entries[existing];
            if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
            if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
            if (size > entry.size) {
                void* memory = hype::codec::arena_alloc(arenas[di], size, 8);
                if (memory == nullptr) return FsStatus::NoSpace;
                entry.offset = static_cast<hcpp::u32>(
                    static_cast<hcpp::u8*>(memory) - arenas[di].base);
            }
            if (size > 0 && data != nullptr) {
                hcpp::copy(arenas[di].base + entry.offset, data, size);
            }
            entry.size = size;
            return FsStatus::Ok;
        }
        const hcpp::u16 index = alloc_entry();
        if (index == no_entry) return FsStatus::NoSpace;
        hcpp::u32 offset = 0;
        if (size > 0) {
            void* memory = hype::codec::arena_alloc(arenas[di], size, 8);
            if (memory == nullptr) {
                entries[index].in_use = 0;
                return FsStatus::NoSpace;
            }
            offset = static_cast<hcpp::u32>(
                static_cast<hcpp::u8*>(memory) - arenas[di].base);
            if (data != nullptr) {
                hcpp::copy(arenas[di].base + offset, data, size);
            }
        }
        name_copy(entries[index].name, leaf);
        entries[index].flags = hypepath_is_system_file(path) ? FS_SYS : FS_NONE;
        entries[index].size = size;
        entries[index].offset = offset;
        entries[index].disk = path.disk;
        entries[index].parent = dir;
        return FsStatus::Ok;
    }

    FsStatus fs_read(const char* path_text, void* out, hcpp::u32 capacity,
                     hcpp::u32* out_size) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        const hcpp::u32 di = disk_index(path.disk);
        if (di >= disk_count_max) return FsStatus::BadDisk;
        const hcpp::u16 index = resolve_entry(path);
        if (index == no_entry) return FsStatus::NotFound;
        const FsEntry& entry = entries[index];
        if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
        const hcpp::u32 bytes = entry.size < capacity ? entry.size : capacity;
        if (bytes > 0) {
            if (out == nullptr) return FsStatus::BadPath;
            hcpp::copy(out, arenas[di].base + entry.offset, bytes);
        }
        if (out_size != nullptr) *out_size = entry.size;
        return FsStatus::Ok;
    }

    FsStatus fs_stat(const char* path_text, FsStat& out) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        if (disk_index(path.disk) >= disk_count_max) return FsStatus::BadDisk;
        const hcpp::u16 index = resolve_entry(path);
        if (index == no_entry) return FsStatus::NotFound;
        const FsEntry& entry = entries[index];
        out = {};
        name_copy(out.name, entry.name);
        out.flags = entry.flags;
        out.size = entry.size;
        out.disk = entry.disk;
        return FsStatus::Ok;
    }

    FsStatus fs_delete(const char* path_text) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        if (disk_index(path.disk) >= disk_count_max) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::AccessDenied;
        const hcpp::u16 index = resolve_entry(path);
        if (index == no_entry) return FsStatus::NotFound;
        FsEntry& entry = entries[index];
        if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
        if ((entry.flags & FS_DIR) != 0 &&
            dir_has_children(index, path.disk)) {
            return FsStatus::AccessDenied;
        }
        entry.in_use = 0;
        entry.name[0] = '\0';
        return FsStatus::Ok;
    }

    hcpp::u32 fs_list(const char* dir_text, FsStat* out, hcpp::u32 out_max) noexcept {
        HypePath path;
        if (hypepath_parse(dir_text, path) != HypePathStatus::Ok) return 0;
        if (disk_index(path.disk) >= disk_count_max) return 0;
        const hcpp::u16 dir = resolve_dir(path, path.depth);
        if (dir == no_entry) return 0;
        hcpp::u32 count = 0;
        for (hcpp::u32 i = 0; i < fs_max_entries; ++i) {
            if (!entries[i].in_use || entries[i].disk != path.disk ||
                entries[i].parent != dir) {
                continue;
            }
            if (out != nullptr && count < out_max) {
                out[count] = {};
                name_copy(out[count].name, entries[i].name);
                out[count].flags = entries[i].flags;
                out[count].size = entries[i].size;
                out[count].disk = entries[i].disk;
            }
            ++count;
        }
        return count;
    }

    bool fs_selftest(FsSelftest& out) noexcept {
        out = {};
        HypePath parsed;
        bool path_ok =
            hypepath_parse("HYPEOS://S/apps/tool.hxer", parsed) == HypePathStatus::Ok &&
            parsed.disk == 'S' && parsed.depth == 2 &&
            name_equal(parsed.components[0], "apps") &&
            name_equal(parsed.components[1], "tool.hxer") &&
            hypepath_disk(parsed.disk) == HypeDisk::System;
        char formatted[hypepath_max_length];
        if (path_ok) {
            const hcpp::u32 written =
                hypepath_format(parsed, formatted, sizeof(formatted));
            path_ok = written > 0 &&
                      name_equal(formatted, "HYPEOS://S/apps/tool.hxer");
        }
        HypePath rejected;
        path_ok = path_ok &&
            hypepath_parse("HYPEOS://S/../escape", rejected) == HypePathStatus::Escape &&
            hypepath_parse("HYPEOS://S/./x", rejected) == HypePathStatus::Escape &&
            hypepath_parse("hypeos://S/apps", rejected) == HypePathStatus::BadScheme &&
            hypepath_parse("/home/user", rejected) == HypePathStatus::BadScheme &&
            hypepath_parse("HYPEOS://q/apps", rejected) == HypePathStatus::BadDisk &&
            hypepath_parse("HYPEOS://Sa/apps", rejected) == HypePathStatus::BadSeparator &&
            hypepath_parse("HYPEOS://S/a//b", rejected) == HypePathStatus::EmptyComponent;
        out.path_ok = path_ok;

        static const char payload[] = "hypeos memfs io";
        bool io_ok =
            fs_write("HYPEOS://U/home/selftest.dat", payload, sizeof(payload)) ==
            FsStatus::Ok;
        char readback[32] = {};
        hcpp::u32 reported = 0;
        if (io_ok) {
            io_ok =
                fs_read("HYPEOS://U/home/selftest.dat", readback, sizeof(readback),
                        &reported) == FsStatus::Ok &&
                reported == sizeof(payload) &&
                hcpp::compare(readback, payload, sizeof(payload)) == 0;
        }
        FsStat listing[8];
        io_ok = io_ok && fs_list("HYPEOS://U/home", listing, 8) >= 1 &&
                (listing[0].flags & FS_DIR) == 0;
        out.io_ok = io_ok;

        bool sys_locked =
            fs_write("HYPEOS://S/sys/probe.hxersys", payload, sizeof(payload)) ==
            FsStatus::Ok;
        if (sys_locked) {
            FsStat info{};
            sys_locked =
                fs_stat("HYPEOS://S/sys/probe.hxersys", info) == FsStatus::Ok &&
                (info.flags & FS_SYS) != 0 &&
                fs_delete("HYPEOS://S/sys/probe.hxersys") == FsStatus::AccessDenied &&
                fs_write("HYPEOS://S/sys/probe.hxersys", payload, sizeof(payload)) ==
                    FsStatus::AccessDenied;
        }
        out.sys_locked = sys_locked;
        return out.path_ok && out.io_ok && out.sys_locked;
    }

}
