#include "kernel/vfs/fat32.h"

#include "hcpp/memory.hpp"
#include "kernel/block/blockdev.h"

namespace {

    constexpr hcpp::u32 sector_size = 512;
    constexpr hcpp::u32 max_spc = 64;          // cap: 32 KiB cluster buffer
    constexpr hcpp::u32 fat_eps = 128;         // FAT32 entries per sector
    constexpr hcpp::u32 dir_eps = 16;          // dir entries per sector
    // FAT entries mask to 28 bits; chain ends on any value that is not a
    // valid data cluster (>= 0x0FFFFFF8 EOC, or out of range).
    constexpr hcpp::u32 eof_mark = 0x0FFFFFFFu;
    constexpr hcpp::u32 name_max = 64;         // decoded-name scratch
    constexpr hcpp::u32 lfn_segs_max = 20;     // 255 UCS-2 chars max
    constexpr hcpp::u32 gpt_scan_max = 4;      // v1: first N GPT entries
    constexpr hcpp::u64 invalid_lba = ~0ull;

    // On-disk field offsets (32-byte dir entry, BPB, MBR, GPT).
    constexpr hcpp::u32 de_attr = 11;
    constexpr hcpp::u32 de_cluster_hi = 20;
    constexpr hcpp::u32 de_cluster_lo = 26;
    constexpr hcpp::u32 de_size = 28;
    constexpr hcpp::u8 attr_lfn = 0x0F;
    constexpr hcpp::u8 attr_label = 0x08;
    constexpr hcpp::u8 attr_dir = 0x10;
    constexpr hcpp::u8 attr_file = 0x20;

    struct Volume {
        bool mounted;
        hcpp::u64 part_lba;
        hcpp::u64 fat_lba;
        hcpp::u64 data_lba;
        hcpp::u32 spc;
        hcpp::u32 fat_count;
        hcpp::u32 fat_size;      // sectors per FAT
        hcpp::u32 root_cluster;
        hcpp::u32 total_clusters;
        hcpp::u32 alloc_hint;    // first cluster to try on free-scan
    };

    Volume vol{};
    hcpp::u8 sec_buf[sector_size];
    hcpp::u8 fat_buf[sector_size];
    hcpp::u64 fat_buf_lba = invalid_lba;       // which FAT sector is cached
    hcpp::u8 cluster_buf[max_spc * sector_size];

    hcpp::u16 le16(const hcpp::u8* p) noexcept {
        return static_cast<hcpp::u16>(p[0]) |
               static_cast<hcpp::u16>(static_cast<hcpp::u16>(p[1]) << 8);
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

    void put16(hcpp::u8* p, hcpp::u16 v) noexcept {
        p[0] = static_cast<hcpp::u8>(v & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
    }

    void put32(hcpp::u8* p, hcpp::u32 v) noexcept {
        p[0] = static_cast<hcpp::u8>(v & 0xFF);
        p[1] = static_cast<hcpp::u8>((v >> 8) & 0xFF);
        p[2] = static_cast<hcpp::u8>((v >> 16) & 0xFF);
        p[3] = static_cast<hcpp::u8>((v >> 24) & 0xFF);
    }

    char ascii_up(char c) noexcept {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }

    bool name_equal_ci(const char* a, const char* b) noexcept {
        hcpp::u32 i = 0;
        while (a[i] != '\0' && b[i] != '\0') {
            if (ascii_up(a[i]) != ascii_up(b[i])) return false;
            ++i;
        }
        return a[i] == b[i];
    }

    hcpp::u32 text_len(const char* s, hcpp::u32 cap) noexcept {
        hcpp::u32 n = 0;
        while (n < cap && s[n] != '\0') ++n;
        return n;
    }

    void text_copy(char* dst, const char* src, hcpp::u32 cap) noexcept {
        hcpp::u32 i = 0;
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
        dst[i] = '\0';
    }

    bool valid_cluster(hcpp::u32 c) noexcept {
        return c >= 2 && c < vol.total_clusters + 2;
    }

    hcpp::u64 cluster_lba(hcpp::u32 c) noexcept {
        return vol.data_lba + static_cast<hcpp::u64>(c - 2) * vol.spc;
    }

    hcpp::u32 cluster_bytes() noexcept { return vol.spc * sector_size; }

    // Single-sector FAT cache: refetches only when the entry lives in a
    // different FAT sector.
    bool fat_load(hcpp::u32 sector_index) noexcept {
        const hcpp::u64 lba = vol.fat_lba + sector_index;
        if (fat_buf_lba != lba) {
            if (!hype::block::block_read(lba, fat_buf, 1)) return false;
            fat_buf_lba = lba;
        }
        return true;
    }

    bool fat_get(hcpp::u32 cluster, hcpp::u32& out) noexcept {
        if (!fat_load(cluster / fat_eps)) return false;
        out = le32(fat_buf + (cluster % fat_eps) * 4) & 0x0FFFFFFFu;
        return true;
    }

    // Read-modify-write one FAT entry and mirror it to every FAT copy.
    bool fat_set(hcpp::u32 cluster, hcpp::u32 value) noexcept {
        const hcpp::u32 index = cluster / fat_eps;
        if (!fat_load(index)) return false;
        hcpp::u8* p = fat_buf + (cluster % fat_eps) * 4;
        put32(p, (le32(p) & 0xF0000000u) | (value & 0x0FFFFFFFu));
        for (hcpp::u32 k = 0; k < vol.fat_count; ++k) {
            if (!hype::block::block_write(
                    vol.fat_lba + static_cast<hcpp::u64>(k) * vol.fat_size +
                        index,
                    fat_buf, 1)) {
                return false;
            }
        }
        return true;
    }

    bool free_chain(hcpp::u32 start) noexcept {
        hcpp::u32 c = start;
        while (valid_cluster(c)) {
            hcpp::u32 next = 0;
            if (!fat_get(c, next)) return false;
            if (!fat_set(c, 0)) return false;
            c = next;
        }
        return true;
    }

    // Allocates `count` clusters (free-scan for 0 entries starting at the
    // alloc hint, then wrapping) and links them into one EOF-terminated
    // chain.
    bool alloc_chain(hcpp::u32 count, hcpp::u32& first) noexcept {
        first = 0;
        hcpp::u32 prev = 0;
        hcpp::u32 found = 0;
        bool io_fail = false;
        const hcpp::u32 last = vol.total_clusters + 1;
        for (hcpp::u32 pass = 0; pass < 2 && found < count && !io_fail;
             ++pass) {
            const hcpp::u32 begin = pass == 0 ? vol.alloc_hint : 2;
            const hcpp::u32 end = pass == 0 ? last + 1 : vol.alloc_hint;
            for (hcpp::u32 c = begin; c < end && found < count && !io_fail;
                 ++c) {
                if (!valid_cluster(c)) continue;
                hcpp::u32 v = 0;
                if (!fat_get(c, v)) {
                    io_fail = true;
                    break;
                }
                if (v != 0) continue;
                if (!fat_set(c, eof_mark)) {
                    io_fail = true;
                    break;
                }
                if (prev != 0 && !fat_set(prev, c)) {
                    fat_set(c, 0);  // unlinked orphan stays free
                    io_fail = true;
                    break;
                }
                if (first == 0) first = c;
                prev = c;
                ++found;
            }
        }
        if (found < count) {
            if (first != 0) free_chain(first);
            first = 0;
            return false;
        }
        vol.alloc_hint = valid_cluster(prev) ? prev : 2;
        return true;
    }

    // ---- directory iteration ----

    struct DirIter {
        hcpp::u32 cluster;   // current cluster (0 = exhausted)
        hcpp::u32 slot;      // next 32-byte slot index in the cluster
    };

    void dir_iter_start(DirIter& it, hcpp::u32 start) noexcept {
        it.cluster = start;
        it.slot = 0;
    }

    // Returns the next raw 32-byte entry (in cluster_buf) plus its on-disk
    // sector lba + byte offset, or nullptr at chain end / I/O error.
    const hcpp::u8* dir_iter_next(DirIter& it, hcpp::u64& out_lba,
                                  hcpp::u32& out_off) noexcept {
        while (it.cluster != 0) {
            if (it.slot == 0 &&
                !hype::block::block_read(cluster_lba(it.cluster),
                                         cluster_buf, vol.spc)) {
                it.cluster = 0;
                return nullptr;
            }
            if (it.slot < vol.spc * dir_eps) {
                const hcpp::u8* e = cluster_buf + it.slot * 32;
                out_lba = cluster_lba(it.cluster) + it.slot / dir_eps;
                out_off = (it.slot * 32) % sector_size;
                ++it.slot;
                return e;
            }
            hcpp::u32 next = 0;
            if (!fat_get(it.cluster, next) || !valid_cluster(next)) {
                it.cluster = 0;
                return nullptr;
            }
            it.cluster = next;
            it.slot = 0;
        }
        return nullptr;
    }

    // Decoded candidate for a lookup.
    struct DirHit {
        bool found;
        bool is_dir;
        hcpp::u32 cluster;
        hcpp::u32 size;
        hcpp::u64 slot_lba;         // where the short entry lives
        hcpp::u32 slot_off;
        hcpp::u64 lfn_lba[lfn_segs_max];   // preceding LFN run (for delete)
        hcpp::u32 lfn_off[lfn_segs_max];
        hcpp::u32 lfn_count;
        char name[name_max];
    };

    // "HYPEAI   TXT" -> "HYPEAI.TXT" (trailing spaces trimmed).
    void format_83(const hcpp::u8* e, char* out, hcpp::u32 cap) noexcept {
        hcpp::u32 pos = 0;
        hcpp::u32 stem = 8;
        while (stem > 0 && e[stem - 1] == ' ') --stem;
        for (hcpp::u32 i = 0; i < stem && pos + 1 < cap; ++i) {
            out[pos++] = static_cast<char>(e[i]);
        }
        hcpp::u32 ext = 3;
        while (ext > 0 && e[8 + ext - 1] == ' ') --ext;
        if (ext != 0 && pos + 1 < cap) {
            out[pos++] = '.';
            for (hcpp::u32 i = 0; i < ext && pos + 1 < cap; ++i) {
                out[pos++] = static_cast<char>(e[8 + i]);
            }
        }
        out[pos] = '\0';
    }

    // UCS-2 -> ASCII for one LFN entry's 13 chars: NUL/0xFFFF terminate,
    // non-ASCII becomes '_'.
    void lfn_decode(const hcpp::u8* e, char* out13) noexcept {
        constexpr hcpp::u8 pos[13] = {1, 3, 5, 7, 9,
                                      14, 16, 18, 20, 22, 24,
                                      28, 30};
        for (hcpp::u32 i = 0; i < 13; ++i) {
            const hcpp::u8 lo = e[pos[i]];
            const hcpp::u8 hi = e[pos[i] + 1];
            if ((lo == 0x00 && hi == 0x00) || (lo == 0xFF && hi == 0xFF)) {
                out13[i] = '\0';
                return;
            }
            out13[i] = hi != 0 ? '_' : static_cast<char>(lo < 0x80 ? lo : '_');
        }
        // A full 13-char segment carries no terminator; callers stop at
        // the first '\0' they meet or run the whole lfn_total span.
    }

    // Finds `name` in the dir chain rooted at `start`. Matches
    // case-insensitively against the decoded LFN (when a valid LFN run
    // precedes the entry) and always against the formatted 8.3 name.
    bool dir_find(hcpp::u32 start, const char* name, DirHit& hit) noexcept {
        hit = {};
        char segs[lfn_segs_max][13];
        hcpp::u32 lfn_total = 0;
        hcpp::u32 lfn_next = 0;
        bool lfn_ok = false;
        hcpp::u32 lfn_count = 0;
        hcpp::u64 lfn_lba[lfn_segs_max];
        hcpp::u32 lfn_off[lfn_segs_max];
        DirIter it;
        dir_iter_start(it, start);
        hcpp::u64 lba;
        hcpp::u32 off;
        const hcpp::u8* e;
        while ((e = dir_iter_next(it, lba, off)) != nullptr) {
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5) {
                lfn_ok = false;
                lfn_total = 0;
                lfn_count = 0;
                continue;
            }
            if (e[de_attr] == attr_lfn) {
                const hcpp::u32 order = e[0] & 0x1F;
                if ((e[0] & 0x40) != 0) {
                    lfn_total = order;
                    lfn_next = order;
                    lfn_ok = order >= 1 && order <= lfn_segs_max;
                    lfn_count = 0;
                } else if (!lfn_ok || order == 0 || order != lfn_next) {
                    lfn_ok = false;
                }
                if (lfn_ok && order >= 1 && order <= lfn_segs_max &&
                    order == lfn_next) {
                    lfn_decode(e, segs[order - 1]);
                    if (lfn_count < lfn_segs_max) {
                        lfn_lba[lfn_count] = lba;
                        lfn_off[lfn_count] = off;
                        ++lfn_count;
                    }
                    lfn_next = order - 1;
                } else {
                    lfn_ok = false;
                }
                continue;
            }
            if ((e[de_attr] & attr_label) != 0) {
                lfn_ok = false;
                lfn_total = 0;
                lfn_count = 0;
                continue;
            }
            // Short entry: canonical name is the LFN when the run was
            // complete (order reached 1), else the 8.3 form.
            char decoded[name_max];
            bool have_lfn = lfn_ok && lfn_total > 0 && lfn_next == 0;
            if (have_lfn) {
                hcpp::u32 pos = 0;
                bool stop = false;
                for (hcpp::u32 s = 0; s < lfn_total && !stop; ++s) {
                    for (hcpp::u32 i = 0; i < 13 && !stop; ++i) {
                        const char c = segs[s][i];
                        if (c == '\0') {
                            stop = true;
                        } else if (pos + 1 < name_max) {
                            decoded[pos++] = c;
                        }
                    }
                }
                decoded[pos] = '\0';
            } else {
                format_83(e, decoded, sizeof(decoded));
            }
            char short_name[16];
            format_83(e, short_name, sizeof(short_name));
            if (name_equal_ci(name, decoded) ||
                name_equal_ci(name, short_name)) {
                hit.found = true;
                hit.is_dir = (e[de_attr] & attr_dir) != 0;
                hit.cluster = (static_cast<hcpp::u32>(
                                   le16(e + de_cluster_hi))
                               << 16) |
                              le16(e + de_cluster_lo);
                hit.size = le32(e + de_size);
                hit.slot_lba = lba;
                hit.slot_off = off;
                hit.lfn_count = lfn_count;
                for (hcpp::u32 i = 0; i < lfn_count; ++i) {
                    hit.lfn_lba[i] = lfn_lba[i];
                    hit.lfn_off[i] = lfn_off[i];
                }
                text_copy(hit.name, decoded, sizeof(hit.name));
                return true;
            }
            lfn_ok = false;
            lfn_total = 0;
            lfn_count = 0;
        }
        return false;
    }

    // First reusable slot (0x00 end-of-dir or 0xE5 deleted) in the chain.
    // When the chain is full, extends it by one zeroed cluster — legal
    // for the FAT32 root chain too.
    bool dir_alloc_slot(hcpp::u32 start, hcpp::u64& out_lba,
                        hcpp::u32& out_off) noexcept {
        hcpp::u32 c = start;
        hcpp::u32 tail = start;
        while (valid_cluster(c)) {
            if (!hype::block::block_read(cluster_lba(c), cluster_buf,
                                         vol.spc)) {
                return false;
            }
            for (hcpp::u32 s = 0; s < vol.spc * dir_eps; ++s) {
                const hcpp::u8* e = cluster_buf + s * 32;
                if (e[0] == 0x00 || e[0] == 0xE5) {
                    out_lba = cluster_lba(c) + s / dir_eps;
                    out_off = (s * 32) % sector_size;
                    return true;
                }
            }
            tail = c;
            hcpp::u32 next = 0;
            if (!fat_get(c, next) || !valid_cluster(next)) break;
            c = next;
        }
        // Full directory: grow by one cluster.
        hcpp::u32 fresh = 0;
        if (!alloc_chain(1, fresh)) return false;
        if (!fat_set(tail, fresh)) {
            fat_set(fresh, 0);
            return false;
        }
        hcpp::set(cluster_buf, 0, cluster_bytes());
        if (!hype::block::block_write(cluster_lba(fresh), cluster_buf,
                                      vol.spc)) {
            fat_set(tail, eof_mark);
            fat_set(fresh, 0);
            return false;
        }
        out_lba = cluster_lba(fresh);
        out_off = 0;
        return true;
    }

    // Resolves the first `depth` components of `path` as directories;
    // returns the start cluster of that directory.
    bool resolve_dir(const hype::vfs::HypePath& path, hcpp::u32 depth,
                     hcpp::u32& out_cluster) noexcept {
        hcpp::u32 cur = vol.root_cluster;
        for (hcpp::u32 d = 0; d < depth; ++d) {
            DirHit hit;
            if (!dir_find(cur, path.components[d], hit) || !hit.is_dir ||
                !valid_cluster(hit.cluster)) {
                return false;
            }
            cur = hit.cluster;
        }
        out_cluster = cur;
        return true;
    }

    // leaf -> 11-byte 8.3 field. Last '.' splits stem/ext (no dot = no
    // ext); stem truncates to 8, ext to 3; uppercase; disallowed chars
    // become '_'. Rejects empty stems and leading dots.
    bool to_83(const char* leaf, hcpp::u8 out[11]) noexcept {
        hcpp::u32 len = text_len(leaf, hype::vfs::hypepath_component_size);
        if (len == 0 || leaf[0] == '.') return false;
        hcpp::u32 dot = len;
        for (hcpp::u32 i = len; i > 0; --i) {
            if (leaf[i - 1] == '.') {
                dot = i - 1;
                break;
            }
        }
        if (dot == 0) return false;
        hcpp::set(out, ' ', 11);
        const auto ok_char = [](char c) noexcept -> char {
            c = ascii_up(c);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
            switch (c) {
                case '$': case '%': case '\'': case '-': case '_':
                case '@': case '~': case '`': case '!': case '(':
                case ')': case '{': case '}': case '^': case '#':
                case '&':
                    return c;
                default:
                    return '_';
            }
        };
        hcpp::u32 stem = dot < 8 ? dot : 8;
        for (hcpp::u32 i = 0; i < stem; ++i) out[i] = ok_char(leaf[i]);
        if (dot < len) {
            hcpp::u32 ext_len = len - dot - 1;
            if (ext_len > 3) ext_len = 3;
            for (hcpp::u32 i = 0; i < ext_len; ++i) {
                out[8 + i] = ok_char(leaf[dot + 1 + i]);
            }
        }
        return true;
    }

    bool write_dir_slot(hcpp::u64 lba, hcpp::u32 off, const hcpp::u8* name83,
                        hcpp::u8 attr, hcpp::u32 cluster,
                        hcpp::u32 size) noexcept {
        if (!hype::block::block_read(lba, sec_buf, 1)) return false;
        hcpp::u8* e = sec_buf + off;
        hcpp::copy(e, name83, 11);
        hcpp::set(e + 11, 0, 32 - 11);
        e[de_attr] = attr;
        put16(e + de_cluster_hi, static_cast<hcpp::u16>(cluster >> 16));
        put16(e + de_cluster_lo, static_cast<hcpp::u16>(cluster & 0xFFFF));
        put32(e + de_size, size);
        return hype::block::block_write(lba, sec_buf, 1);
    }

    bool update_dir_entry(const DirHit& hit, hcpp::u32 cluster,
                          hcpp::u32 size) noexcept {
        if (!hype::block::block_read(hit.slot_lba, sec_buf, 1)) return false;
        hcpp::u8* e = sec_buf + hit.slot_off;
        put16(e + de_cluster_hi, static_cast<hcpp::u16>(cluster >> 16));
        put16(e + de_cluster_lo, static_cast<hcpp::u16>(cluster & 0xFFFF));
        put32(e + de_size, size);
        return hype::block::block_write(hit.slot_lba, sec_buf, 1);
    }

    bool mark_deleted(hcpp::u64 lba, hcpp::u32 off) noexcept {
        if (!hype::block::block_read(lba, sec_buf, 1)) return false;
        sec_buf[off] = 0xE5;
        return hype::block::block_write(lba, sec_buf, 1);
    }

    // Writes `size` bytes of `data` down the chain starting at `first`
    // (zero-pads the tail cluster). Allocated count is `need` clusters.
    bool write_chain_data(hcpp::u32 first, const hcpp::u8* data,
                          hcpp::u32 size, hcpp::u32 need) noexcept {
        hcpp::u32 c = first;
        hcpp::u32 done = 0;
        for (hcpp::u32 i = 0; i < need; ++i) {
            if (!valid_cluster(c)) return false;
            const hcpp::u64 lba = cluster_lba(c);
            const hcpp::u32 left = size - done;
            if (left >= cluster_bytes()) {
                if (!hype::block::block_write(lba, data + done, vol.spc)) {
                    return false;
                }
                done += cluster_bytes();
            } else {
                hcpp::set(cluster_buf, 0, cluster_bytes());
                if (left > 0) hcpp::copy(cluster_buf, data + done, left);
                if (!hype::block::block_write(lba, cluster_buf, vol.spc)) {
                    return false;
                }
                done += left;
            }
            hcpp::u32 next = 0;
            if (i + 1 < need) {
                if (!fat_get(c, next)) return false;
                c = next;
            }
        }
        return true;
    }

    // BPB validation + volume layout. `sectors` is the partition's sector
    // count from MBR/GPT (bounds the computed region when BPB totals are
    // larger than the partition).
    bool probe_bpb(hcpp::u64 lba, hcpp::u64 sectors,
                   hype::vfs::Fat32Mount& out) noexcept {
        if (!hype::block::block_read(lba, sec_buf, 1)) return false;
        if (sec_buf[510] != 0x55 || sec_buf[511] != 0xAA) return false;
        if (le16(sec_buf + 11) != sector_size) return false;
        const hcpp::u32 spc = sec_buf[13];
        if (spc == 0 || spc > max_spc || (spc & (spc - 1)) != 0) return false;
        const hcpp::u32 reserved = le16(sec_buf + 14);
        const hcpp::u32 nfats = sec_buf[16];
        const hcpp::u32 fatsz = le32(sec_buf + 36);
        const hcpp::u32 rootc = le32(sec_buf + 44);
        if (reserved == 0 || nfats == 0 || fatsz == 0 || rootc < 2) {
            return false;
        }
        if (hcpp::compare(sec_buf + 82, "FAT32", 5) != 0) return false;
        hcpp::u64 totsec = le16(sec_buf + 19);
        if (totsec == 0) totsec = le32(sec_buf + 32);
        if (totsec == 0) return false;
        if (sectors != 0 && totsec > sectors) totsec = sectors;
        const hcpp::u64 fat_region =
            reserved + static_cast<hcpp::u64>(nfats) * fatsz;
        if (totsec <= fat_region) return false;
        const hcpp::u64 clusters = (totsec - fat_region) / spc;
        if (clusters == 0 || clusters > 0x0FFFFFF0ull) return false;
        if (rootc >= clusters + 2) return false;

        vol.part_lba = lba;
        vol.fat_lba = lba + reserved;
        vol.data_lba = lba + fat_region;
        vol.spc = spc;
        vol.fat_count = nfats;
        vol.fat_size = fatsz;
        vol.root_cluster = rootc;
        vol.total_clusters = static_cast<hcpp::u32>(clusters);
        vol.alloc_hint = 2;
        vol.mounted = true;

        out.part_ok = true;
        out.part_lba = lba;
        out.clusters = vol.total_clusters;
        hcpp::set(out.label, 0, sizeof(out.label));
        hcpp::u32 n = 11;
        while (n > 0 && sec_buf[71 + n - 1] == ' ') --n;
        for (hcpp::u32 i = 0; i < n; ++i) {
            out.label[i] = static_cast<char>(sec_buf[71 + i]);
        }
        out.label[n] = '\0';
        return true;
    }

    // GPT partition-type GUIDs, on-disk (mixed-endian) byte order.
    constexpr hcpp::u8 guid_esp[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1E, 0xF8, 0xD2, 0x11,
        0x8B, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    constexpr hcpp::u8 guid_basic[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};

    bool guid_zero(const hcpp::u8* g) noexcept {
        for (hcpp::u32 i = 0; i < 16; ++i) {
            if (g[i] != 0) return false;
        }
        return true;
    }

    // Protective-MBR path: GPT header at LBA 1, first gpt_scan_max
    // entries scanned for EFI System / Basic Data partitions whose boot
    // sector validates as FAT32.
    bool gpt_probe(hype::vfs::Fat32Mount& out) noexcept {
        if (!hype::block::block_read(1, sec_buf, 1)) return false;
        if (hcpp::compare(sec_buf, "EFI PART", 8) != 0) return false;
        const hcpp::u64 entries_lba = le64(sec_buf + 72);
        const hcpp::u32 num = le32(sec_buf + 80);
        const hcpp::u32 esz = le32(sec_buf + 84);
        if (entries_lba == 0 || esz < 128 || esz > sector_size) return false;
        const hcpp::u32 n = num < gpt_scan_max ? num : gpt_scan_max;
        for (hcpp::u32 i = 0; i < n; ++i) {
            const hcpp::u64 byte_off = static_cast<hcpp::u64>(i) * esz;
            const hcpp::u32 in_sector =
                static_cast<hcpp::u32>(byte_off % sector_size);
            if (in_sector + esz > sector_size) continue;  // straddler
            if (!hype::block::block_read(entries_lba + byte_off / sector_size,
                                         sec_buf, 1)) {
                continue;
            }
            const hcpp::u8* e = sec_buf + in_sector;
            if (guid_zero(e)) continue;
            if (hcpp::compare(e, guid_esp, 16) != 0 &&
                hcpp::compare(e, guid_basic, 16) != 0) {
                continue;
            }
            const hcpp::u64 first = le64(e + 32);
            const hcpp::u64 last = le64(e + 40);
            const hcpp::u64 sectors =
                last >= first ? last - first + 1 : 0;
            if (first != 0 && probe_bpb(first, sectors, out)) return true;
        }
        return false;
    }

    // Common file/dir create-or-rewrite body for fat32_write/mkdir.
    hype::vfs::FsStatus store_entry(
        const hype::vfs::HypePath& path, const hcpp::u8* data,
        hcpp::u32 size, bool make_dir) noexcept {
        using hype::vfs::FsStatus;
        if (path.depth == 0) return make_dir ? FsStatus::Exists
                                             : FsStatus::IsDir;
        hcpp::u32 parent = 0;
        if (!resolve_dir(path, path.depth - 1, parent)) {
            return FsStatus::NotFound;
        }
        const char* leaf = path.components[path.depth - 1];
        DirHit hit;
        bool exists = dir_find(parent, leaf, hit);
        hcpp::u8 name83[11];
        if (!exists) {
            if (!to_83(leaf, name83)) return FsStatus::BadPath;
            char fmt[16];
            format_83(name83, fmt, sizeof(fmt));
            exists = dir_find(parent, fmt, hit);
        }
        if (exists) {
            if (make_dir) return FsStatus::Exists;
            if (hit.is_dir) return FsStatus::IsDir;
        }
        const hcpp::u32 cb = cluster_bytes();
        const hcpp::u32 need = make_dir ? 1
                                        : (size + cb - 1) / cb;
        hcpp::u32 first = 0;
        if (need != 0 && !alloc_chain(need, first)) return FsStatus::NoSpace;
        bool ok = true;
        if (make_dir) {
            // Fresh dir cluster: "." -> self, ".." -> parent (0 = root).
            hcpp::set(cluster_buf, 0, cb);
            hcpp::u8* e0 = cluster_buf;
            e0[0] = '.';
            hcpp::set(e0 + 1, ' ', 10);
            e0[de_attr] = attr_dir;
            put16(e0 + de_cluster_hi, static_cast<hcpp::u16>(first >> 16));
            put16(e0 + de_cluster_lo, static_cast<hcpp::u16>(first & 0xFFFF));
            hcpp::u8* e1 = cluster_buf + 32;
            e1[0] = '.';
            e1[1] = '.';
            hcpp::set(e1 + 2, ' ', 9);
            e1[de_attr] = attr_dir;
            const hcpp::u32 up =
                parent == vol.root_cluster ? 0 : parent;
            put16(e1 + de_cluster_hi, static_cast<hcpp::u16>(up >> 16));
            put16(e1 + de_cluster_lo, static_cast<hcpp::u16>(up & 0xFFFF));
            ok = hype::block::block_write(cluster_lba(first), cluster_buf,
                                          vol.spc);
        } else if (need != 0) {
            ok = write_chain_data(first, data, size, need);
        }
        if (!ok) {
            if (first != 0) free_chain(first);
            return FsStatus::IoError;
        }
        if (exists) {
            // Rewrite in place: keep the on-disk name/attr (and any LFN),
            // swap in the new chain + size, then release the old chain.
            const hcpp::u32 old = hit.cluster;
            if (!update_dir_entry(hit, first, size)) {
                if (first != 0) free_chain(first);
                return FsStatus::IoError;
            }
            if (valid_cluster(old)) free_chain(old);
            hype::block::block_flush();
            return FsStatus::Ok;
        }
        hcpp::u64 slot_lba;
        hcpp::u32 slot_off;
        if (!dir_alloc_slot(parent, slot_lba, slot_off)) {
            if (first != 0) free_chain(first);
            return FsStatus::NoSpace;
        }
        if (!write_dir_slot(slot_lba, slot_off, name83,
                            make_dir ? attr_dir : attr_file, first,
                            make_dir ? 0 : size)) {
            if (first != 0) free_chain(first);
            return FsStatus::IoError;
        }
        hype::block::block_flush();
        return FsStatus::Ok;
    }

}

namespace hype::vfs {

    Fat32Mount fat32_mount() noexcept {
        Fat32Mount out{};
        vol = {};
        fat_buf_lba = invalid_lba;
        out.block_ok = hype::block::block_present();
        if (!out.block_ok) return out;
        if (!hype::block::block_read(0, sec_buf, 1)) return out;

        // Stash the four MBR slots: probe_bpb() reuses sec_buf, so the
        // table cannot be read in place while probing.
        hcpp::u8 parts[64];
        hcpp::copy(parts, sec_buf + 0x1BE, sizeof(parts));

        // MBR slots: 0x0B/0x0C FAT32 entries first; a protective 0xEE
        // switches to the GPT scan when no MBR FAT32 validates.
        bool protective = false;
        for (hcpp::u32 i = 0; i < 4; ++i) {
            const hcpp::u8* e = parts + i * 16;
            const hcpp::u8 type = e[4];
            if (type == 0xEE) {
                protective = true;
                continue;
            }
            if (type != 0x0B && type != 0x0C) continue;
            const hcpp::u64 lba = le32(e + 8);
            const hcpp::u64 sectors = le32(e + 12);
            if (lba != 0 && sectors != 0 &&
                probe_bpb(lba, sectors, out)) {
                return out;
            }
        }
        if (protective) gpt_probe(out);
        return out;
    }

    bool fat32_mounted() noexcept { return vol.mounted; }

    FsStatus fat32_read(const HypePath& path, void* out, hcpp::u32 capacity,
                        hcpp::u32* out_size) noexcept {
        if (out_size != nullptr) *out_size = 0;
        if (!vol.mounted) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::IsDir;
        hcpp::u32 dir = 0;
        if (!resolve_dir(path, path.depth - 1, dir)) {
            return FsStatus::NotFound;
        }
        DirHit hit;
        if (!dir_find(dir, path.components[path.depth - 1], hit)) {
            return FsStatus::NotFound;
        }
        if (hit.is_dir) return FsStatus::IsDir;
        const hcpp::u32 bytes =
            hit.size < capacity ? hit.size : capacity;
        if (bytes > 0 && out == nullptr) return FsStatus::BadPath;
        if (bytes > 0 && !valid_cluster(hit.cluster)) {
            return FsStatus::IoError;  // nonzero size, no chain
        }
        hcpp::u8* dst = static_cast<hcpp::u8*>(out);
        hcpp::u32 done = 0;
        hcpp::u32 c = hit.cluster;
        while (done < bytes && valid_cluster(c)) {
            const hcpp::u32 chunk =
                bytes - done < cluster_bytes() ? bytes - done
                                               : cluster_bytes();
            if (chunk == cluster_bytes()) {
                if (!hype::block::block_read(cluster_lba(c), dst + done,
                                             vol.spc)) {
                    return FsStatus::IoError;
                }
            } else {
                if (!hype::block::block_read(cluster_lba(c), cluster_buf,
                                             vol.spc)) {
                    return FsStatus::IoError;
                }
                hcpp::copy(dst + done, cluster_buf, chunk);
            }
            done += chunk;
            if (done < bytes) {
                hcpp::u32 next = 0;
                if (!fat_get(c, next) || !valid_cluster(next)) break;
                c = next;
            }
        }
        if (out_size != nullptr) *out_size = hit.size;
        return FsStatus::Ok;
    }

    FsStatus fat32_write(const HypePath& path, const void* data,
                         hcpp::u32 size) noexcept {
        if (!vol.mounted) return FsStatus::BadDisk;
        if (size > 0 && data == nullptr) return FsStatus::BadPath;
        return store_entry(path, static_cast<const hcpp::u8*>(data), size,
                           false);
    }

    FsStatus fat32_mkdir(const HypePath& path) noexcept {
        if (!vol.mounted) return FsStatus::BadDisk;
        return store_entry(path, nullptr, 0, true);
    }

    FsStatus fat32_stat(const HypePath& path, FsStat& out) noexcept {
        if (!vol.mounted) return FsStatus::BadDisk;
        out = {};
        out.disk = 'F';
        if (path.depth == 0) {
            out.flags = FS_DIR;
            text_copy(out.name, "F", sizeof(out.name));
            return FsStatus::Ok;
        }
        hcpp::u32 dir = 0;
        if (!resolve_dir(path, path.depth - 1, dir)) {
            return FsStatus::NotFound;
        }
        DirHit hit;
        if (!dir_find(dir, path.components[path.depth - 1], hit)) {
            return FsStatus::NotFound;
        }
        text_copy(out.name, hit.name, sizeof(out.name));
        out.flags = hit.is_dir ? FS_DIR : FS_NONE;
        out.size = hit.size;
        return FsStatus::Ok;
    }

    FsStatus fat32_delete(const HypePath& path) noexcept {
        if (!vol.mounted) return FsStatus::BadDisk;
        if (path.depth == 0) return FsStatus::AccessDenied;
        hcpp::u32 dir = 0;
        if (!resolve_dir(path, path.depth - 1, dir)) {
            return FsStatus::NotFound;
        }
        DirHit hit;
        if (!dir_find(dir, path.components[path.depth - 1], hit)) {
            return FsStatus::NotFound;
        }
        if (hit.is_dir) {
            // Only empty dirs ("."/".." + deleted/LFN noise) may go.
            DirIter it;
            dir_iter_start(it, hit.cluster);
            hcpp::u64 lba;
            hcpp::u32 off;
            const hcpp::u8* e;
            while ((e = dir_iter_next(it, lba, off)) != nullptr) {
                if (e[0] == 0x00) break;
                if (e[0] == 0xE5 || e[de_attr] == attr_lfn ||
                    (e[de_attr] & attr_label) != 0) {
                    continue;
                }
                if ((e[de_attr] & attr_dir) != 0 &&
                    (e[0] == '.' &&
                     (e[1] == ' ' || (e[1] == '.' && e[2] == ' ')))) {
                    continue;  // "." / ".."
                }
                return FsStatus::AccessDenied;
            }
        }
        bool ok = mark_deleted(hit.slot_lba, hit.slot_off);
        for (hcpp::u32 i = 0; ok && i < hit.lfn_count; ++i) {
            ok = mark_deleted(hit.lfn_lba[i], hit.lfn_off[i]);
        }
        if (ok && valid_cluster(hit.cluster)) ok = free_chain(hit.cluster);
        if (!ok) return FsStatus::IoError;
        hype::block::block_flush();
        return FsStatus::Ok;
    }

    hcpp::u32 fat32_list(const HypePath& path, FsStat* out,
                         hcpp::u32 out_max) noexcept {
        if (!vol.mounted) return 0;
        hcpp::u32 dir = 0;
        if (!resolve_dir(path, path.depth, dir)) return 0;
        char segs[lfn_segs_max][13];
        hcpp::u32 lfn_total = 0;
        hcpp::u32 lfn_next = 0;
        bool lfn_ok = false;
        hcpp::u32 count = 0;
        DirIter it;
        dir_iter_start(it, dir);
        hcpp::u64 lba;
        hcpp::u32 off;
        const hcpp::u8* e;
        while ((e = dir_iter_next(it, lba, off)) != nullptr) {
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5) {
                lfn_ok = false;
                continue;
            }
            if (e[de_attr] == attr_lfn) {
                const hcpp::u32 order = e[0] & 0x1F;
                if ((e[0] & 0x40) != 0) {
                    lfn_total = order;
                    lfn_next = order;
                    lfn_ok = order >= 1 && order <= lfn_segs_max;
                } else if (!lfn_ok || order == 0 || order != lfn_next) {
                    lfn_ok = false;
                }
                if (lfn_ok && order == lfn_next && order >= 1) {
                    lfn_decode(e, segs[order - 1]);
                    lfn_next = order - 1;
                } else {
                    lfn_ok = false;
                }
                continue;
            }
            if ((e[de_attr] & attr_label) != 0) {
                lfn_ok = false;
                continue;
            }
            char decoded[name_max];
            if (lfn_ok && lfn_total > 0 && lfn_next == 0) {
                hcpp::u32 pos = 0;
                bool stop = false;
                for (hcpp::u32 s = 0; s < lfn_total && !stop; ++s) {
                    for (hcpp::u32 i = 0; i < 13 && !stop; ++i) {
                        const char c = segs[s][i];
                        if (c == '\0') {
                            stop = true;
                        } else if (pos + 1 < name_max) {
                            decoded[pos++] = c;
                        }
                    }
                }
                decoded[pos] = '\0';
            } else {
                format_83(e, decoded, sizeof(decoded));
            }
            lfn_ok = false;
            lfn_total = 0;
            if (decoded[0] == '.' &&
                (decoded[1] == '\0' ||
                 (decoded[1] == '.' && decoded[2] == '\0'))) {
                continue;  // "." / ".."
            }
            if (out != nullptr && count < out_max) {
                FsStat& st = out[count];
                st = {};
                text_copy(st.name, decoded, sizeof(st.name));
                st.flags =
                    (e[de_attr] & attr_dir) != 0 ? FS_DIR : FS_NONE;
                st.size = le32(e + de_size);
                st.disk = 'F';
            }
            ++count;
        }
        return count;
    }

    bool fat32_selftest() noexcept {
        if (!vol.mounted) return false;
        static const char payload[] = "HypeAI was here";
        if (fs_write("HYPEOS://F/hypeai.txt", payload, sizeof(payload)) !=
            FsStatus::Ok) {
            return false;
        }
        char back[24] = {};
        hcpp::u32 got = 0;
        return fs_read("HYPEOS://F/hypeai.txt", back, sizeof(back),
                       &got) == FsStatus::Ok &&
               got == sizeof(payload) &&
               hcpp::compare(back, payload, sizeof(payload)) == 0;
    }

}
