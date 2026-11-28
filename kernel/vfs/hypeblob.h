#pragma once

#include "hcpp/types.hpp"

namespace hype::vfs {

    // HYPEDATA blob lives at a fixed LBA in the raw disk image, right after
    // the kernel area. Must match tools/mkimage.py (1 + 32 + 4096).
    constexpr hcpp::u32 hypeblob_data_lba = 4129;

    struct BlobMount {
        bool ata_ok;
        bool lba_ok;
        bool magic_ok;
        hcpp::u32 files;
        hcpp::u64 bytes;
    };

    // Reads the HYPEDATA region through the block layer (AHCI DMA when a
    // SATA disk is live, ATA PIO otherwise) and mounts every entry as a
    // memfs file. Entries named "<disk>/<path>" ("u", "s" or "t") mount at
    // "HYPEOS://<DISK>/<path>"; all others mount under HYPEOS://S/data/.
    BlobMount hypeblob_mount() noexcept;

    // Serializes the persistable memfs state (U:*, S:/data, T:*) back into
    // the HYPEDATA region at hypeblob_data_lba, then flushes the drive
    // cache. Every written sector is read back and compared before the
    // call reports success. No-op (returns false) until hypeblob_mount()
    // has seen a valid on-disk blob, so boot-time writes cannot clobber
    // the region. Format matches tools/mkimage.py exactly.
    bool hypeblob_sync() noexcept;

}
