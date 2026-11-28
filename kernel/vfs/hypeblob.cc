#include "kernel/vfs/hypeblob.h"

#include "hcpp/memory.hpp"
#include "kernel/ata.h"
#include "kernel/block/blockdev.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/vfs/hypefs_internal.h"

namespace {

    constexpr hcpp::u32 sector_size = 512;
    constexpr hcpp::u32 max_entries = 64;
    constexpr hcpp::u32 max_file_bytes = 1u << 20;
    constexpr hcpp::u32 name_length = 64;
    constexpr hcpp::u32 entry_size = 80;
    constexpr hcpp::u32 header_sector_max = 16;  // room for 64 entries
    constexpr hcpp::u32 read_chunk = 128;        // sectors per ATA command
    constexpr hcpp::u32 write_chunk = 128;
    constexpr hcpp::u32 path_capacity = 160;
    constexpr hcpp::u32 dir_list_cap = 64;
    constexpr hcpp::u32 walk_max_depth = hype::vfs::hypepath_max_components;

    // Sync-time cap for the on-disk region. tools/mkimage.py reserves the
    // full 8192-sector window at LBA 8258 for the blob (inside the GPT
    // "HypeAI data" partition), so 4 MiB always fits while covering the
    // full memfs (~2.5 MiB).
    constexpr hcpp::u64 region_max_bytes = 4ull << 20;

    // Static buffers: the payload is read once at boot, so BSS scratch space
    // is fine and avoids touching the heap. io_buf has a spare sector so
    // sync can zero-pad a file's tail sector without overflowing.
    hcpp::u8 header_buf[header_sector_max * sector_size];
    hcpp::u8 io_buf[max_file_bytes + sector_size];
    hcpp::u8 verify_buf[sector_size];

    // Files queued for serialization and one listing array per walk depth.
    struct PendingFile {
        char name[name_length];  // blob entry name ("u/apps/x.hxer")
        char path[hype::vfs::hypepath_max_length];  // full vfs path
        hcpp::u64 offset;        // sector-aligned blob offset, set at layout
        hcpp::u32 size;
    };

    PendingFile pending[max_entries];
    hype::vfs::FsStat dir_listing[walk_max_depth * dir_list_cap];
    hcpp::u32 pending_count = 0;
    bool blob_mounted = false;
    bool io_active = false;

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

    void put32(hcpp::u8* p, hcpp::u32 v) noexcept {
        p[0] = static_cast<hcpp::u8>(v & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
        p[2] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
        p[3] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
    }

    void put64(hcpp::u8* p, hcpp::u64 v) noexcept {
        put32(p, static_cast<hcpp::u32>(v));
        put32(p + 4, static_cast<hcpp::u32>(v >> 32));
    }

    // Appends `src` to the NUL-terminated `dst` (current length `len`),
    // keeping one byte for the terminator. Returns false on overflow.
    bool append(char* dst, hcpp::u32 cap, hcpp::u32& len,
                const char* src) noexcept {
        for (hcpp::u32 i = 0; src[i] != '\0'; ++i) {
            if (len + 1 >= cap) {
                dst[len] = '\0';
                return false;
            }
            dst[len++] = src[i];
        }
        dst[len] = '\0';
        return true;
    }

    bool read_sectors(hcpp::u64 offset, hcpp::u32 bytes, hcpp::u8* out) noexcept {
        hcpp::u64 lba = hype::vfs::hypeblob_data_lba + offset / sector_size;
        hcpp::u32 remaining = (bytes + sector_size - 1) / sector_size;
        while (remaining != 0) {
            const hcpp::u32 count =
                remaining < read_chunk ? remaining : read_chunk;
            if (!hype::block::block_read(lba, out, count)) {
                return false;
            }
            lba += count;
            out += count * sector_size;
            remaining -= count;
        }
        return true;
    }

    // Writes `bytes` (must be sector-aligned) at blob offset `offset`.
    bool write_sectors(hcpp::u64 offset, const hcpp::u8* data,
                       hcpp::u32 bytes) noexcept {
        hcpp::u64 lba = hype::vfs::hypeblob_data_lba + offset / sector_size;
        hcpp::u32 remaining = bytes / sector_size;
        while (remaining != 0) {
            const hcpp::u32 count =
                remaining < write_chunk ? remaining : write_chunk;
            if (!hype::block::block_write(lba, data, count)) {
                return false;
            }
            lba += count;
            data += count * sector_size;
            remaining -= count;
        }
        return true;
    }

    // Read-after-write verify: re-reads every written sector at blob offset
    // `offset` and compares it against `data` byte for byte.
    bool verify_sectors(hcpp::u64 offset, const hcpp::u8* data,
                        hcpp::u32 bytes) noexcept {
        const hcpp::u64 lba =
            hype::vfs::hypeblob_data_lba + offset / sector_size;
        const hcpp::u32 sectors = bytes / sector_size;
        for (hcpp::u32 s = 0; s < sectors; ++s) {
            if (!hype::block::block_read(lba + s, verify_buf, 1)) {
                return false;
            }
            if (hcpp::compare(verify_buf, data + s * sector_size,
                              sector_size) != 0) {
                return false;
            }
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

    // Mount routing: a leading "<disk>/" component ("u", "s" or "t") mounts
    // the entry at "HYPEOS://<DISK>/<rest>" (e.g. "u/apps/x.hxer" ->
    // "HYPEOS://U/apps/x.hxer"); anything else goes to
    // "HYPEOS://S/data/<name>". Intermediate directories are created.
    bool mount_file(const char* name, const hcpp::u8* data,
                    hcpp::u32 size) noexcept {
        char path[path_capacity];
        const char* prefix = "HYPEOS://S/data/";
        char routed[12];
        hcpp::u32 name_start = 0;
        if ((name[0] == 'u' || name[0] == 's' || name[0] == 't') &&
            name[1] == '/' && name[2] != '\0') {
            for (hcpp::u32 i = 0; i < sizeof(routed); ++i) routed[i] = '\0';
            const char base[] = "HYPEOS://S/";
            for (hcpp::u32 i = 0; base[i] != '\0'; ++i) routed[i] = base[i];
            routed[9] = name[0] == 'u' ? 'U' : (name[0] == 's' ? 'S' : 'T');
            prefix = routed;
            name_start = 2;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 i = 0; prefix[i] != '\0'; ++i) path[pos++] = prefix[i];
        for (hcpp::u32 i = name_start; name[i] != '\0'; ++i) {
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

    // Recursive memfs walk for hypeblob_sync(). `dir_path` is a full vfs
    // path ("HYPEOS://U/wallpapers"), `prefix` the blob-name prefix
    // accumulated so far ("u/wallpapers/"). Each file becomes a PendingFile
    // whose blob name mirrors the mount routing rules above.
    void collect_dir(const char* dir_path, const char* prefix,
                     hcpp::u32 depth) noexcept {
        if (depth >= walk_max_depth || pending_count >= max_entries) return;
        hype::vfs::FsStat* listing = dir_listing + depth * dir_list_cap;
        const hcpp::u32 count =
            hype::vfs::fs_list(dir_path, listing, dir_list_cap);
        const hcpp::u32 bound = count < dir_list_cap ? count : dir_list_cap;
        for (hcpp::u32 i = 0; i < bound && pending_count < max_entries; ++i) {
            const hype::vfs::FsStat& st = listing[i];
            if ((st.flags & hype::vfs::FS_SYS) != 0) continue;
            char child_path[hype::vfs::hypepath_max_length];
            hcpp::u32 len = 0;
            if (!append(child_path, sizeof(child_path), len, dir_path) ||
                !append(child_path, sizeof(child_path), len, "/") ||
                !append(child_path, sizeof(child_path), len, st.name)) {
                continue;
            }
            char child_name[name_length];
            len = 0;
            if (!append(child_name, sizeof(child_name), len, prefix) ||
                !append(child_name, sizeof(child_name), len, st.name)) {
                continue;
            }
            if ((st.flags & hype::vfs::FS_DIR) != 0) {
                if (!append(child_name, sizeof(child_name), len, "/")) {
                    continue;
                }
                collect_dir(child_path, child_name, depth + 1);
                continue;
            }
            if (st.size == 0 || st.size > max_file_bytes) continue;
            if (!name_valid(child_name)) continue;
            PendingFile& item = pending[pending_count];
            len = 0;
            append(item.name, sizeof(item.name), len, child_name);
            len = 0;
            append(item.path, sizeof(item.path), len, child_path);
            item.offset = 0;
            item.size = st.size;
            ++pending_count;
        }
    }

}

namespace hype::vfs {

    BlobMount hypeblob_mount() noexcept {
        BlobMount out{};
        out.ata_ok = hype::block::block_present();
        if (!out.ata_ok) return out;

        // Logs "ata model=... sectors=N"; informational only.
        hype::ata::AtaInfo ident{};
        hype::block::block_identify(ident);

        if (!hype::block::block_read(hypeblob_data_lba, header_buf, 1)) {
            return out;
        }
        out.lba_ok = true;
        if (hcpp::compare(header_buf, "HYPEDATA", 8) != 0) return out;
        out.magic_ok = true;

        const hcpp::u32 count = le32(header_buf + 8);
        const hcpp::u32 header_sectors = le32(header_buf + 12);
        if (count > max_entries || header_sectors == 0 ||
            header_sectors > header_sector_max ||
            16 + count * entry_size > header_sectors * sector_size) {
            return out;
        }
        // A valid header (even a 0-entry one) means the region is ours to
        // rewrite: enable hypeblob_sync() from here on.
        blob_mounted = true;
        if (header_sectors > 1 &&
            !hype::block::block_read(hypeblob_data_lba + 1,
                                     header_buf + sector_size,
                                     header_sectors - 1)) {
            return out;
        }

        fs_mkdir("HYPEOS://S/data");
        // mount_file() routes through fs_mkdir/fs_write which mark the
        // memfs dirty; io_active keeps the write-through auto-sync from
        // re-entering the blob region while we are still reading it.
        io_active = true;
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
        io_active = false;
        return out;
    }

    bool hypeblob_sync() noexcept {
        // Guard: never write before a valid on-disk blob has been seen, so
        // boot-time fs_write()s (hxer installs, seeds) cannot clobber the
        // region before mount.
        if (!blob_mounted || !hype::block::block_present() || io_active) {
            return false;
        }
        io_active = true;

        // Collect persistable files. U:* -> "u/<path>", T:* -> "t/<path>",
        // S:/data subtree -> "<path>" (the mount routing's inverse). Other
        // S: files are FS_SYS or boot-regenerated and are skipped.
        pending_count = 0;
        collect_dir("HYPEOS://U", "u/", 0);
        collect_dir("HYPEOS://S/data", "", 0);
        collect_dir("HYPEOS://T", "t/", 0);

        // Layout pass, matching tools/mkimage.py: entries at offset 16,
        // bodies sector-aligned starting after the header. Truncate the
        // entry list until it fits the region cap.
        hcpp::u32 used = pending_count;
        hcpp::u32 header_sectors;
        hcpp::u64 cursor;
        for (;;) {
            header_sectors =
                (16 + used * entry_size + sector_size - 1) / sector_size;
            cursor = static_cast<hcpp::u64>(header_sectors) * sector_size;
            hcpp::u32 fit = 0;
            for (hcpp::u32 i = 0; i < used; ++i) {
                const hcpp::u64 end =
                    cursor + (static_cast<hcpp::u64>(pending[i].size) +
                              sector_size - 1) /
                                 sector_size * sector_size;
                if (end > region_max_bytes) break;
                pending[i].offset = cursor;
                cursor = end;
                ++fit;
            }
            if (fit == used) break;
            used = fit;
        }

        bool ok = true;
        // Bodies first, header last: a crash mid-sync leaves the previous
        // header intact rather than pointing at half-written data.
        for (hcpp::u32 i = 0; i < used && ok; ++i) {
            const PendingFile& item = pending[i];
            hcpp::u32 got = 0;
            if (hype::vfs::fs_read(item.path, io_buf, max_file_bytes, &got) !=
                    hype::vfs::FsStatus::Ok ||
                got != item.size) {
                ok = false;
                break;
            }
            const hcpp::u32 sectors =
                (item.size + sector_size - 1) / sector_size;
            hcpp::set(io_buf + item.size, 0,
                      sectors * sector_size - item.size);
            ok = write_sectors(item.offset, io_buf,
                               sectors * sector_size) &&
                 verify_sectors(item.offset, io_buf,
                                sectors * sector_size);
        }

        if (ok) {
            hcpp::set(header_buf, 0,
                      header_sectors * sector_size);
            hcpp::copy(header_buf, "HYPEDATA", 8);
            put32(header_buf + 8, used);
            put32(header_buf + 12, header_sectors);
            for (hcpp::u32 i = 0; i < used; ++i) {
                hcpp::u8* entry = header_buf + 16 + i * entry_size;
                hcpp::u32 len = 0;
                append(reinterpret_cast<char*>(entry), name_length, len,
                       pending[i].name);
                put64(entry + name_length, pending[i].offset);
                put64(entry + name_length + 8, pending[i].size);
            }
            ok = write_sectors(0, header_buf,
                               header_sectors * sector_size) &&
                 verify_sectors(0, header_buf,
                                header_sectors * sector_size);
        }
        if (!hype::block::block_flush()) ok = false;
        io_active = false;
        if (ok) {
            // Verified write-back landed: drop the memfs dirty bits so
            // the write-through layer does not re-sync the same state.
            hype::vfs::detail::fs_mark_clean();
        }
        return ok;
    }

    bool hypeblob_io_active() noexcept { return io_active; }

}
