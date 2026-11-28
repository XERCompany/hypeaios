#pragma once

#include "hcpp/types.hpp"
#include "kernel/vfs/hypefs.h"

// File-descriptor table over the memfs (issue #103). 64 slots; fd 0/1/2
// are reserved for the serial console and reject all file ops. fds
// encode a per-slot generation so a value reused after close fails
// validation instead of silently hitting a new file. Every op
// revalidates the opened entry (parent + leaf name + entry generation)
// so deleting or recreating a file behind an open fd is reported, not
// aliased.
namespace hype::vfs {

    constexpr hcpp::u32 fs_fd_max = 64;
    constexpr hcpp::i32 fs_fd_invalid = -1;

    enum FsOpenFlags : hcpp::u32 {
        FS_OPEN_READ    = 1u,
        FS_OPEN_WRITE   = 2u,
        FS_OPEN_APPEND  = 4u,   // every fwrite lands at end-of-file
        FS_OPEN_CREATE  = 8u,   // create the file when missing
        FS_OPEN_TRUNC   = 16u,  // truncate to 0 on open (needs a write flag)
    };

    enum FsSeekWhence : hcpp::u32 {
        FS_SEEK_SET = 0,
        FS_SEEK_CUR = 1,
        FS_SEEK_END = 2,
    };

    // Returns an fd >= 3 on success, fs_fd_invalid on failure.
    hcpp::i32 fs_open(const char* path, hcpp::u32 flags) noexcept;
    FsStatus fs_close(hcpp::i32 fd) noexcept;

    // Position-independent I/O: explicit offset, fd seek position
    // untouched.
    FsStatus fs_pread(hcpp::i32 fd, void* out, hcpp::u32 len, hcpp::u32 off,
                      hcpp::u32* out_read) noexcept;
    FsStatus fs_pwrite(hcpp::i32 fd, const void* data, hcpp::u32 len,
                       hcpp::u32 off, hcpp::u32* out_written) noexcept;

    // Sequential I/O at the fd's own offset (advanced by bytes moved).
    FsStatus fs_fread(hcpp::i32 fd, void* out, hcpp::u32 len,
                      hcpp::u32* out_read) noexcept;
    FsStatus fs_fwrite(hcpp::i32 fd, const void* data, hcpp::u32 len,
                       hcpp::u32* out_written) noexcept;

    FsStatus fs_seek(hcpp::i32 fd, hcpp::i32 off, hcpp::u32 whence,
                     hcpp::u32* out_pos) noexcept;

    // Explicit write-through: pushes pending memfs state to the HYPEDATA
    // blob, bypassing the auto-sync debounce. IoError when the flush
    // could not complete (e.g. no mounted blob).
    FsStatus fs_flush(hcpp::i32 fd) noexcept;

}
