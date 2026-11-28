#pragma once

#include "kernel/vfs/hypefs.h"

// Internal contract between the memfs core (hypefs.cc) and the file-
// descriptor table (fdtable.cc). Not for general app use: entry indices
// are unstable across delete+recreate, so callers revalidate through
// fs_entry_matches() (parent + leaf name + generation) before every I/O.
namespace hype::vfs::detail {

    constexpr hcpp::u16 fs_entry_none = 0xFFFF;

    // Resolves a full HYPEOS:// path to an entry index (file or dir).
    hcpp::u16 fs_entry_resolve(const char* path) noexcept;

    // Returns the entry for `path`, creating an empty file when missing.
    // Existing directories report IsDir.
    FsStatus fs_entry_open_create(const char* path, hcpp::u16& out_index) noexcept;

    // True when `index` still names the same file the fd opened: entry is
    // in use, sits under the recorded parent, keeps the recorded leaf
    // name, and has not been recycled (generation check).
    bool fs_entry_matches(hcpp::u16 index, hcpp::u16 parent,
                          hcpp::u16 generation, const char* name) noexcept;

    FsStatus fs_entry_read_at(hcpp::u16 index, hcpp::u32 off, void* out,
                              hcpp::u32 len, hcpp::u32* out_done) noexcept;
    FsStatus fs_entry_write_at(hcpp::u16 index, hcpp::u32 off,
                               const void* data, hcpp::u32 len,
                               hcpp::u32* out_done) noexcept;
    FsStatus fs_entry_truncate(hcpp::u16 index) noexcept;

    hcpp::u32 fs_entry_size(hcpp::u16 index) noexcept;
    hcpp::u32 fs_entry_flags(hcpp::u16 index) noexcept;
    hcpp::u16 fs_entry_parent(hcpp::u16 index) noexcept;
    hcpp::u16 fs_entry_generation(hcpp::u16 index) noexcept;
    char fs_entry_disk(hcpp::u16 index) noexcept;

    // Writes the canonical "HYPEOS://D/a/b" path for an entry. Returns
    // the length, or 0 on failure.
    hcpp::u32 fs_entry_path(hcpp::u16 index, char* out,
                            hcpp::u32 capacity) noexcept;

    // Called by hypeblob_sync() after a verified write-back so the
    // write-through layer drops its dirty bits regardless of which
    // entry point triggered the sync.
    void fs_mark_clean() noexcept;

}
