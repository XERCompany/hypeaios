#include "kernel/block/blockdev.h"

#include "kernel/ata.h"
#include "kernel/block/ahci.h"
#include "kernel/block/nvme.h"
#include "kernel/usb/usbmsc.h"

namespace hype::block {

    namespace {

        // Per-command ceilings: ATA PIO accepts at most 255 sectors per
        // request (28-bit LBA); AHCI gets the full LBA48 range, but the
        // command table caps a single transfer at prdt_max * 4 MiB, so
        // stay under it and under the u16 sector-count field.
        constexpr hcpp::u32 ata_chunk = 255;
        constexpr hcpp::u32 ahci_chunk = 32768;

        constexpr hcpp::u32 sector_size = 512;

    }

    bool block_initialize() noexcept {
        // Priority order: NVMe > AHCI > ATA PIO. Both probes log their
        // own dev=ok/absent markers; routing prefers NVMe whenever a
        // namespace came up.
        nvme::nvme_probe();
        ahci::ahci_probe();
        return block_present();
    }

    bool block_present() noexcept {
        return nvme::nvme_ready() || ahci::ahci_ready() ||
               ata::ata_present() || usb::usbmsc::usbmsc_ready();
    }

    bool block_identify(ata::AtaInfo& out) noexcept {
        if (nvme::nvme_ready()) return nvme::nvme_identify(out);
        if (ahci::ahci_ready()) return ahci::ahci_identify(out);
        return ata::ata_identify(out);
    }

    bool block_read(hcpp::u64 lba, void* buffer,
                    hcpp::u32 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 || (lba >> 48) != 0) {
            return false;
        }
        if (nvme::nvme_ready()) {
            return nvme::nvme_read_sectors(lba, buffer, sectors);
        }
        if (ahci::ahci_ready()) {
            while (sectors != 0) {
                const hcpp::u16 count = static_cast<hcpp::u16>(
                    sectors < ahci_chunk ? sectors : ahci_chunk);
                if (!ahci::ahci_read(lba, buffer, count)) return false;
                lba += count;
                buffer = static_cast<hcpp::u8*>(buffer) +
                         count * sector_size;
                sectors -= count;
            }
            return true;
        }
        if (ata::ata_present()) {
            while (sectors != 0) {
                const hcpp::u8 count = static_cast<hcpp::u8>(
                    sectors < ata_chunk ? sectors : ata_chunk);
                if (!ata::ata_pio_read(static_cast<hcpp::u32>(lba), buffer,
                                       count)) {
                    return false;
                }
                lba += count;
                buffer = static_cast<hcpp::u8*>(buffer) +
                         count * sector_size;
                sectors -= count;
            }
            return true;
        }
        if (usb::usbmsc::usbmsc_ready()) {
            return usb::usbmsc::usbmsc_read(lba, buffer, sectors);
        }
        return false;
    }

    bool block_write(hcpp::u64 lba, const void* buffer,
                     hcpp::u32 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 || (lba >> 48) != 0) {
            return false;
        }
        if (nvme::nvme_ready()) {
            return nvme::nvme_write_sectors(lba, buffer, sectors);
        }
        if (ahci::ahci_ready()) {
            while (sectors != 0) {
                const hcpp::u16 count = static_cast<hcpp::u16>(
                    sectors < ahci_chunk ? sectors : ahci_chunk);
                if (!ahci::ahci_write(lba, buffer, count)) return false;
                lba += count;
                buffer = static_cast<const hcpp::u8*>(buffer) +
                         count * sector_size;
                sectors -= count;
            }
            return true;
        }
        if (ata::ata_present()) {
            while (sectors != 0) {
                const hcpp::u8 count = static_cast<hcpp::u8>(
                    sectors < ata_chunk ? sectors : ata_chunk);
                if (!ata::ata_write_sectors(static_cast<hcpp::u32>(lba),
                                            buffer, count)) {
                    return false;
                }
                lba += count;
                buffer = static_cast<const hcpp::u8*>(buffer) +
                         count * sector_size;
                sectors -= count;
            }
            return true;
        }
        if (usb::usbmsc::usbmsc_ready()) {
            return usb::usbmsc::usbmsc_write(lba, buffer, sectors);
        }
        return false;
    }

    bool block_flush() noexcept {
        if (nvme::nvme_ready()) return nvme::nvme_flush();
        if (ahci::ahci_ready()) return ahci::ahci_flush();
        if (usb::usbmsc::usbmsc_ready() && !ata::ata_present()) {
            return true;  // BOT has no cache-flush command here
        }
        return ata::ata_flush_cache();
    }

}
