#include "kernel/codecapps/archive/archive.h"
#include "kernel/codecapps/arena.h"

// POSIX ustar TAR: 512-byte header blocks {name, octal size, typeflag,
// "ustar" magic @257, optional GNU 'L' longname block} + data padded to 512.
// Entry names point into the source buffer (longname blocks included).

namespace hype::codec::archive {

    namespace {

        bool block_zero(const hcpp::u8 *p) noexcept {
            for (hcpp::size_t i = 0; i < 512; ++i)
                if (p[i] != 0) return false;
            return true;
        }

        bool tag_eq(const hcpp::u8 *p, const char *t, hcpp::size_t n) noexcept {
            for (hcpp::size_t i = 0; i < n; ++i)
                if (p[i] != static_cast<hcpp::u8>(t[i])) return false;
            return true;
        }

        hcpp::u64 octal(const hcpp::u8 *p, hcpp::size_t n) noexcept {
            hcpp::size_t i = 0;
            while (i < n && (p[i] == ' ' || p[i] == '\0')) ++i;
            hcpp::u64 v = 0;
            while (i < n && p[i] >= '0' && p[i] <= '7') {
                v = (v << 3) | static_cast<hcpp::u8>(p[i] - '0');
                ++i;
            }
            return v;
        }

        hcpp::size_t str_len(const hcpp::u8 *p, hcpp::size_t max) noexcept {
            hcpp::size_t n = 0;
            while (n < max && p[n] != '\0') ++n;
            return n;
        }

        bool header_checksum(const hcpp::u8 *p) noexcept {
            hcpp::u32 sum = 0;
            for (hcpp::size_t i = 0; i < 512; ++i)
                sum += (i >= 148 && i < 156) ? 0x20 : p[i];
            return sum == octal(p + 148, 8);
        }

    }  // namespace

    bool tar_probe(const hcpp::u8 *b, hcpp::size_t n) noexcept {
        return b != nullptr && n >= 512 && tag_eq(b + 257, "ustar", 5);
    }

    CodecStatus tar_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        ArchiveList *list = static_cast<ArchiveList *>(output);
        if (src == nullptr || list == nullptr || !tar_probe(src, size))
            return CodecStatus::BadInput;
        list->count = 0;
        if (list->entries == nullptr) {
            if (list->capacity == 0) list->capacity = 64;
            list->entries = static_cast<ArchiveEntry *>(
                arena_alloc(arena, list->capacity * sizeof(ArchiveEntry), 8));
            if (list->entries == nullptr) return CodecStatus::NoSpace;
        }
        hcpp::size_t pos = 0;
        const char *longname = nullptr;
        hcpp::u16 longname_len = 0;
        while (pos + 512 <= size && list->count < list->capacity) {
            const hcpp::u8 *hdr = src + pos;
            if (block_zero(hdr)) break;
            if (!header_checksum(hdr)) return CodecStatus::BadInput;
            const hcpp::u64 dsize = octal(hdr + 124, 12);
            const hcpp::u8 type = hdr[156];
            const hcpp::size_t padded =
                512 + ((static_cast<hcpp::size_t>(dsize) + 511) & ~static_cast<hcpp::size_t>(511));
            if (dsize > size - pos || padded > size - pos) return CodecStatus::BadInput;
            if (type == 'L') {
                // GNU longname: the data block holds the real NUL-terminated
                // name; the next header carries the actual entry.
                longname = reinterpret_cast<const char *>(hdr + 512);
                longname_len = static_cast<hcpp::u16>(
                    str_len(hdr + 512, dsize < archive_max_name ? dsize : archive_max_name));
                pos += padded;
                continue;
            }
            if (type == 'x' || type == 'g' || type == 'K') {
                // pax extended / global header, longlink: skip payload.
                pos += padded;
                continue;
            }
            ArchiveEntry &e = list->entries[list->count++];
            if (longname != nullptr) {
                e.name = longname;
                e.name_len = longname_len;
                longname = nullptr;
            } else {
                e.name = reinterpret_cast<const char *>(hdr);
                e.name_len = static_cast<hcpp::u16>(str_len(hdr, 100));
            }
            e.method = type;
            e.flags = type == '5' ? archive_entry_dir : 0;
            e.offset = static_cast<hcpp::u32>(pos + 512);
            e.size = static_cast<hcpp::u32>(dsize);
            e.packed = static_cast<hcpp::u32>(dsize);
            e.crc32 = 0;
            pos += padded;
        }
        return list->count != 0 ? CodecStatus::Ok : CodecStatus::BadInput;
    }

}  // namespace hype::codec::archive
