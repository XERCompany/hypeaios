// USB mass-storage BOT (Bulk-Only Transport) + SCSI driver.
//
// Protocol per command: a 31-byte Command Block Wrapper on bulk OUT, an
// optional data stage on the direction-implied bulk endpoint, then a
// 13-byte Command Status Wrapper on bulk IN. CSW status 1 (command
// failed) triggers REQUEST SENSE to clear the check condition; status 2
// (phase error) or a transport fault triggers the Bulk-Only Mass
// Storage Reset recovery (class request 0xFF + halt clears on both bulk
// endpoints). Stalls on the data or CSW stage are recovered with
// host_clear_halt(), which performs the xHCI Reset Endpoint / Set TR
// Dequeue dance plus the device-side CLEAR_FEATURE(ENDPOINT_HALT).
//
// Scratch layout per unit: one page for CBW (offset 0) + CSW (offset 32)
// + small reply data (offset 48), one page as the data-stage bounce
// buffer (READ CAPACITY / INQUIRY land there directly; block I/O copies
// through it in <= 4 KiB chunks so arbitrary caller buffers work).
//
// Markers documented in usbmsc.h.

#include "kernel/usb/usbmsc.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::usb::usbmsc {

    namespace {

        constexpr hcpp::u8 code_success = 1;
        constexpr hcpp::u8 code_stall = 6;
        constexpr hcpp::u8 code_short_packet = 13;

        constexpr hcpp::u32 cbw_sig = 0x43425355;  // 'USBC'
        constexpr hcpp::u32 csw_sig = 0x53425355;  // 'USBS'
        constexpr hcpp::u32 cbw_size = 31;
        constexpr hcpp::u32 csw_off = 32;
        constexpr hcpp::u32 csw_size = 13;
        constexpr hcpp::u32 dma_chunk = 4096;  // one page per data stage

        constexpr hcpp::u32 max_units = 2;

        // Bulk-Only Mass Storage Reset (bRequest 0xFF, class/interface).
        constexpr hcpp::u8 req_bomsr = 0xFF;
        constexpr hcpp::u8 req_get_max_lun = 0xFE;

        // SCSI opcodes used here.
        constexpr hcpp::u8 scsi_inquiry = 0x12;
        constexpr hcpp::u8 scsi_request_sense = 0x03;
        constexpr hcpp::u8 scsi_test_unit_ready = 0x00;
        constexpr hcpp::u8 scsi_read_capacity_10 = 0x25;
        constexpr hcpp::u8 scsi_read_10 = 0x28;
        constexpr hcpp::u8 scsi_write_10 = 0x2A;

        struct Unit {
            HostDevice *dev;
            hcpp::u8 iface;
            hcpp::u8 lun;
            hcpp::u32 block_size;
            hcpp::u64 blocks;
            bool ready;
            hcpp::u64 xfer_phys;  // CBW @0, CSW @32
            hcpp::u64 data_phys;  // data-stage bounce page
        };

        Unit units[max_units];
        hcpp::u32 unit_count;
        hcpp::u32 next_tag = 0x55AA0001u;

        void pause(hcpp::u64 ticks) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            while (interrupts::pit_ticks() - start < ticks) {
                hype_cpu_halt();
            }
        }

        void le32_put(volatile hcpp::u8 *p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v);
            p[1] = static_cast<hcpp::u8>(v >> 8);
            p[2] = static_cast<hcpp::u8>(v >> 16);
            p[3] = static_cast<hcpp::u8>(v >> 24);
        }
        hcpp::u32 le32_get(const volatile hcpp::u8 *p) noexcept {
            return hcpp::u32(p[0]) | (hcpp::u32(p[1]) << 8) |
                   (hcpp::u32(p[2]) << 16) | (hcpp::u32(p[3]) << 24);
        }
        hcpp::u32 be32_get(const volatile hcpp::u8 *p) noexcept {
            return (hcpp::u32(p[0]) << 24) | (hcpp::u32(p[1]) << 16) |
                   (hcpp::u32(p[2]) << 8) | hcpp::u32(p[3]);
        }

        void copy_to(volatile hcpp::u8 *dst, const hcpp::u8 *src,
                     hcpp::u32 n) noexcept {
            for (hcpp::u32 i = 0; i < n; ++i) dst[i] = src[i];
        }
        void copy_from(hcpp::u8 *dst, const volatile hcpp::u8 *src,
                       hcpp::u32 n) noexcept {
            for (hcpp::u32 i = 0; i < n; ++i) dst[i] = src[i];
        }

        bool bulk_ok(hcpp::u8 code) noexcept {
            return code == code_success || code == code_short_packet;
        }

        // BOT reset recovery: class reset on EP0, then halt clear on both
        // bulk endpoints (xHCI endpoint reset + dequeue fix inside
        // host_clear_halt).
        void bot_reset_recovery(Unit &u) noexcept {
            host_control(u.dev, 0x21, req_bomsr, 0, u.iface, 0, 0, false);
            host_clear_halt(u.dev, false);
            host_clear_halt(u.dev, true);
        }

        // Run one BOT command. data_phys is the data-stage buffer (may be
        // 0 when data_len is 0). Returns 0 = CSW good status,
        // 1 = check condition, 2 = transport/phase failure.
        hcpp::u8 bot_command(Unit &u, const hcpp::u8 *cdb, hcpp::u8 cdb_len,
                             hcpp::u32 data_len, bool dir_in,
                             hcpp::u64 data_phys) noexcept {
            volatile hcpp::u8 *x =
                reinterpret_cast<volatile hcpp::u8 *>(u.xfer_phys);
            const hcpp::u32 tag = next_tag++;
            le32_put(x + 0, cbw_sig);
            le32_put(x + 4, tag);
            le32_put(x + 8, data_len);
            x[12] = dir_in ? 0x80 : 0x00;
            x[13] = static_cast<hcpp::u8>(u.lun & 0x0F);
            x[14] = static_cast<hcpp::u8>(cdb_len & 0x1F);
            for (hcpp::u8 i = 0; i < 16; ++i) {
                x[15 + i] = i < cdb_len ? cdb[i] : 0;
            }

            hcpp::u32 residual = 0;
            hcpp::u8 code =
                host_bulk(u.dev, false, u.xfer_phys, cbw_size, &residual);
            if (code == code_stall) {
                // CBW stall: endpoint-level recovery then give up — the
                // device is confused; caller escalates to reset recovery.
                host_clear_halt(u.dev, false);
                return 2;
            }
            if (!bulk_ok(code)) return 2;

            if (data_len != 0) {
                code = host_bulk(u.dev, dir_in, data_phys, data_len,
                                 &residual);
                if (code == code_stall) {
                    // Data-stage stall per BOT spec: clear the halt and
                    // carry on to the CSW so the device can report status.
                    host_clear_halt(u.dev, dir_in);
                } else if (!bulk_ok(code)) {
                    return 2;
                }
            }

            // CSW: one stall retry, then fail.
            bool got = false;
            for (hcpp::u8 attempt = 0; attempt < 2 && !got; ++attempt) {
                code = host_bulk(u.dev, true, u.xfer_phys + csw_off,
                                 csw_size, &residual);
                if (code == code_stall) {
                    host_clear_halt(u.dev, true);
                    continue;
                }
                got = bulk_ok(code);
            }
            if (!got) return 2;
            if (le32_get(x + csw_off) != csw_sig ||
                le32_get(x + csw_off + 4) != tag) {
                return 2;  // CSW desync: caller does reset recovery
            }
            const hcpp::u8 status = x[csw_off + 12];
            return status == 0 ? 0 : (status == 1 ? 1 : 2);
        }

        // SCSI command wrapper: on check-condition, REQUEST SENSE drains
        // the pending sense so the next command starts clean; on
        // transport failure, the BOT reset recovery runs.
        bool scsi_command(Unit &u, const hcpp::u8 *cdb, hcpp::u8 cdb_len,
                          hcpp::u32 data_len, bool dir_in,
                          hcpp::u64 data_phys) noexcept {
            const hcpp::u8 st =
                bot_command(u, cdb, cdb_len, data_len, dir_in, data_phys);
            if (st == 0) return true;
            if (st == 1) {
                const hcpp::u8 sense[6] = {scsi_request_sense, 0, 0, 0,
                                           18, 0};
                bot_command(u, sense, 6, 18, true, u.data_phys);
            } else {
                bot_reset_recovery(u);
            }
            return false;
        }

        bool unit_inquiry(Unit &u) noexcept {
            volatile hcpp::u8 *b =
                reinterpret_cast<volatile hcpp::u8 *>(u.data_phys);
            for (hcpp::u32 i = 0; i < 36; ++i) b[i] = 0;
            const hcpp::u8 cdb[6] = {scsi_inquiry, 0, 0, 0, 36, 0};
            if (!scsi_command(u, cdb, 6, 36, true, u.data_phys)) {
                return false;
            }
            // Vendor (8..15) + product (16..31), printable as-is.
            serial::write("usbmsc inq=");
            for (hcpp::u32 i = 8; i < 36; ++i) {
                const hcpp::u8 c = b[i];
                serial::write_char(c >= 32 && c < 127
                                       ? static_cast<char>(c)
                                       : ' ');
            }
            serial::write("\r\n");
            return true;
        }

        bool unit_ready(Unit &u) noexcept {
            const hcpp::u8 cdb[6] = {scsi_test_unit_ready, 0, 0, 0, 0, 0};
            return scsi_command(u, cdb, 6, 0, false, 0);
        }

        bool unit_capacity(Unit &u) noexcept {
            volatile hcpp::u8 *b =
                reinterpret_cast<volatile hcpp::u8 *>(u.data_phys);
            for (hcpp::u32 i = 0; i < 8; ++i) b[i] = 0;
            const hcpp::u8 cdb[10] = {scsi_read_capacity_10, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0};
            if (!scsi_command(u, cdb, 10, 8, true, u.data_phys)) {
                return false;
            }
            u.blocks = hcpp::u64(be32_get(b)) + 1;
            u.block_size = be32_get(b + 4);
            return u.block_size != 0;
        }

        bool unit_blocks(Unit &u, hcpp::u64 lba, hcpp::u32 count,
                         hcpp::u64 phys, bool write) noexcept {
            hcpp::u8 cdb[10] = {};
            cdb[0] = write ? scsi_write_10 : scsi_read_10;
            cdb[1] = static_cast<hcpp::u8>((u.lun & 0x7) << 5);
            cdb[2] = static_cast<hcpp::u8>(lba >> 24);
            cdb[3] = static_cast<hcpp::u8>(lba >> 16);
            cdb[4] = static_cast<hcpp::u8>(lba >> 8);
            cdb[5] = static_cast<hcpp::u8>(lba);
            cdb[7] = static_cast<hcpp::u8>(count >> 8);
            cdb[8] = static_cast<hcpp::u8>(count);
            return scsi_command(u, cdb, 10, count * u.block_size, !write,
                                phys);
        }

        Unit *active_unit() noexcept {
            for (auto &u : units)
                if (u.ready) return &u;
            return nullptr;
        }

        bool unit_usable(const Unit &u) noexcept {
            // Power-of-two block size that fits the bounce page.
            return u.block_size >= 512 && u.block_size <= dma_chunk &&
                   (u.block_size & (u.block_size - 1)) == 0;
        }

    }  // namespace

    void msc_bind(HostDevice *dev, hcpp::u8 iface) noexcept {
        if (unit_count >= max_units) {
            serial::write_line("usbmsc dev=skip");
            return;
        }
        Unit &u = units[unit_count++];
        u = Unit{};
        u.dev = dev;
        u.iface = iface;
        u.xfer_phys = memory::pmm_allocate_page();
        u.data_phys = memory::pmm_allocate_page();
        if (u.xfer_phys == 0 || u.data_phys == 0) {
            serial::write_line("usbmsc dev=fail s=1");
            return;
        }

        // GET_MAX_LUN is optional; a stall or error just means LUN 0.
        volatile hcpp::u8 *sc =
            reinterpret_cast<volatile hcpp::u8 *>(host_scratch());
        sc[0] = 0;
        u.lun = 0;
        if (host_control(dev, 0xA1, req_get_max_lun, 0, iface, 1,
                         host_scratch(), true)) {
            u.lun = static_cast<hcpp::u8>(sc[0] & 0x0F);
        }

        if (!unit_inquiry(u)) {
            bot_reset_recovery(u);
            if (!unit_inquiry(u)) {
                serial::write_line("usbmsc dev=fail s=2");
                return;
            }
        }
        // Removable media may need a few TUR attempts while it spins up.
        bool ready = false;
        for (hcpp::u8 attempt = 0; attempt < 4 && !ready; ++attempt) {
            ready = unit_ready(u);
            if (!ready) pause(5);
        }
        if (!ready) {
            serial::write_line("usbmsc dev=fail s=3");
            return;
        }
        if (!unit_capacity(u) && !unit_capacity(u)) {
            serial::write_line("usbmsc dev=fail s=4");
            return;
        }
        if (!unit_usable(u) || u.blocks == 0) {
            serial::write_line("usbmsc dev=fail s=5");
            return;
        }

        u.ready = true;
        serial::write_line("usbmsc dev=ok");
        serial::write("usbmsc cap=");
        serial::write_dec32(static_cast<hcpp::u32>(
            (u.blocks * u.block_size) / (1024 * 1024)));
        serial::write(" blocks=");
        serial::write_dec32(static_cast<hcpp::u32>(u.blocks));
        serial::write("\r\n");

        // Read-only verification: pull LBA 0 through the BOT path and
        // check the boot signature. No writes to unknown media.
        if (unit_blocks(u, 0, 1, u.data_phys, false)) {
            serial::write_line("usbmsc rw=ok");
            const volatile hcpp::u8 *b =
                reinterpret_cast<volatile hcpp::u8 *>(u.data_phys);
            if (u.block_size >= 512 && b[510] == 0x55 && b[511] == 0xAA) {
                serial::write_line("usbmsc sig=55aa");
            }
        } else {
            serial::write_line("usbmsc rw=fail");
        }
    }

    bool usbmsc_ready() noexcept { return active_unit() != nullptr; }

    hcpp::u32 usbmsc_units() noexcept {
        hcpp::u32 n = 0;
        for (const auto &u : units)
            if (u.ready) ++n;
        return n;
    }

    bool usbmsc_read(hcpp::u64 lba, void *buffer,
                     hcpp::u32 sectors) noexcept {
        Unit *u = active_unit();
        if (u == nullptr || buffer == nullptr || sectors == 0) return false;
        if (lba + sectors > u->blocks) return false;
        const hcpp::u32 chunk_blocks = dma_chunk / u->block_size;
        hcpp::u8 *out = static_cast<hcpp::u8 *>(buffer);
        while (sectors != 0) {
            const hcpp::u32 n =
                sectors < chunk_blocks ? sectors : chunk_blocks;
            if (!unit_blocks(*u, lba, n, u->data_phys, false)) {
                return false;
            }
            copy_from(out,
                      reinterpret_cast<const volatile hcpp::u8 *>(
                          u->data_phys),
                      n * u->block_size);
            out += n * u->block_size;
            lba += n;
            sectors -= n;
        }
        return true;
    }

    bool usbmsc_write(hcpp::u64 lba, const void *buffer,
                      hcpp::u32 sectors) noexcept {
        Unit *u = active_unit();
        if (u == nullptr || buffer == nullptr || sectors == 0) return false;
        if (lba + sectors > u->blocks) return false;
        const hcpp::u32 chunk_blocks = dma_chunk / u->block_size;
        const hcpp::u8 *in = static_cast<const hcpp::u8 *>(buffer);
        while (sectors != 0) {
            const hcpp::u32 n =
                sectors < chunk_blocks ? sectors : chunk_blocks;
            copy_to(reinterpret_cast<volatile hcpp::u8 *>(u->data_phys),
                    in, n * u->block_size);
            if (!unit_blocks(*u, lba, n, u->data_phys, true)) {
                return false;
            }
            in += n * u->block_size;
            lba += n;
            sectors -= n;
        }
        return true;
    }

}  // namespace hype::usb::usbmsc
