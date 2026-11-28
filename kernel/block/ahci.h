#pragma once

#include "hcpp/types.hpp"
#include "kernel/ata.h"

namespace hype::ahci {

    // AHCI 1.3 SATA host driver. Probes a PCI class 0x01 / subclass 0x06 /
    // prog-if 0x01 controller, takes ownership via the BIOS/OS handoff
    // semaphore when supported, sets GHC.AE, then brings up every
    // implemented port that reports a live link (PxSSTS DET=3, IPM=1):
    // 1KB-aligned command list + 256B-aligned FIS receive area from PMM,
    // SUD/POD spin-up, FRE then ST. Commands are FIS-based register-H2D
    // on slot 0 (IDENTIFY 0xEC, READ DMA EXT 0x25, WRITE DMA EXT 0x35,
    // FLUSH CACHE EXT 0xEA) with PRDT DMA through identity-mapped
    // physical memory. Every poll is bounded; a timeout triggers a
    // COMRESET recovery attempt instead of hanging.
    //
    // Serial markers: "ahci dev=ok ports=N", "ahci portN model=<name>",
    // "ahci rw=ok" after a verify read, "ahci dev=absent" when no SATA
    // controller or no live disk is found.

    // Probes and initializes the HBA. Returns true when at least one
    // port has an identified ATA disk ready for I/O.
    bool ahci_probe() noexcept;
    // True once ahci_probe() found a usable disk on `ahci_port()`.
    bool ahci_ready() noexcept;
    // HBA port index of the active disk, 0xFF when none.
    hcpp::u8 ahci_port() noexcept;
    // Cached IDENTIFY data of the active disk.
    bool ahci_identify(hype::ata::AtaInfo& out) noexcept;
    // READ DMA EXT (LBA48). `sectors` in [1, 65535]; `buffer` must be
    // word-aligned and below 4 GiB (identity mapping).
    bool ahci_read(hcpp::u64 lba, void* buffer, hcpp::u16 sectors) noexcept;
    // WRITE DMA EXT (LBA48). Same constraints as ahci_read().
    bool ahci_write(hcpp::u64 lba, const void* buffer,
                    hcpp::u16 sectors) noexcept;
    // FLUSH CACHE EXT (0xEA): drains the drive's write cache.
    bool ahci_flush() noexcept;

}
