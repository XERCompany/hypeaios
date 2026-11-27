#pragma once

#include "hcpp/types.hpp"
#include "kernel/vfs/hypepath.h"

namespace hype::vfs {

    constexpr hcpp::u32 fs_name_size = 32;
    constexpr hcpp::u32 fs_max_entries = 192;
    constexpr hcpp::u32 fs_disk_s_bytes = 256u * 1024u;
    constexpr hcpp::u32 fs_disk_u_bytes = 384u * 1024u;
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
    };

    void fs_init() noexcept;
    bool fs_seed() noexcept;
    hcpp::u32 fs_disk_count() noexcept;
    FsStatus fs_mkdir(const char* path) noexcept;
    FsStatus fs_write(const char* path, const void* data, hcpp::u32 size) noexcept;
    FsStatus fs_read(const char* path, void* out, hcpp::u32 capacity,
                     hcpp::u32* out_size) noexcept;
    FsStatus fs_stat(const char* path, FsStat& out) noexcept;
    FsStatus fs_delete(const char* path) noexcept;
    hcpp::u32 fs_list(const char* dir_path, FsStat* out, hcpp::u32 out_max) noexcept;
    bool fs_selftest(FsSelftest& out) noexcept;

}
