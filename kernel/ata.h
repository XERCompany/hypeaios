#pragma once

#include "hcpp/types.hpp"

namespace hype::ata {

    // Minimal ATA PIO driver for the primary IDE bus (0x1F0/0x3F6), master
    // drive, LBA28. QEMU attaches `-drive format=raw,file=...` as exactly this
    // device, so the kernel can read the HYPEDATA payload region after boot
    // without BIOS services.

    struct AtaInfo {
        bool lba48;           // IDENTIFY word 83 bit 10: LBA48 supported
        hcpp::u64 sectors;    // addressable sector count (LBA48 or LBA28)
        char model[41];       // IDENTIFY model string, trailing spaces trimmed
    };

    bool ata_present() noexcept;
    // Reads `sectors` 512-byte sectors starting at 28-bit `lba` into `buffer`.
    // `sectors` must be in [1, 255]. Returns false on timeout/error.
    bool ata_pio_read(hcpp::u32 lba, void* buffer, hcpp::u8 sectors) noexcept;
    // IDENTIFY DEVICE (0xEC). Fills `out` with capacity/model and logs
    // "ata model=... sectors=N" on serial. Returns false on timeout/error.
    bool ata_identify(AtaInfo& out) noexcept;
    // Writes `sectors` 512-byte sectors from `buffer` at 28-bit `lba`
    // (WRITE SECTORS 0x30). Polls DRQ per sector like the read path.
    bool ata_write_sectors(hcpp::u32 lba, const void* buffer,
                           hcpp::u8 sectors) noexcept;
    // LBA48 variants (READ/WRITE SECTORS EXT, 0x24/0x34). `sectors` in
    // [1, 65535]; `lba` must fit in 48 bits.
    bool ata_read_sectors48(hcpp::u64 lba, void* buffer,
                            hcpp::u16 sectors) noexcept;
    bool ata_write_sectors48(hcpp::u64 lba, const void* buffer,
                             hcpp::u16 sectors) noexcept;
    // FLUSH CACHE (0xE7): waits until the drive drains its write cache.
    bool ata_flush_cache() noexcept;

}
