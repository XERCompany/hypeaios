#include "kernel/vfs/hypeblob.h"

#include "hcpp/memory.hpp"
#include "kernel/ata.h"
#include "kernel/vfs/hypefs.h"

namespace {

    constexpr hcpp::u32 sector_size = 512;
    constexpr hcpp::u32 max_entries = 64;
    constexpr hcpp::u32 max_file_bytes = 1u << 20;
    constexpr hcpp::u32 name_length = 64;
    constexpr hcpp::u32 entry_size = 80;
    constexpr hcpp::u32 header_sector_max = 16;  // room for 64 entries
    constexpr hcpp::u32 read_chunk = 128;        // sectors per ATA command
    constexpr hcpp::u32 path_capacity = 160;

    // Static buffers: the payload is read once at boot, so BSS scratch space
    // is fine and avoids touching the heap.
    hcpp::u8 header_buf[header_sector_max * sector_size];
    hcpp::u8 io_buf[max_file_bytes];

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

    bool read_sectors(hcpp::u64 offset, hcpp::u32 bytes, hcpp::u8* out) noexcept {
        hcpp::u64 lba = hype::vfs::hypeblob_data_lba + offset / sector_size;
        hcpp::u32 remaining = (bytes + sector_size - 1) / sector_size;
        while (remaining != 0) {
            const hcpp::u32 count =
                remaining < read_chunk ? remaining : read_chunk;
            if (!hype::ata::ata_pio_read(static_cast<hcpp::u32>(lba), out,
                                        static_cast<hcpp::u8>(count))) {
                return false;
            }
            lba += count;
            out += count * sector_size;
            remaining -= count;
        }
        return true;
    }

    bool name_valid(const char* name) noexcept {
        if (name[0] == '\0' || name[0] == '/') return false;
        bool dot_run = false;
        hcpp::u32 length = 0;
        for (hcpp::u32 i = 0; name[i] != '\0' && i < name_length; ++i) {
            const char c = name[i];
            if (c == '/' && name[i + 1] == '/') return false;
            if (c == '.') {
                if (dot_run) return false;  // reject ".." path escapes
                dot_run = true;
            } else if (c != '/' && c != '.') {
                dot_run = false;
            }
            if (c == '\\' || static_cast<hcpp::u8>(c) < 0x21 ||
                static_cast<hcpp::u8>(c) > 0x7E) {
                return false;
            }
            length = i + 1;
        }
        return length > 0 && length < name_length;
    }

    // Builds "HYPEOS://S/data/<name>" and creates intermediate directories.
    bool mount_file(const char* name, const hcpp::u8* data,
                    hcpp::u32 size) noexcept {
        char path[path_capacity];
        const char prefix[] = "HYPEOS://S/data/";
        hcpp::u32 pos = 0;
        for (hcpp::u32 i = 0; prefix[i] != '\0'; ++i) path[pos++] = prefix[i];
        for (hcpp::u32 i = 0; name[i] != '\0'; ++i) {
            if (pos + 2 >= path_capacity) return false;
            if (name[i] == '/') {
                path[pos] = '\0';
                hype::vfs::fs_mkdir(path);  // Exists is fine
            }
            path[pos++] = name[i];
        }
        path[pos] = '\0';
        return hype::vfs::fs_write(path, data, size) == hype::vfs::FsStatus::Ok;
    }

}

namespace hype::vfs {

    BlobMount hypeblob_mount() noexcept {
        BlobMount out{};
        out.ata_ok = hype::ata::ata_present();
        if (!out.ata_ok) return out;

        if (!hype::ata::ata_pio_read(hypeblob_data_lba, header_buf, 1)) {
            return out;
        }
        out.lba_ok = true;
        if (hcpp::compare(header_buf, "HYPEDATA", 8) != 0) return out;
        out.magic_ok = true;

        const hcpp::u32 count = le32(header_buf + 8);
        const hcpp::u32 header_sectors = le32(header_buf + 12);
        if (count == 0 || count > max_entries || header_sectors == 0 ||
            header_sectors > header_sector_max ||
            16 + count * entry_size > header_sectors * sector_size) {
            return out;
        }
        if (header_sectors > 1 &&
            !hype::ata::ata_pio_read(hypeblob_data_lba + 1,
                                     header_buf + sector_size,
                                     static_cast<hcpp::u8>(header_sectors - 1))) {
            return out;
        }

        fs_mkdir("HYPEOS://S/data");
        for (hcpp::u32 i = 0; i < count; ++i) {
            hcpp::u8* entry = header_buf + 16 + i * entry_size;
            const char* name = reinterpret_cast<const char*>(entry);
            const hcpp::u64 offset = le64(entry + name_length);
            const hcpp::u64 size = le64(entry + name_length + 8);
            entry[name_length - 1] = '\0';
            if (!name_valid(name)) continue;
            if (size == 0 || size > max_file_bytes) continue;
            if ((offset % sector_size) != 0 || offset == 0) continue;
            if (!read_sectors(offset, static_cast<hcpp::u32>(size), io_buf)) {
                continue;
            }
            if (mount_file(name, io_buf, static_cast<hcpp::u32>(size))) {
                ++out.files;
                out.bytes += size;
            }
        }
        return out;
    }

}
