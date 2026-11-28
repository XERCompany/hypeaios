#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype {

    struct InterruptRegisters;

    // Crash dump on panic (issue #119, docs/research/gaps-kernel.md
    // 6.1). panic()/panic_exception() call this after the serial dump
    // and BEFORE the halt loop. Builds "header + register frame + klog
    // ring" in a static staging buffer, then persists it via the first
    // path that works:
    //   1. fs_write("HYPEOS://U/crash.log") + hypeblob_sync() — when
    //      the memfs is up (fs_disk_count() != 0) and no blob
    //      serialization is in flight;
    //   2. raw sectors at the crash scratch LBA via block_write():
    //      132 sectors starting at LBA 16450 (= hypeblob_data_lba 8258
    //      + 4 MiB of blob region / 512). That is the first sector past
    //      the declared HYPEDATA window; tools/mkimage.py keeps the gap
    //      up to the ESP inside the "HypeAI data" GPT partition, so the
    //      scratch range can never collide with live blob data or a
    //      filesystem.
    // Prints "crash dump=saved" when either path persisted (the raw
    // path adds a "crash dump raw lba=" detail line) or
    // "crash dump=vfs-offline" when nothing could be written.
    //
    // The path is allocation-free and bounded: the memfs has fixed
    // size, and every block/ATA wait has a poll limit, so the panic
    // path cannot hang.
    void crashdump_write(hcpp::StringView reason,
                         const InterruptRegisters* regs) noexcept;

    // Selftest: writes a small probe dump to U:/crash-test.log, prints
    // "crash test=ok" on success ("crash test=fail|skip" otherwise),
    // then deletes the file so U: stays clean.
    bool crashdump_test() noexcept;

}  // namespace hype
