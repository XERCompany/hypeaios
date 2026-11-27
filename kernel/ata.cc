#include "kernel/ata.h"

namespace {

    constexpr hcpp::u16 reg_data = 0x1F0;
    constexpr hcpp::u16 reg_sector_count = 0x1F2;
    constexpr hcpp::u16 reg_lba_low = 0x1F3;
    constexpr hcpp::u16 reg_lba_mid = 0x1F4;
    constexpr hcpp::u16 reg_lba_high = 0x1F5;
    constexpr hcpp::u16 reg_drive = 0x1F6;
    constexpr hcpp::u16 reg_status = 0x1F7;
    constexpr hcpp::u16 reg_command = 0x1F7;
    constexpr hcpp::u16 reg_alt_status = 0x3F6;

    constexpr hcpp::u8 status_err = 0x01;
    constexpr hcpp::u8 status_drq = 0x08;
    constexpr hcpp::u8 status_bsy = 0x80;

    constexpr hcpp::u8 cmd_read_sectors = 0x20;
    constexpr hcpp::u8 drive_master_lba = 0xE0;

    constexpr hcpp::u32 poll_limit = 0x400000;

    hcpp::u8 inb(hcpp::u16 port) noexcept {
        hcpp::u8 value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    void outb(hcpp::u16 port, hcpp::u8 value) noexcept {
        asm volatile("outb %0, %1" ::"a"(value), "Nd"(port));
    }

    void insw(hcpp::u16 port, void* dest, hcpp::u32 words) noexcept {
        asm volatile("rep insw"
                     : "+D"(dest), "+c"(words)
                     : "d"(port)
                     : "memory");
    }

    // ~400ns delay: four alternate-status reads, per ATA spec.
    void io_delay() noexcept {
        inb(reg_alt_status);
        inb(reg_alt_status);
        inb(reg_alt_status);
        inb(reg_alt_status);
    }

    bool wait_data() noexcept {
        for (hcpp::u32 i = 0; i < poll_limit; ++i) {
            const hcpp::u8 status = inb(reg_status);
            if ((status & status_err) != 0) return false;
            if ((status & (status_bsy | status_drq)) == status_drq) return true;
        }
        return false;
    }

}

namespace hype::ata {

    bool ata_present() noexcept {
        // A floating bus reads as 0xFF; that means no drive is attached.
        if (inb(reg_status) == 0xFF) return false;
        outb(reg_drive, 0xA0);  // select master
        io_delay();
        return inb(reg_status) != 0xFF;
    }

    bool ata_pio_read(hcpp::u32 lba, void* buffer, hcpp::u8 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 || (lba & 0xF0000000u) != 0) {
            return false;
        }
        outb(reg_drive,
             drive_master_lba | static_cast<hcpp::u8>((lba >> 24) & 0x0F));
        io_delay();
        outb(reg_sector_count, sectors);
        outb(reg_lba_low, static_cast<hcpp::u8>(lba & 0xFF));
        outb(reg_lba_mid, static_cast<hcpp::u8>((lba >> 8) & 0xFF));
        outb(reg_lba_high, static_cast<hcpp::u8>((lba >> 16) & 0xFF));
        outb(reg_command, cmd_read_sectors);
        auto* dst = static_cast<hcpp::u8*>(buffer);
        for (hcpp::u32 sector = 0; sector < sectors; ++sector) {
            if (!wait_data()) return false;
            insw(reg_data, dst, 256);
            dst += 512;
        }
        return true;
    }

}
