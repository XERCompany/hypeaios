#pragma once

#include "hcpp/types.hpp"
#include "kernel/ata.h"

namespace hype::nvme {

    // NVMe 1.4 NVM Express host driver. Probes a PCI class 0x01 /
    // subclass 0x08 / prog-if 0x02 controller, maps its 64-bit
    // memory BAR0 (< 4 GiB, identity-mapped), then performs the
    // standard init sequence: CC.EN=0 wait for CSTS.RDY=0, AQA +
    // ASQ/ACQ admin queues on PMM pages, CC.EN=1 (NVM command set,
    // 64B SQ / 16B CQ entries) wait for CSTS.RDY=1. Doorbell
    // registers live at BAR0+0x1000 with a stride of 4<<CAP.DSTRD
    // bytes per register.
    //
    // Admin commands (IDENTIFY controller/namespace, SET FEATURES,
    // CREATE I/O CQ/SQ) run serialized on admin queue 0; all
    // completions are polled via the CQ phase bit with bounded
    // spin budgets, so a wedged controller never hangs boot. NVM
    // READ/WRITE/FLUSH run on I/O queue pair 1 (64 entries) with
    // PRP1/PRP2/PRP-list DMA into identity-mapped buffers.
    //
    // Serial markers: "nvme dev=ok ns=N lba=<bytes>",
    // "nvme ns<id> cap=<MB>MB model=<mn>", "nvme rw=ok" after a
    // verify read of LBA 0, "nvme dev=absent" when no controller
    // is found, "nvme dev=fail"/"nvme rw=fail" on init/IO failure.

    // Probes and initializes the first NVMe controller + its first
    // active namespace. Returns true when the device is ready for
    // sector I/O.
    bool nvme_probe() noexcept;
    // True once nvme_probe() finished with a usable namespace.
    bool nvme_ready() noexcept;
    // Number of active namespace IDs reported by the controller.
    hcpp::u32 nvme_namespace_count() noexcept;
    // Native LBA size in bytes of the active namespace.
    hcpp::u32 nvme_sector_size() noexcept;
    // Cached identify data mapped onto the ATA info shape: model
    // is the NVMe MN field, sectors is capacity in 512B units.
    bool nvme_identify(hype::ata::AtaInfo& out) noexcept;
    // Read/write `sectors` 512-byte sectors at `lba` through I/O
    // queue 1 (chunked internally to what one NVM command allows).
    // Buffers must be below 4 GiB (identity-mapped DMA). False on
    // any command error or timeout.
    bool nvme_read_sectors(hcpp::u64 lba, void* buffer,
                           hcpp::u32 sectors) noexcept;
    bool nvme_write_sectors(hcpp::u64 lba, const void* buffer,
                            hcpp::u32 sectors) noexcept;
    // NVM FLUSH (0x00): drains the controller's volatile cache.
    bool nvme_flush() noexcept;

}
