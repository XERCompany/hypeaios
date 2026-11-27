#include "kernel/ata.h"

#include "kernel/serial.hpp"

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
    constexpr hcpp::u8 cmd_read_sectors_ext = 0x24;
    constexpr hcpp::u8 cmd_write_sectors = 0x30;
    constexpr hcpp::u8 cmd_write_sectors_ext = 0x34;
    constexpr hcpp::u8 cmd_identify = 0xEC;
    constexpr hcpp::u8 cmd_flush_cache = 0xE7;
    constexpr hcpp::u8 drive_master = 0xA0;
    constexpr hcpp::u8 drive_master_lba = 0xE0;
    constexpr hcpp::u8 drive_master_lba48 = 0x40;

    constexpr hcpp::u32 poll_limit = 0x400000;
    // FLUSH CACHE waits for the host to fsync the whole backing image —
    // on a freshly written multi-GB image that can take tens of seconds,
    // so it gets a far bigger budget than per-sector DRQ polling.
    constexpr hcpp::u32 ready_limit = 0x20000000;

    // IDENTIFY payload scratch; read once per ata_identify() call.
    hcpp::u8 ident_buf[512];

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

    void outsw(hcpp::u16 port, const void* src, hcpp::u32 words) noexcept {
        asm volatile("rep outsw"
                     : "+S"(src), "+c"(words)
                     : "d"(port)
                     : "memory");
    }

    hcpp::u32 le32(const hcpp::u8* p) noexcept {
        return static_cast<hcpp::u32>(p[0]) |
               (static_cast<hcpp::u32>(p[1]) << 8) |
               (static_cast<hcpp::u32>(p[2]) << 16) |
               (static_cast<hcpp::u32>(p[3]) << 24);
    }

    hcpp::u64 le64(const hcpp::u8* p) noexcept {
        return static_cast<hcpp::u64>(le32(p)) |
               (static_cast<hcpp::u64>(le32(p + 4)) << 32);
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

    // Waits until BSY clears (used after non-data commands like FLUSH
    // CACHE, and as a pre-command drain: the spec requires BSY=0 before a
    // new command may be written to the command register).
    bool wait_ready(hcpp::u32 limit) noexcept {
        for (hcpp::u32 i = 0; i < limit; ++i) {
            const hcpp::u8 status = inb(reg_status);
            if ((status & status_err) != 0) return false;
            if ((status & status_bsy) == 0) return true;
        }
        return false;
    }

    bool wait_ready() noexcept { return wait_ready(poll_limit); }

    // LBA48 register block: high bytes first, then low bytes.
    void lba48_regs(hcpp::u64 lba, hcpp::u16 sectors) noexcept {
        outb(reg_sector_count, static_cast<hcpp::u8>((sectors >> 8) & 0xFF));
        outb(reg_lba_low, static_cast<hcpp::u8>((lba >> 24) & 0xFF));
        outb(reg_lba_mid, static_cast<hcpp::u8>((lba >> 32) & 0xFF));
        outb(reg_lba_high, static_cast<hcpp::u8>((lba >> 40) & 0xFF));
        outb(reg_sector_count, static_cast<hcpp::u8>(sectors & 0xFF));
        outb(reg_lba_low, static_cast<hcpp::u8>(lba & 0xFF));
        outb(reg_lba_mid, static_cast<hcpp::u8>((lba >> 8) & 0xFF));
        outb(reg_lba_high, static_cast<hcpp::u8>((lba >> 16) & 0xFF));
    }

}

namespace hype::ata {

    bool ata_present() noexcept {
        // A floating bus reads as 0xFF; that means no drive is attached.
        if (inb(reg_status) == 0xFF) return false;
        outb(reg_drive, drive_master);
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

    bool ata_identify(AtaInfo& out) noexcept {
        out = {};
        if (!wait_ready()) return false;
        outb(reg_drive, drive_master);
        io_delay();
        outb(reg_sector_count, 0);
        outb(reg_lba_low, 0);
        outb(reg_lba_mid, 0);
        outb(reg_lba_high, 0);
        outb(reg_command, cmd_identify);
        if (inb(reg_status) == 0) return false;  // no drive
        if (!wait_data()) return false;
        insw(reg_data, ident_buf, 256);
        // Model string: words 27..46 (bytes 54..93), character pairs are
        // byte-swapped within each word.
        for (hcpp::u32 i = 0; i < 40; i += 2) {
            out.model[i] = static_cast<char>(ident_buf[54 + i + 1]);
            out.model[i + 1] = static_cast<char>(ident_buf[54 + i]);
        }
        out.model[40] = '\0';
        for (hcpp::u32 i = 0; i < 40; ++i) {
            const unsigned char c = static_cast<unsigned char>(out.model[i]);
            if (c < 0x20 || c > 0x7E) out.model[i] = ' ';
        }
        for (hcpp::u32 i = 40; i != 0 && out.model[i - 1] == ' '; --i) {
            out.model[i - 1] = '\0';
        }
        // Word 83 bit 10 = LBA48 supported; words 60..61 = LBA28 capacity,
        // words 100..103 = LBA48 capacity.
        out.lba48 = (ident_buf[167] & 0x04) != 0;
        const hcpp::u64 sectors48 = le64(ident_buf + 200);
        const hcpp::u32 sectors28 = le32(ident_buf + 120);
        out.sectors =
            (out.lba48 && sectors48 != 0) ? sectors48 : sectors28;
        serial::write("ata model=");
        serial::write(out.model);
        serial::write(" sectors=");
        if (out.sectors <= 0xFFFFFFFFull) {
            serial::write_dec32(static_cast<hcpp::u32>(out.sectors));
        } else {
            serial::write_hex64(out.sectors);
        }
        serial::write("\r\n");
        return true;
    }

    bool ata_write_sectors(hcpp::u32 lba, const void* buffer,
                           hcpp::u8 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 ||
            (lba & 0xF0000000u) != 0) {
            return false;
        }
        // Never issue a command while the drive is still busy — e.g. a
        // flush still draining in the background.
        if (!wait_ready(ready_limit)) return false;
        outb(reg_drive,
             drive_master_lba | static_cast<hcpp::u8>((lba >> 24) & 0x0F));
        io_delay();
        outb(reg_sector_count, sectors);
        outb(reg_lba_low, static_cast<hcpp::u8>(lba & 0xFF));
        outb(reg_lba_mid, static_cast<hcpp::u8>((lba >> 8) & 0xFF));
        outb(reg_lba_high, static_cast<hcpp::u8>((lba >> 16) & 0xFF));
        outb(reg_command, cmd_write_sectors);
        const auto* src = static_cast<const hcpp::u8*>(buffer);
        for (hcpp::u32 sector = 0; sector < sectors; ++sector) {
            if (!wait_data()) return false;
            outsw(reg_data, src, 256);
            src += 512;
            io_delay();  // 400ns before polling for the next DRQ
        }
        return wait_ready();
    }

    bool ata_read_sectors48(hcpp::u64 lba, void* buffer,
                            hcpp::u16 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 || (lba >> 48) != 0) {
            return false;
        }
        outb(reg_drive, drive_master_lba48);
        io_delay();
        lba48_regs(lba, sectors);
        outb(reg_command, cmd_read_sectors_ext);
        auto* dst = static_cast<hcpp::u8*>(buffer);
        for (hcpp::u32 sector = 0; sector < sectors; ++sector) {
            if (!wait_data()) return false;
            insw(reg_data, dst, 256);
            dst += 512;
        }
        return true;
    }

    bool ata_write_sectors48(hcpp::u64 lba, const void* buffer,
                             hcpp::u16 sectors) noexcept {
        if (buffer == nullptr || sectors == 0 || (lba >> 48) != 0) {
            return false;
        }
        if (!wait_ready(ready_limit)) return false;
        outb(reg_drive, drive_master_lba48);
        io_delay();
        lba48_regs(lba, sectors);
        outb(reg_command, cmd_write_sectors_ext);
        const auto* src = static_cast<const hcpp::u8*>(buffer);
        for (hcpp::u32 sector = 0; sector < sectors; ++sector) {
            if (!wait_data()) return false;
            outsw(reg_data, src, 256);
            src += 512;
            io_delay();
        }
        return wait_ready();
    }

    bool ata_flush_cache() noexcept {
        if (!wait_ready(ready_limit)) return false;
        outb(reg_drive, drive_master_lba);
        io_delay();
        outb(reg_command, cmd_flush_cache);
        return wait_ready(ready_limit);
    }

}
