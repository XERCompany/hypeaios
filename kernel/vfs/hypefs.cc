#include "kernel/vfs/hypefs.h"

#include "hcpp/memory.hpp"
#include "kernel/codecapps/arena.h"
#include "kernel/guard/landlock.h"
#include "kernel/memory/pmm.h"
#include "kernel/time/timer.h"
#include "kernel/vfs/fat32.h"
#include "kernel/vfs/fdtable.h"
#include "kernel/vfs/hypeblob.h"
#include "kernel/vfs/hypefs_internal.h"

namespace {

    constexpr hcpp::u32 disk_count_max = 3;
    constexpr hcpp::size_t disk_bytes[disk_count_max] = {
        hype::vfs::fs_disk_s_bytes,
        hype::vfs::fs_disk_u_bytes,
        hype::vfs::fs_disk_t_bytes,
    };
    constexpr hcpp::size_t disk_pages[disk_count_max] = {
        hype::vfs::fs_disk_s_bytes / hype::memory::page_size,
        hype::vfs::fs_disk_u_bytes / hype::memory::page_size,
        hype::vfs::fs_disk_t_bytes / hype::memory::page_size,
    };
    constexpr char disk_letters[disk_count_max] = {'S', 'U', 'T'};
    constexpr hcpp::u16 no_entry = 0xFFFF;
    constexpr hcpp::u32 alloc_fail = 0xFFFFFFFFu;

    // File bodies live in the per-disk arena, carved into fixed 4 KiB
    // blocks tracked by a bitmap. Files occupy contiguous runs so reads
    // stay a single memcpy, and fs_delete()/fs_write() return freed runs
    // to the map so space is reclaimed instead of bumped forever.
    constexpr hcpp::u32 fs_block_shift = 12;
    constexpr hcpp::u32 fs_block_size = 1u << fs_block_shift;
    // Bitmap rows are sized for the largest disk (U: grew to 4 MiB for
    // full-res screenshots — see fs_disk_u_bytes in hypefs.h).
    constexpr hcpp::u32 fs_max_blocks =
        hype::vfs::fs_disk_u_bytes >> fs_block_shift;
    constexpr hcpp::u32 fs_map_words = fs_max_blocks / 32;

    // Write-through policy: successful mutations set a dirty bit and a
    // debounced auto-sync pushes the memfs back into the HYPEDATA blob at
    // most once per window. fs_sync()/fs_flush() force it immediately.
    constexpr hcpp::u64 fs_sync_debounce_ms = 250;

    struct FsEntry {
        char name[hype::vfs::fs_name_size];
        hcpp::u32 flags;
        hcpp::u32 size;
        hcpp::u32 offset;
        hcpp::u32 alloc;
        hcpp::u16 parent;
        char disk;
        hcpp::u8 in_use;
    };

    FsEntry entries[hype::vfs::fs_max_entries];
    hcpp::u16 entry_generations[hype::vfs::fs_max_entries];
    hype::codec::Arena arenas[disk_count_max];
    hcpp::u16 disk_roots[disk_count_max];
    hcpp::u32 block_maps[disk_count_max][fs_map_words];
    hcpp::u32 block_totals[disk_count_max];
    hcpp::u32 block_used[disk_count_max];
    hcpp::u32 disks_ready = 0;
    hcpp::u32 dirty_mask = 0;
    hcpp::u64 last_sync_ms = 0;

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

    hcpp::u32 blocks_for(hcpp::u32 bytes) noexcept {
        return (bytes + fs_block_size - 1) >> fs_block_shift;
    }

    // First-fit contiguous run of `bytes` worth of blocks on disk `di`.
    // Returns the arena offset, or alloc_fail when no run fits. The
    // capacity check compares bytes first so blocks_for() cannot wrap.
    hcpp::u32 memfs_alloc(hcpp::u32 di, hcpp::u32 bytes) noexcept {
        if (di >= disk_count_max || bytes == 0 ||
            bytes > (block_totals[di] << fs_block_shift)) {
            return alloc_fail;
        }
        const hcpp::u32 need = blocks_for(bytes);
        hcpp::u32 run = 0;
        hcpp::u32 start = 0;
        for (hcpp::u32 b = 0; b < block_totals[di]; ++b) {
            const bool taken =
                (block_maps[di][b >> 5] & (1u << (b & 31))) != 0;
            if (taken) {
                run = 0;
                continue;
            }
            if (run == 0) start = b;
            if (++run < need) continue;
            for (hcpp::u32 k = start; k < start + need; ++k) {
                block_maps[di][k >> 5] |= (1u << (k & 31));
            }
            block_used[di] += need;
            return start << fs_block_shift;
        }
        return alloc_fail;
    }

    void memfs_free(hcpp::u32 di, hcpp::u32 offset,
                    hcpp::u32 alloc_bytes) noexcept {
        if (di >= disk_count_max || alloc_bytes == 0) return;
        const hcpp::u32 first = offset >> fs_block_shift;
        const hcpp::u32 count = alloc_bytes >> fs_block_shift;
        for (hcpp::u32 b = first; b < first + count && b < block_totals[di];
             ++b) {
            block_maps[di][b >> 5] &= ~(1u << (b & 31));
        }
        block_used[di] = block_used[di] >= count ? block_used[di] - count : 0;
    }

    void mark_dirty(hcpp::u32 di) noexcept {
        if (di < disk_count_max) dirty_mask |= (1u << di);
    }

    // HypeGuard choke point (issue #114): every fs op consults the
    // landlock layer for the current subject right after the path
    // parses, before any entry/table work. Log also counts as allowed
    // — only Deny blocks. Runs before fs_init() too: a denied op
    // returns AccessDenied without touching fs state.
    bool fs_guard_allows(const char* path_text, hcpp::u32 fs_op) noexcept {
        return hype::guard::landlock_check(path_text, fs_op) !=
               hype::guard::GuardResult::Deny;
    }

    // Debounced write-through: at most one hypeblob_sync() per
    // fs_sync_debounce_ms of uptime, and never from inside blob I/O
    // itself (mount_file() routes through fs_write()).
    void fs_autosync() noexcept {
        if (dirty_mask == 0 || hype::vfs::hypeblob_io_active()) return;
        const hcpp::u64 now = hype::time::uptime_ms();
        if (now - last_sync_ms < fs_sync_debounce_ms) return;
        last_sync_ms = now;  // throttle retries even when sync fails
        if (hype::vfs::hypeblob_sync()) dirty_mask = 0;
    }

    void release_entry_body(hcpp::u32 di, FsEntry& entry) noexcept {
        if (entry.alloc != 0) {
            memfs_free(di, entry.offset, entry.alloc);
            entry.alloc = 0;
        }
        entry.offset = 0;
        entry.size = 0;
    }

    hcpp::u16 alloc_entry() noexcept {
        for (hcpp::u32 i = 0; i < hype::vfs::fs_max_entries; ++i) {
            if (entries[i].in_use) continue;
            entries[i] = {};
            entries[i].in_use = 1;
            entries[i].parent = no_entry;
            ++entry_generations[i];
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
        hcpp::set(entry_generations, 0, sizeof(entry_generations));
        hcpp::set(block_maps, 0, sizeof(block_maps));
        hcpp::set(block_totals, 0, sizeof(block_totals));
        hcpp::set(block_used, 0, sizeof(block_used));
        disks_ready = 0;
        dirty_mask = 0;
        last_sync_ms = 0;
        for (hcpp::u32 d = 0; d < disk_count_max; ++d) {
            arenas[d] = {};
            disk_roots[d] = no_entry;
            if (!hype::codec::arena_from_pmm(arenas[d], disk_pages[d])) continue;
            block_totals[d] =
                static_cast<hcpp::u32>(disk_bytes[d] >> fs_block_shift);
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

    hcpp::u32 fs_disk_used(char disk) noexcept {
        const hcpp::u32 di = disk_index(disk);
        if (di >= disk_count_max) return 0;
        return block_used[di] << fs_block_shift;
    }

    hcpp::u32 fs_disk_free(char disk) noexcept {
        const hcpp::u32 di = disk_index(disk);
        if (di >= disk_count_max) return 0;
        return (block_totals[di] - block_used[di]) << fs_block_shift;
    }

    bool fs_dirty() noexcept { return dirty_mask != 0; }

    bool fs_sync() noexcept {
        if (dirty_mask == 0) return true;
        if (hype::vfs::hypeblob_io_active()) return false;
        last_sync_ms = hype::time::uptime_ms();
        if (!hype::vfs::hypeblob_sync()) return false;
        dirty_mask = 0;
        return true;
    }

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
        // F: is the on-disk FAT32 mount (issue #106), not a memfs disk.
        if (path.disk == 'F') return fat32_mkdir(path);
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
        mark_dirty(disk_index(path.disk));
        fs_autosync();
        return FsStatus::Ok;
    }

    // Shared body for fs_write() and detail::fs_entry_write_at(): sizes
    // the block run to `size` and copies `data` at `off`, growing the run
    // (and preserving old contents) when the write crosses the current
    // allocation. The old run is released only after the new one is
    // secured, so a NoSpace failure leaves the file intact.
    static FsStatus store_into(hcpp::u32 di, FsEntry& entry, const void* data,
                        hcpp::u32 off, hcpp::u32 len,
                        hcpp::u32* out_done) noexcept {
        if (off > 0xFFFFFFFFu - len) return FsStatus::BadPath;
        if (out_done != nullptr) *out_done = 0;
        if (len == 0) return FsStatus::Ok;
        const hcpp::u32 end = off + len;
        if (end > entry.alloc) {
            const hcpp::u32 new_offset = memfs_alloc(di, end);
            if (new_offset == alloc_fail) return FsStatus::NoSpace;
            if (entry.size != 0) {
                hcpp::copy(arenas[di].base + new_offset,
                           arenas[di].base + entry.offset, entry.size);
            }
            memfs_free(di, entry.offset, entry.alloc);
            entry.offset = new_offset;
            entry.alloc = blocks_for(end) << fs_block_shift;
        }
        if (off > entry.size) {
            hcpp::set(arenas[di].base + entry.offset + entry.size, 0,
                      off - entry.size);  // deterministic sparse gap
        }
        if (data != nullptr) {
            hcpp::copy(arenas[di].base + entry.offset + off, data, len);
        }
        if (end > entry.size) entry.size = end;
        if (out_done != nullptr) *out_done = len;
        return FsStatus::Ok;
    }

    FsStatus fs_write(const char* path_text, const void* data, hcpp::u32 size) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        if (!fs_guard_allows(path_text, hype::guard::landlock_fs_write)) {
            return FsStatus::AccessDenied;
        }
        if (path.disk == 'F') return fat32_write(path, data, size);
        const hcpp::u32 di = disk_index(path.disk);
        if (di >= disk_count_max) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::IsDir;
        if (size > (block_totals[di] << fs_block_shift)) {
            return FsStatus::NoSpace;
        }
        const hcpp::u16 dir = resolve_dir(path, path.depth - 1);
        if (dir == no_entry) return FsStatus::NotFound;
        const char* leaf = path.components[path.depth - 1];
        const hcpp::u16 existing = find_child(dir, path.disk, leaf);
        if (existing != no_entry) {
            FsEntry& entry = entries[existing];
            if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
            if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
            // Whole-file replace: keep the run when the block count is
            // unchanged, free only the tail blocks when shrinking, and
            // grow via a fresh run so failure leaves the file intact.
            const hcpp::u32 need = blocks_for(size);
            const hcpp::u32 have = entry.alloc >> fs_block_shift;
            if (need < have) {
                const hcpp::u32 first = entry.offset >> fs_block_shift;
                for (hcpp::u32 b = first + need; b < first + have; ++b) {
                    block_maps[di][b >> 5] &= ~(1u << (b & 31));
                }
                block_used[di] -= have - need;
                entry.alloc = need << fs_block_shift;
                if (need == 0) entry.offset = 0;
            } else if (need > have) {
                const hcpp::u32 new_offset = memfs_alloc(di, size);
                if (new_offset == alloc_fail) return FsStatus::NoSpace;
                memfs_free(di, entry.offset, entry.alloc);
                entry.offset = new_offset;
                entry.alloc = need << fs_block_shift;
            }
            if (size > 0 && data != nullptr) {
                hcpp::copy(arenas[di].base + entry.offset, data, size);
            }
            entry.size = size;
            mark_dirty(di);
            fs_autosync();
            return FsStatus::Ok;
        }
        const hcpp::u16 index = alloc_entry();
        if (index == no_entry) return FsStatus::NoSpace;
        hcpp::u32 offset = 0;
        hcpp::u32 alloc = 0;
        if (size > 0) {
            offset = memfs_alloc(di, size);
            if (offset == alloc_fail) {
                entries[index].in_use = 0;
                return FsStatus::NoSpace;
            }
            alloc = blocks_for(size) << fs_block_shift;
            if (data != nullptr) {
                hcpp::copy(arenas[di].base + offset, data, size);
            }
        }
        name_copy(entries[index].name, leaf);
        entries[index].flags = hypepath_is_system_file(path) ? FS_SYS : FS_NONE;
        entries[index].size = size;
        entries[index].offset = offset;
        entries[index].alloc = alloc;
        entries[index].disk = path.disk;
        entries[index].parent = dir;
        mark_dirty(di);
        fs_autosync();
        return FsStatus::Ok;
    }

    FsStatus fs_read(const char* path_text, void* out, hcpp::u32 capacity,
                     hcpp::u32* out_size) noexcept {
        HypePath path;
        if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        if (!fs_guard_allows(path_text, hype::guard::landlock_fs_read)) {
            return FsStatus::AccessDenied;
        }
        if (path.disk == 'F') {
            return fat32_read(path, out, capacity, out_size);
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
        if (path.disk == 'F') return fat32_stat(path, out);
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
        if (!fs_guard_allows(path_text, hype::guard::landlock_fs_delete)) {
            return FsStatus::AccessDenied;
        }
        if (path.disk == 'F') return fat32_delete(path);
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
        // Reclaim the block run so the space is reusable; the entry slot
        // itself is recycled while its generation keeps advancing so
        // stale fds can tell a new file from the old one.
        release_entry_body(disk_index(path.disk), entry);
        entry.in_use = 0;
        entry.name[0] = '\0';
        mark_dirty(disk_index(path.disk));
        fs_autosync();
        return FsStatus::Ok;
    }

    // File-body copy used by the fs_rename() fallback path (FAT32 and
    // cross-disk moves): reads the whole file through the facade and
    // writes it to `newp` — fs_write creates the destination entry.
    // The scratch buffer is page-granular PMM memory; the file size is
    // bounded by the source disk's capacity (or the FAT32 file size).
    static FsStatus fs_copy_file(const HypePath& oldp,
                                 const char* old_text,
                                 const HypePath& newp,
                                 const char* new_text,
                                 hcpp::u32 size) noexcept {
        (void)oldp;
        (void)newp;
        if (size > 32u * 1024u * 1024u) return FsStatus::Unsupported;
        hcpp::uintptr_t mem = 0;
        hcpp::u8* buf = nullptr;
        hcpp::size_t pages = 0;
        if (size != 0) {
            pages =
                (static_cast<hcpp::size_t>(size) +
                 hype::memory::page_size - 1) /
                hype::memory::page_size;
            mem = hype::memory::pmm_allocate_pages(pages);
            if (mem == 0) return FsStatus::NoSpace;
            buf = reinterpret_cast<hcpp::u8*>(mem);
        }
        hcpp::u32 got = 0;
        FsStatus st = fs_read(old_text, buf, size, &got);
        if (st == FsStatus::Ok && got != size) st = FsStatus::IoError;
        if (st == FsStatus::Ok) st = fs_write(new_text, buf, size);
        if (mem != 0) hype::memory::pmm_free_pages(mem, pages);
        return st;
    }

    FsStatus fs_rename(const char* old_text, const char* new_text) noexcept {
        HypePath oldp, newp;
        if (hypepath_parse(old_text, oldp) != HypePathStatus::Ok ||
            hypepath_parse(new_text, newp) != HypePathStatus::Ok) {
            return FsStatus::BadPath;
        }
        // Rename == delete-right on the source + write-right on the
        // destination under the landlock model.
        if (!fs_guard_allows(old_text, hype::guard::landlock_fs_delete) ||
            !fs_guard_allows(new_text, hype::guard::landlock_fs_write)) {
            return FsStatus::AccessDenied;
        }
        if (oldp.depth == 0 || newp.depth == 0) return FsStatus::AccessDenied;

        if (oldp.disk == newp.disk && oldp.disk != 'F') {
            // Native memfs rename: the entry keeps its allocation and is
            // re-linked under the new parent with the new leaf name.
            const hcpp::u32 di = disk_index(oldp.disk);
            if (di >= disk_count_max) return FsStatus::BadDisk;
            const hcpp::u16 index = resolve_entry(oldp);
            if (index == no_entry) return FsStatus::NotFound;
            FsEntry& entry = entries[index];
            if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
            const hcpp::u16 dir = resolve_dir(newp, newp.depth - 1);
            if (dir == no_entry) return FsStatus::NotFound;
            // Re-parenting a directory into its own subtree would create
            // a cycle: walk the destination's ancestor chain and refuse
            // when it passes through the entry being moved.
            if ((entry.flags & FS_DIR) != 0) {
                for (hcpp::u16 p = dir; p != no_entry;
                     p = entries[p].parent) {
                    if (p == index) return FsStatus::BadPath;
                }
            }
            const char* leaf = newp.components[newp.depth - 1];
            const hcpp::u16 clash = find_child(dir, newp.disk, leaf);
            if (clash == index && dir == entry.parent) {
                return FsStatus::Ok;  // same name, same parent: no-op
            }
            if (clash != no_entry) return FsStatus::Exists;
            name_copy(entry.name, leaf);
            entry.parent = dir;
            mark_dirty(di);
            fs_autosync();
            return FsStatus::Ok;
        }

        // FAT32 rename and cross-disk moves have no relink primitive:
        // files go through a real copy+delete; directories refuse.
        {
            FsStat src;
            const FsStatus st = fs_stat(old_text, src);
            if (st != FsStatus::Ok) return st;
            if ((src.flags & FS_DIR) != 0) return FsStatus::Unsupported;
            FsStat dst;
            if (fs_stat(new_text, dst) == FsStatus::Ok) {
                return FsStatus::Exists;
            }
            const FsStatus copied =
                fs_copy_file(oldp, old_text, newp, new_text, src.size);
            if (copied != FsStatus::Ok) return copied;
            return fs_delete(old_text);
        }
    }

    hcpp::u32 fs_list(const char* dir_text, FsStat* out, hcpp::u32 out_max) noexcept {
        HypePath path;
        if (hypepath_parse(dir_text, path) != HypePathStatus::Ok) return 0;
        if (path.disk == 'F') return fat32_list(path, out, out_max);
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

    // ---- fd-table backend (kernel/vfs/hypefs_internal.h) ----

    namespace detail {

        hcpp::u16 fs_entry_resolve(const char* path_text) noexcept {
            HypePath path;
            if (hypepath_parse(path_text, path) != HypePathStatus::Ok) {
                return fs_entry_none;
            }
            if (disk_index(path.disk) >= disk_count_max) {
                return fs_entry_none;
            }
            return resolve_entry(path);
        }

        FsStatus fs_entry_open_create(const char* path_text,
                                      hcpp::u16& out_index) noexcept {
            out_index = fs_entry_none;
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
                if ((entries[existing].flags & FS_DIR) != 0) {
                    return FsStatus::IsDir;
                }
                out_index = existing;
                return FsStatus::Ok;
            }
            const hcpp::u16 index = alloc_entry();
            if (index == no_entry) return FsStatus::NoSpace;
            name_copy(entries[index].name, leaf);
            entries[index].flags =
                hypepath_is_system_file(path) ? FS_SYS : FS_NONE;
            entries[index].disk = path.disk;
            entries[index].parent = dir;
            mark_dirty(di);
            fs_autosync();
            out_index = index;
            return FsStatus::Ok;
        }

        bool fs_entry_matches(hcpp::u16 index, hcpp::u16 parent,
                              hcpp::u16 generation,
                              const char* name) noexcept {
            return index < fs_max_entries && entries[index].in_use &&
                   entries[index].parent == parent &&
                   entry_generations[index] == generation &&
                   name_equal(entries[index].name, name);
        }

        FsStatus fs_entry_read_at(hcpp::u16 index, hcpp::u32 off, void* out,
                                  hcpp::u32 len,
                                  hcpp::u32* out_done) noexcept {
            if (out_done != nullptr) *out_done = 0;
            if (index >= fs_max_entries || !entries[index].in_use) {
                return FsStatus::NotFound;
            }
            const FsEntry& entry = entries[index];
            if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
            if (off >= entry.size || len == 0) return FsStatus::Ok;
            const hcpp::u32 bytes =
                len < entry.size - off ? len : entry.size - off;
            if (out == nullptr) return FsStatus::BadPath;
            const hcpp::u32 di = disk_index(entry.disk);
            if (di >= disk_count_max) return FsStatus::BadDisk;
            hcpp::copy(out, arenas[di].base + entry.offset + off, bytes);
            if (out_done != nullptr) *out_done = bytes;
            return FsStatus::Ok;
        }

        FsStatus fs_entry_write_at(hcpp::u16 index, hcpp::u32 off,
                                   const void* data, hcpp::u32 len,
                                   hcpp::u32* out_done) noexcept {
            if (out_done != nullptr) *out_done = 0;
            if (index >= fs_max_entries || !entries[index].in_use) {
                return FsStatus::NotFound;
            }
            FsEntry& entry = entries[index];
            if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
            if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
            const hcpp::u32 di = disk_index(entry.disk);
            if (di >= disk_count_max) return FsStatus::BadDisk;
            const FsStatus stored =
                store_into(di, entry, data, off, len, out_done);
            if (stored != FsStatus::Ok) return stored;
            mark_dirty(di);
            fs_autosync();
            return FsStatus::Ok;
        }

        FsStatus fs_entry_truncate(hcpp::u16 index) noexcept {
            if (index >= fs_max_entries || !entries[index].in_use) {
                return FsStatus::NotFound;
            }
            FsEntry& entry = entries[index];
            if ((entry.flags & FS_DIR) != 0) return FsStatus::IsDir;
            if ((entry.flags & FS_SYS) != 0) return FsStatus::AccessDenied;
            const hcpp::u32 di = disk_index(entry.disk);
            if (di >= disk_count_max) return FsStatus::BadDisk;
            release_entry_body(di, entry);
            mark_dirty(di);
            fs_autosync();
            return FsStatus::Ok;
        }

        hcpp::u32 fs_entry_size(hcpp::u16 index) noexcept {
            return index < fs_max_entries && entries[index].in_use
                       ? entries[index].size
                       : 0;
        }

        hcpp::u32 fs_entry_flags(hcpp::u16 index) noexcept {
            return index < fs_max_entries && entries[index].in_use
                       ? entries[index].flags
                       : 0;
        }

        hcpp::u16 fs_entry_parent(hcpp::u16 index) noexcept {
            return index < fs_max_entries && entries[index].in_use
                       ? entries[index].parent
                       : no_entry;
        }

        hcpp::u16 fs_entry_generation(hcpp::u16 index) noexcept {
            return index < fs_max_entries ? entry_generations[index] : 0;
        }

        char fs_entry_disk(hcpp::u16 index) noexcept {
            return index < fs_max_entries && entries[index].in_use
                       ? entries[index].disk
                       : '\0';
        }

        void fs_mark_clean() noexcept {
            dirty_mask = 0;
            last_sync_ms = hype::time::uptime_ms();
        }

        // Rebuilds "HYPEOS://D/a/b/leaf" by walking the parent chain.
        hcpp::u32 fs_entry_path(hcpp::u16 index, char* out,
                                hcpp::u32 capacity) noexcept {
            if (out == nullptr || capacity == 0 || index >= fs_max_entries ||
                !entries[index].in_use) {
                return 0;
            }
            hcpp::u16 chain[hypepath_max_components + 1];
            hcpp::u32 depth = 0;
            hcpp::u16 cur = index;
            while (cur != no_entry && cur < fs_max_entries &&
                   entries[cur].in_use && entries[cur].name[0] != '\0' &&
                   depth <= hypepath_max_components) {
                chain[depth++] = cur;
                cur = entries[cur].parent;
            }
            const char disk =
                cur != no_entry && cur < fs_max_entries &&
                        entries[cur].in_use
                    ? entries[cur].disk
                    : entries[index].disk;
            hcpp::u32 used = 0;
            const auto emit = [&out, capacity, &used](char v) noexcept -> bool {
                if (used + 1 >= capacity) return false;
                out[used++] = v;
                return true;
            };
            const char scheme[] = "HYPEOS://";
            for (hcpp::u32 i = 0; scheme[i] != '\0'; ++i) {
                if (!emit(scheme[i])) return 0;
            }
            if (!emit(disk)) return 0;
            while (depth != 0) {
                if (!emit('/')) return 0;
                const FsEntry& part = entries[chain[--depth]];
                for (hcpp::u32 i = 0; part.name[i] != '\0'; ++i) {
                    if (!emit(part.name[i])) return 0;
                }
            }
            out[used] = '\0';
            return used;
        }

    }  // namespace detail

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

        // fd table: open a fresh file, pwrite at offset 0, seek END then
        // SET, fread the bytes back, close. fs_flush is exercised but not
        // gated on (no ATA -> honest IoError, not an fd bug).
        const hcpp::i32 probe_fd =
            fs_open("HYPEOS://U/home/fdprobe.bin",
                    FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE);
        bool fd_ok = probe_fd > 2;
        if (fd_ok) {
            out.fd_value = static_cast<hcpp::u32>(probe_fd);
            static const char fd_payload[] = "hypeos fd io";
            hcpp::u32 moved = 0;
            hcpp::u32 pos = 0;
            char back[16] = {};
            hcpp::u32 got = 0;
            fd_ok =
                fs_pwrite(probe_fd, fd_payload, sizeof(fd_payload), 0,
                          &moved) == FsStatus::Ok &&
                moved == sizeof(fd_payload) &&
                fs_seek(probe_fd, 0, FS_SEEK_END, &pos) == FsStatus::Ok &&
                pos == sizeof(fd_payload) &&
                fs_seek(probe_fd, 0, FS_SEEK_SET, &pos) == FsStatus::Ok &&
                pos == 0 &&
                fs_fread(probe_fd, back, sizeof(fd_payload), &got) ==
                    FsStatus::Ok &&
                got == sizeof(fd_payload) &&
                hcpp::compare(back, fd_payload, sizeof(fd_payload)) == 0 &&
                fs_close(probe_fd) == FsStatus::Ok;
            fs_delete("HYPEOS://U/home/fdprobe.bin");
        }
        out.fd_ok = fd_ok;

        // Directory enumeration: U:/ (trailing slash) must list the
        // seeded + blob-mounted dirs, and U:/home the probe files.
        FsStat kids[16];
        out.dir_items = fs_list("HYPEOS://U/", kids, 16);
        out.dir_ok = out.dir_items != 0 &&
                     fs_list("HYPEOS://U/home", kids, 16) >= 1;

        // Space reclaim: a same-size write+delete+rewrite must return the
        // block run to the map exactly, leaving U: usage at baseline.
        static hcpp::u8 reclaim_buf[2048];
        hcpp::set(reclaim_buf, 0x5A, sizeof(reclaim_buf));
        const hcpp::u32 u_before = fs_disk_used('U');
        const hcpp::u32 expect =
            blocks_for(sizeof(reclaim_buf)) << fs_block_shift;
        const char* const reclaim_path = "HYPEOS://U/home/reclaim.bin";
        bool reclaim_ok =
            fs_write(reclaim_path, reclaim_buf, sizeof(reclaim_buf)) ==
                FsStatus::Ok &&
            fs_disk_used('U') == u_before + expect &&
            fs_delete(reclaim_path) == FsStatus::Ok &&
            fs_disk_used('U') == u_before &&
            fs_write(reclaim_path, reclaim_buf, sizeof(reclaim_buf)) ==
                FsStatus::Ok &&
            fs_disk_used('U') == u_before + expect &&
            fs_delete(reclaim_path) == FsStatus::Ok &&
            fs_disk_used('U') == u_before;
        out.reclaim_ok = reclaim_ok;

        // Write-through: mutations above marked the mount dirty; the
        // debounced auto-sync may already have flushed it. Otherwise a
        // manual fs_sync() must (a no-blob environment reports fail).
        out.sync_auto = !fs_dirty();
        out.sync_ok = out.sync_auto || fs_sync();

        return out.path_ok && out.io_ok && out.sys_locked && out.fd_ok &&
               out.dir_ok && out.reclaim_ok;
    }

}
