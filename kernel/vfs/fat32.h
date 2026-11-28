#pragma once

#include "hcpp/types.hpp"
#include "kernel/vfs/hypefs.h"
#include "kernel/vfs/hypepath.h"

namespace hype::vfs {

    // FAT32 on a real disk partition (issue #106). Separate from the
    // memfs + HYPEDATA blob stack: this mounts an actual on-disk FAT32
    // volume as the VFS disk letter F (HYPEOS://F/...). The stage1 boot
    // sector at LBA 0 carries no MBR partition table today, so the probe
    // usually reports part=none and F: stays absent — every entry point
    // degrades gracefully (BadDisk / empty list).

    struct Fat32Mount {
        bool block_ok;    // a block backend can serve sector I/O
        bool part_ok;     // a FAT32 partition was found and its BPB parsed
        hcpp::u64 part_lba;
        hcpp::u32 clusters;     // usable data clusters
        char label[12];       // BPB volume label, space-trimmed
    };

    // Probes the partition tables: the four MBR slots at LBA 0 for types
    // 0x0B/0x0C, or — when a protective MBR entry (0xEE) is present — the
    // GPT header at LBA 1 and its first 4 entries (EFI System / Basic
    // Data GUIDs). The first partition whose boot sector validates as
    // FAT32 BPB wins. Safe to call once after block_initialize().
    Fat32Mount fat32_mount() noexcept;
    bool fat32_mounted() noexcept;

    // VFS ops for HYPEOS://F/... paths. `path` is the parsed HypePath
    // (disk == 'F'). Names resolve case-insensitively against both the
    // LFN (UCS-2 -> ASCII) and the 8.3 short name. Write-side v1 scope:
    // files and directories are created with short 8.3 names only (leaf
    // components that cannot map to a valid 8.3 name are rejected with
    // BadPath); existing files keep their on-disk name (and LFN).
    // Rewriting an existing file frees its cluster chain and allocates a
    // fresh one; deleting a file marks its entry (and LFN run) 0xE5 and
    // frees the chain. Full-directory chains grow by one zeroed cluster
    // when needed. FAT updates are mirrored to every FAT copy.
    FsStatus fat32_read(const HypePath& path, void* out, hcpp::u32 capacity,
                        hcpp::u32* out_size) noexcept;
    FsStatus fat32_write(const HypePath& path, const void* data,
                         hcpp::u32 size) noexcept;
    FsStatus fat32_stat(const HypePath& path, FsStat& out) noexcept;
    FsStatus fat32_delete(const HypePath& path) noexcept;
    FsStatus fat32_mkdir(const HypePath& path) noexcept;
    hcpp::u32 fat32_list(const HypePath& path, FsStat* out,
                         hcpp::u32 out_max) noexcept;

    // Writes HYPEOS://F/hypeai.txt through the fs_* dispatch, reads it
    // back and compares. False when F: is not mounted or any step fails.
    bool fat32_selftest() noexcept;

}
