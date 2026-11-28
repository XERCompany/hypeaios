#pragma once

#include "hcpp/types.hpp"

namespace hype::sdhci {

    // SD Host Controller (SDHCI spec 1.00-3.00) driver over PCI: probes
    // class 0x08 / subclass 0x05 (SD host controller, prog-if 0x00/0x01)
    // devices — vendor-agnostic since Ricoh/O2/Intel/Broadcom all expose
    // the same SDHCI register set on BAR0.
    //
    // Controller bring-up: software reset -> card detect via Present
    // State -> 3.3V bus power -> 400 kHz SDCLK (power-of-2 divisor for
    // spec 1/2, 10-bit divisor for spec 3). Card init follows the SD
    // physical layer spec: CMD0 -> CMD8 (SDv2 voltage check) -> ACMD41
    // OCR poll (HCS/CCS distinguishes SDSC from SDHC/SDXC) -> CMD2 CID
    // -> CMD3 RCA -> CMD9 CSD (capacity) -> CMD7 select -> ACMD6 4-bit
    // bus -> raise SDCLK up to 25 MHz -> CMD16 512-byte blocks.
    //
    // Data transfers run through SDMA with a 32 KiB-aligned bounce
    // buffer (64 sectors per command, CMD17/18 + CMD24/25, auto-CMD12
    // on multi-block). Controllers without SDMA capability fall back to
    // programmed I/O through the Buffer Data Port. Every poll is
    // bounded — a stuck controller fails the command instead of
    // hanging; command/data-line errors get a software CMD+DAT reset.
    //
    // Verification is read-only by design: the probe reads LBA 0 and
    // accepts any plausible contents, never writing unknown media.
    //
    // Markers: "sdhci dev=ok" (controller up), "sdhci dev=absent" (no
    // SDHCI PCI device, unusable BAR or reset failure),
    // "sdhci dev=fail s=<step>" on a bring-up error,
    // "sdhci card=sdhc cap=<MB>" / "sdhci card=sdsc cap=<MB>",
    // "sdhci card=none" when the slot is empty,
    // "sdhci rw=ok|fail" after the LBA-0 verify read.

    // Probes the PCI class scan, maps BAR0, brings the controller and
    // card up, runs the verify read. Returns true when a card is ready
    // for sector I/O.
    bool sdhci_probe() noexcept;
    // True once sdhci_probe() finished card init successfully. Reverts
    // to false when the card is pulled (Present State re-check).
    bool sdhci_ready() noexcept;
    // Total 512-byte sectors reported by the card's CSD.
    hcpp::u64 sdhci_sectors() noexcept;
    // Reads/writes `sectors` 512-byte sectors at `lba` (block-addressed
    // on SDHC/SDXC, byte-addressed internally on SDSC). False on any
    // controller/card error or timeout.
    bool sdhci_read(hcpp::u64 lba, void* buffer,
                    hcpp::u32 sectors) noexcept;
    bool sdhci_write(hcpp::u64 lba, const void* buffer,
                     hcpp::u32 sectors) noexcept;
    // SD media has no cache-flush command; always succeeds when ready.
    bool sdhci_flush() noexcept;

}
