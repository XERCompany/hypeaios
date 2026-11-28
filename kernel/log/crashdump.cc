#include "kernel/log/crashdump.h"

#include "hcpp/memory.hpp"
#include "kernel/block/blockdev.h"
#include "kernel/log/kring.h"
#include "kernel/panic.h"
#include "kernel/serial.hpp"
#include "kernel/vfs/hypeblob.h"
#include "kernel/vfs/hypefs.h"

namespace hype {

    namespace {

        constexpr char crash_path[] = "HYPEOS://U/crash.log";
        constexpr char crash_test_path[] = "HYPEOS://U/crash-test.log";
        constexpr hcpp::u32 sector_size = 512;

        // Raw-LBA fallback scratch region: the HYPEDATA blob starts at
        // hypeblob_data_lba (8258) and hypeblob.cc caps serialization at
        // a 4 MiB window (8192 sectors), so the first sector past it is
        // 8258 + 8192 = 16450. mkimage.py keeps the gap between the blob
        // and the ESP inside the GPT "HypeAI data" partition, making 132
        // sectors here a safe parking spot that the blob can never grow
        // into and no filesystem can claim.
        constexpr hcpp::u64 crash_lba = hype::vfs::hypeblob_data_lba + 8192;
        constexpr hcpp::u32 crash_lba_sectors = 132;

        // Static staging: header + register frame is well under 2 KiB,
        // so the ring dump always has the full 64 KiB available.
        constexpr hcpp::u32 dump_head_bytes = 2048;
        hcpp::u8 dump_buf[kring::kring_capacity + dump_head_bytes];
        hcpp::u32 dump_len = 0;

        void dput(char c) noexcept {
            if (dump_len < sizeof(dump_buf)) {
                dump_buf[dump_len++] = static_cast<hcpp::u8>(c);
            }
        }

        void dappend(const char* text) noexcept {
            if (text == nullptr) {
                return;
            }
            while (*text != '\0') {
                dput(*text++);
            }
        }

        void dappend(hcpp::StringView text) noexcept {
            for (hcpp::size_t i = 0; i < text.size(); ++i) {
                dput(text[i]);
            }
        }

        void dappend_dec(hcpp::u64 value) noexcept {
            char tmp[20];
            hcpp::u32 n = 0;
            if (value == 0) {
                dput('0');
                return;
            }
            while (value != 0) {
                tmp[n++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (n != 0) {
                dput(tmp[--n]);
            }
        }

        void dappend_hex(hcpp::u64 value) noexcept {
            dappend("0x");
            for (int shift = 60; shift >= 0; shift -= 4) {
                const hcpp::u8 digit =
                    static_cast<hcpp::u8>((value >> shift) & 0xF);
                dput(digit < 10
                         ? static_cast<char>('0' + digit)
                         : static_cast<char>('A' + digit - 10));
            }
        }

        void dfield(const char* name, hcpp::u64 value) noexcept {
            dappend(name);
            dput('=');
            dappend_hex(value);
        }

        void ring_chunk(const void* data, hcpp::u32 size, void* /*ctx*/) noexcept {
            const hcpp::u32 room = sizeof(dump_buf) > dump_len
                                       ? static_cast<hcpp::u32>(
                                             sizeof(dump_buf) - dump_len)
                                       : 0;
            const hcpp::u32 take = size < room ? size : room;
            if (take != 0) {
                hcpp::copy(dump_buf + dump_len, data, take);
                dump_len += take;
            }
        }

        hcpp::u32 build_dump(hcpp::StringView reason,
                             const InterruptRegisters* regs) noexcept {
            dump_len = 0;
            dappend("HYPEAI OS CRASH DUMP\n");
            dappend("reason=");
            dappend(reason);
            dput('\n');
            if (regs != nullptr) {
                dappend("vector=");
                dappend_dec(regs->vector);
                dappend(" name=");
                dappend(exception_name(regs->vector));
                dappend(" error=");
                dappend_hex(regs->error);
                dput('\n');
                dfield("rip", regs->rip);
                dput(' ');
                dfield("cs", regs->cs);
                dput(' ');
                dfield("rflags", regs->rflags);
                dput('\n');
                dfield("rsp", regs->rsp);
                dput(' ');
                dfield("ss", regs->ss);
                dput('\n');
                dfield("rax", regs->rax);
                dput(' ');
                dfield("rbx", regs->rbx);
                dput(' ');
                dfield("rcx", regs->rcx);
                dput(' ');
                dfield("rdx", regs->rdx);
                dput('\n');
                dfield("rsi", regs->rsi);
                dput(' ');
                dfield("rdi", regs->rdi);
                dput(' ');
                dfield("rbp", regs->rbp);
                dput('\n');
                dfield("r8", regs->r8);
                dput(' ');
                dfield("r9", regs->r9);
                dput(' ');
                dfield("r10", regs->r10);
                dput(' ');
                dfield("r11", regs->r11);
                dput('\n');
                dfield("r12", regs->r12);
                dput(' ');
                dfield("r13", regs->r13);
                dput(' ');
                dfield("r14", regs->r14);
                dput(' ');
                dfield("r15", regs->r15);
                dput('\n');
            }
            dappend("kring bytes=");
            dappend_dec(kring::kring_size());
            dappend(" written=");
            dappend_dec(kring::kring_written());
            dappend("\n== serial/klog ring ==\n");
            kring::kring_copy_to(&ring_chunk, nullptr);
            dappend("\n== end of dump ==\n");
            return dump_len;
        }

        bool persist_raw(const hcpp::u8* data, hcpp::u32 len) noexcept {
            if (!hype::block::block_present()) {
                return false;
            }
            const hcpp::u32 sectors = (len + sector_size - 1) / sector_size;
            if (sectors == 0 || sectors > crash_lba_sectors) {
                return false;
            }
            // Zero-pad the tail sector so the on-disk record has no
            // stale bytes after the dump text.
            hcpp::set(dump_buf + len, 0, sectors * sector_size - len);
            if (!hype::block::block_write(crash_lba, data, sectors)) {
                return false;
            }
            hype::block::block_flush();
            serial::write("crash dump raw lba=");
            serial::write_dec32(static_cast<hcpp::u32>(crash_lba));
            serial::write(" sectors=");
            serial::write_dec32(sectors);
            serial::write("\r\n");
            return true;
        }

    }  // namespace

    void crashdump_write(hcpp::StringView reason,
                         const InterruptRegisters* regs) noexcept {
        if (reason.empty()) {
            reason = "unknown";
        }
        // Stamps the ring tail so the dump itself shows where the crash
        // record begins.
        kring::kring_mark(reason);
        serial::write("crash dump reason=");
        serial::write(reason);
        serial::write("\r\n");
        const hcpp::u32 len = build_dump(reason, regs);
        bool saved = false;
        // VFS path: only when the memfs is initialized and no blob
        // serialization is mid-flight (a panic inside hypeblob_sync()
        // must not re-enter it — fs_write() would survive, but the
        // follow-up sync would corrupt the in-progress image).
        const bool vfs_up = hype::vfs::fs_disk_count() != 0 &&
                            !hype::vfs::hypeblob_io_active();
        if (vfs_up && hype::vfs::fs_write(crash_path, dump_buf, len) ==
                          hype::vfs::FsStatus::Ok) {
            saved = hype::vfs::hypeblob_sync();
        }
        // Raw-LBA fallback: also runs when the memfs write or the blob
        // sync failed — the scratch region does not overlap blob data.
        if (!saved) {
            saved = persist_raw(dump_buf, len);
        }
        serial::write("crash dump=");
        serial::write_line(saved ? "saved" : "vfs-offline");
    }

    bool crashdump_test() noexcept {
        if (hype::vfs::fs_disk_count() == 0) {
            serial::write_line("crash test=skip");
            return false;
        }
        char probe[160];
        hcpp::u32 len = 0;
        const char* head = "HYPEAI OS CRASH TEST\nkring bytes=";
        while (head[len] != '\0') {
            probe[len] = head[len];
            ++len;
        }
        hcpp::u32 size = kring::kring_size();
        char digits[10];
        hcpp::u32 n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (size % 10));
            size /= 10;
        } while (size != 0 && n < sizeof(digits));
        while (n != 0) {
            probe[len++] = digits[--n];
        }
        probe[len++] = '\n';
        const hype::vfs::FsStatus st =
            hype::vfs::fs_write(crash_test_path, probe, len);
        bool ok = st == hype::vfs::FsStatus::Ok;
        if (ok) {
            ok = hype::vfs::hypeblob_sync();
        }
        serial::write("crash test=");
        serial::write_line(ok ? "ok" : "fail");
        // Keep U: clean: remove the probe file and flush the delete.
        hype::vfs::fs_delete(crash_test_path);
        hype::vfs::hypeblob_sync();
        return ok;
    }

}  // namespace hype
