#include "kernel/codecapps/archive/archive.h"
#include "kernel/codecapps/arena.h"
#include "kernel/ztext/ztext.h"

// ZIP (PKWARE APPNOTE): probe `PK\x03\x04` (or `PK\x05\x06` empty archive),
// locate the end-of-central-directory record by scanning the tail, walk the
// central directory entries (`PK\x01\x02`) into an ArchiveList, resolve each
// member's payload through its local file header (`PK\x03\x04`), and extract
// method 0 (stored) / method 8 (deflate via ZText inflate) with crc32 check.

namespace hype::codec::archive {

    namespace {

        constexpr hcpp::u32 zip_sig_local = 0x04034B50;
        constexpr hcpp::u32 zip_sig_central = 0x02014B50;
        constexpr hcpp::u32 zip_sig_eocd = 0x06054B50;
        constexpr hcpp::size_t zip_eocd_len = 22;
        constexpr hcpp::size_t zip_max_comment = 0xFFFF;

        hcpp::u16 rd16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>(p[0] | (static_cast<hcpp::u16>(p[1]) << 8));
        }

        hcpp::u32 rd32(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }

        bool bounds(hcpp::size_t offset, hcpp::size_t length, hcpp::size_t total) noexcept {
            return offset <= total && length <= total - offset;
        }

        // Resolve the payload offset of a member through its local header.
        bool local_data_offset(const hcpp::u8 *src, hcpp::size_t size, hcpp::u32 lh_off,
                               hcpp::size_t &data_off) noexcept {
            if (!bounds(lh_off, 30, size) || rd32(src + lh_off) != zip_sig_local)
                return false;
            const hcpp::u16 nlen = rd16(src + lh_off + 26);
            const hcpp::u16 elen = rd16(src + lh_off + 28);
            const hcpp::size_t off = static_cast<hcpp::size_t>(lh_off) + 30 + nlen + elen;
            if (!bounds(off, 0, size)) return false;
            data_off = off;
            return true;
        }

    }  // namespace

    bool zip_probe(const hcpp::u8 *b, hcpp::size_t n) noexcept {
        if (b == nullptr || n < 4) return false;
        const hcpp::u32 sig = rd32(b);
        return sig == zip_sig_local || sig == zip_sig_eocd;
    }

    CodecStatus zip_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                           Arena &arena) noexcept {
        ArchiveList *list = static_cast<ArchiveList *>(output);
        if (src == nullptr || list == nullptr || !zip_probe(src, size)) return CodecStatus::BadInput;
        list->count = 0;
        if (list->entries == nullptr) {
            if (list->capacity == 0) list->capacity = 64;
            list->entries = static_cast<ArchiveEntry *>(
                arena_alloc(arena, list->capacity * sizeof(ArchiveEntry), 8));
            if (list->entries == nullptr) return CodecStatus::NoSpace;
        }
        // End-of-central-directory: scan the tail backwards for PK\x05\x06.
        const hcpp::size_t window =
            size < zip_eocd_len + zip_max_comment ? 0 : size - zip_eocd_len - zip_max_comment;
        hcpp::size_t eocd = size;
        for (hcpp::size_t i = size; i-- > window;) {
            if (i + zip_eocd_len <= size && rd32(src + i) == zip_sig_eocd &&
                i + zip_eocd_len + rd16(src + i + 20) <= size) {
                eocd = i;
                break;
            }
        }
        if (eocd == size) return CodecStatus::BadInput;
        const hcpp::u16 total = rd16(src + eocd + 10);
        const hcpp::u32 cd_off = rd32(src + eocd + 16);
        if (!bounds(cd_off, 0, eocd)) return CodecStatus::BadInput;
        hcpp::size_t pos = cd_off;
        hcpp::size_t count = 0;
        while (count < total && count < list->capacity) {
            if (!bounds(pos, 46, eocd) || rd32(src + pos) != zip_sig_central) break;
            const hcpp::u16 flags = rd16(src + pos + 8);
            const hcpp::u16 method = rd16(src + pos + 10);
            const hcpp::u32 crc = rd32(src + pos + 16);
            const hcpp::u32 csize = rd32(src + pos + 20);
            const hcpp::u32 usize = rd32(src + pos + 24);
            const hcpp::u16 nlen = rd16(src + pos + 28);
            const hcpp::u16 elen = rd16(src + pos + 30);
            const hcpp::u16 clen = rd16(src + pos + 32);
            const hcpp::u32 lh_off = rd32(src + pos + 42);
            const hcpp::size_t name_off = pos + 46;
            if (!bounds(name_off, static_cast<hcpp::size_t>(nlen) + elen + clen, eocd))
                return CodecStatus::BadInput;
            hcpp::size_t data_off = 0;
            if (!local_data_offset(src, size, lh_off, data_off))
                return CodecStatus::BadInput;
            ArchiveEntry &e = list->entries[count++];
            e.name = reinterpret_cast<const char *>(src + name_off);
            e.name_len = nlen;
            e.method = static_cast<hcpp::u8>(method);
            e.flags = 0;
            if (nlen != 0 && src[name_off + nlen - 1] == '/')
                e.flags |= archive_entry_dir;
            if ((flags & 1) != 0 || (method != 0 && method != 8))
                e.flags |= archive_entry_unsupported;
            e.offset = static_cast<hcpp::u32>(data_off);
            e.size = usize;
            e.packed = csize;
            e.crc32 = crc;
            pos = name_off + nlen + elen + clen;
        }
        list->count = count;
        return count == total ? CodecStatus::Ok
             : count != 0       ? CodecStatus::Ok
                                : CodecStatus::BadInput;
    }

    CodecStatus zip_extract(const hcpp::u8 *src, hcpp::size_t size,
                            const ArchiveEntry &entry, hcpp::u8 *dst,
                            hcpp::size_t dst_cap, hcpp::size_t &out_len) noexcept {
        out_len = 0;
        if (src == nullptr || dst == nullptr) return CodecStatus::BadInput;
        if ((entry.flags & archive_entry_dir) != 0) return CodecStatus::BadInput;
        if ((entry.flags & archive_entry_unsupported) != 0)
            return CodecStatus::Unsupported;
        if (!bounds(entry.offset, entry.packed, size) ||
            !bounds(entry.offset, entry.size, size))
            return CodecStatus::BadInput;
        if (entry.method == 0) {
            if (entry.size > dst_cap) return CodecStatus::NoSpace;
            const hcpp::u32 n = entry.size < entry.packed ? entry.size : entry.packed;
            for (hcpp::u32 i = 0; i < n; ++i) dst[i] = src[entry.offset + i];
            out_len = n;
        } else if (entry.method == 8) {
            if (entry.size > dst_cap) return CodecStatus::NoSpace;
            if (!ztext::ztext_inflate(dst, dst_cap, src + entry.offset, entry.packed,
                                      out_len))
                return CodecStatus::BadInput;
        } else {
            return CodecStatus::Unsupported;
        }
        if (entry.crc32 != 0 && ztext::ztext_crc32(dst, out_len) != entry.crc32)
            return CodecStatus::BadInput;
        return CodecStatus::Ok;
    }

    const ArchiveEntry *archive_find(const ArchiveList &list, const char *name) noexcept {
        hcpp::size_t want = 0;
        while (name[want] != '\0') ++want;
        for (hcpp::size_t i = 0; i < list.count; ++i) {
            const ArchiveEntry &e = list.entries[i];
            if (e.name_len != want) continue;
            hcpp::size_t k = 0;
            while (k < want && e.name[k] == name[k]) ++k;
            if (k == want) return &e;
        }
        return nullptr;
    }

}  // namespace hype::codec::archive
