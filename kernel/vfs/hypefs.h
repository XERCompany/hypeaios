#pragma once

#include "hcpp/types.hpp"
#include "kernel/vfs/hypepath.h"

namespace hype::vfs {

    constexpr hcpp::u32 fs_name_size = 32;
    constexpr hcpp::u32 fs_max_entries = 192;
    constexpr hcpp::u32 fs_disk_s_bytes = 2u * 1024u * 1024u;
    constexpr hcpp::u32 fs_disk_u_bytes = 2u * 1024u * 1024u;
    constexpr hcpp::u32 fs_disk_t_bytes = 128u * 1024u;

    enum FsFlags : hcpp::u32 {
        FS_NONE = 0,
        FS_DIR = 1u,
        FS_SYS = 2u,
    };

    enum class FsStatus {
        Ok,
        BadPath,
        BadDisk,
        NotFound,
        IsDir,
        Exists,
        AccessDenied,
        NoSpace,
        BadFd,
        IoError,
        // The primitive exists but not for this operand (e.g. renaming a
        // directory on the FAT32 mount, which has no relink primitive).
        Unsupported,
    };

    struct FsStat {
        char name[fs_name_size];
        hcpp::u32 flags;
        hcpp::u32 size;
        char disk;
    };

    struct FsSelftest {
        bool path_ok;
        bool sys_locked;
        bool io_ok;
        // VFS hardening probes (issue #103).
        bool fd_ok;        // fs_open/pwrite/seek/fread/close round-trip
        hcpp::u32 fd_value;  // fd number the probe ran on (0 when open failed)
        bool dir_ok;       // fs_list enumerates U:/ and U:/home
        hcpp::u32 dir_items;
        bool reclaim_ok;   // write+delete+rewrite frees and reuses blocks
        bool sync_ok;      // dirty state reached the blob (auto or manual)
        bool sync_auto;    // true when the debounced auto-sync beat fs_sync()
    };

    void fs_init() noexcept;
    bool fs_seed() noexcept;
    hcpp::u32 fs_disk_count() noexcept;
    hcpp::u32 fs_disk_file_count(char disk) noexcept;
    // Per-mount space accounting in bytes, from the fixed-block allocator.
    hcpp::u32 fs_disk_used(char disk) noexcept;
    hcpp::u32 fs_disk_free(char disk) noexcept;
    FsStatus fs_mkdir(const char* path) noexcept;
    FsStatus fs_write(const char* path, const void* data, hcpp::u32 size) noexcept;
    FsStatus fs_read(const char* path, void* out, hcpp::u32 capacity,
                     hcpp::u32* out_size) noexcept;
    FsStatus fs_stat(const char* path, FsStat& out) noexcept;
    FsStatus fs_delete(const char* path) noexcept;
    // Rename / move (issue #128, hxfiles). On the memfs disks this is a
    // real relink: the entry keeps its block run and is re-parented and
    // renamed in place, so directories move too. On F: (and for
    // cross-disk moves) files are copied through fs_read/fs_write and
    // the source is deleted — a real operation, but not atomic and not
    // available for directories (Unsupported). The destination must not
    // exist (Exists); FS_SYS entries and disk roots refuse
    // (AccessDenied).
    FsStatus fs_rename(const char* old_path, const char* new_path) noexcept;
    hcpp::u32 fs_list(const char* dir_path, FsStat* out, hcpp::u32 out_max) noexcept;
    // Write-through policy: fs_write/fs_delete/fs_pwrite set a dirty flag
    // and a debounced auto-sync pushes the memfs to the HYPEDATA blob at
    // most once per fs_sync_debounce window. fs_sync() flushes pending
    // state immediately (called from the kernel idle loop and fs_flush).
    // Returns true when nothing is dirty or the sync succeeded.
    bool fs_sync() noexcept;
    bool fs_dirty() noexcept;
    bool fs_selftest(FsSelftest& out) noexcept;

}
