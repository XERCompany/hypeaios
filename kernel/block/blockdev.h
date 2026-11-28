#pragma once

#include "hcpp/types.hpp"
#include "kernel/ata.h"

namespace hype::block {

    // Block dispatch layer. Routes sector I/O with the priority
    // NVMe > AHCI > ATA PIO > USB-MSC: an NVMe namespace wins when
    // nvme_probe() brought one up, then AHCI/SATA when ahci_probe()
    // found a live disk, then the legacy ATA PIO path on
    // 0x1F0 — so the same HYPEDATA blob works on modern PCs (no
    // IDE controller) and on QEMU's default drive — and finally the
    // USB mass-storage unit usbmsc.cc bound during xHCI enumeration,
    // so a plugged-in USB stick stays reachable on diskless setups.
    //
    // Call block_initialize() once after PCI scan and PMM are up
    // (stage_vfs does this before hypeblob_mount()).

    // Probes NVMe and AHCI; prints the "nvme dev=ok|absent"/
    // "ahci dev=ok|absent"/"* rw=ok" markers. Returns block_present().
    bool block_initialize() noexcept;
    // True when either backend can serve sector I/O.
    bool block_present() noexcept;
    // IDENTIFY of the active device; logs a model line on both paths.
    bool block_identify(hype::ata::AtaInfo& out) noexcept;
    // Reads/writes `sectors` 512-byte sectors at 48-bit `lba`, chunked
    // internally to whatever the active backend supports. False on any
    // backend error or timeout.
    bool block_read(hcpp::u64 lba, void* buffer,
                    hcpp::u32 sectors) noexcept;
    bool block_write(hcpp::u64 lba, const void* buffer,
                     hcpp::u32 sectors) noexcept;
    // Flush the active device's write cache.
    bool block_flush() noexcept;

}
