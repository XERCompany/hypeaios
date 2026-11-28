#pragma once

#include "hcpp/types.hpp"
#include "kernel/usb/xhci_host.h"

namespace hype::usb::usbmsc {

    // USB mass-storage class driver: Bulk-Only Transport (BOT) over the
    // configured bulk OUT/IN endpoints plus a small SCSI command set
    // (INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10),
    // READ(10), WRITE(10)). Data-stage stalls get the spec-mandated
    // recovery: xHCI endpoint-halt clear via host_clear_halt() and, on
    // transport-level failure, the Bulk-Only Mass Storage Reset class
    // request followed by halt clears on both bulk endpoints.
    //
    // msc_bind() is called by xhci.cc during enumeration once the
    // interface (class 08 / subclass 06 / protocol 50) is configured. On
    // success the unit registers into the block layer: blockdev.cc
    // consults usbmsc_ready()/usbmsc_read()/usbmsc_write() as its
    // lowest-priority backend, so an existing NVMe/AHCI/ATA boot disk is
    // never shadowed.
    //
    // Verification is read-only by design: probing reads LBA 0 and checks
    // the 0x55AA signature, never writing unknown media.
    //
    // Markers: "usbmsc dev=ok" / "usbmsc dev=absent" (no xHCI/MSC unit),
    // "usbmsc dev=fail s=<step>" on probe failure,
    // "usbmsc cap=<MB> blocks=<N>", "usbmsc rw=ok" after the LBA-0 read
    // verify, "usbmsc sig=55aa" when the boot signature is present.

    void msc_bind(HostDevice *dev, hcpp::u8 iface) noexcept;
    bool usbmsc_ready() noexcept;
    hcpp::u32 usbmsc_units() noexcept;
    bool usbmsc_read(hcpp::u64 lba, void *buffer,
                     hcpp::u32 sectors) noexcept;
    bool usbmsc_write(hcpp::u64 lba, const void *buffer,
                      hcpp::u32 sectors) noexcept;

}  // namespace hype::usb::usbmsc
