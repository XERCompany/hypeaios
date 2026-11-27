#pragma once

#include "hcpp/types.hpp"

namespace hype::ata {

    // Minimal ATA PIO driver for the primary IDE bus (0x1F0/0x3F6), master
    // drive, LBA28. QEMU attaches `-drive format=raw,file=...` as exactly this
    // device, so the kernel can read the HYPEDATA payload region after boot
    // without BIOS services.

    bool ata_present() noexcept;
    // Reads `sectors` 512-byte sectors starting at 28-bit `lba` into `buffer`.
    // `sectors` must be in [1, 255]. Returns false on timeout/error.
    bool ata_pio_read(hcpp::u32 lba, void* buffer, hcpp::u8 sectors) noexcept;

}
