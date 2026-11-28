#include "kernel/codecapps/woff.h"

#include "kernel/codecapps/arena.h"
#include "kernel/ztext/ztext.h"

// WOFF 1.0 unwrap (wave-2): 44-byte header + 20-byte table directory;
// each table is zlib-compressed or stored. Rebuilds the sfnt layout
// (scaler header + 16-byte directory + 4-aligned table data) into
// BlobOut for the existing TTF parser path.

namespace hype::codec {

    namespace {

        hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
            return (static_cast<hcpp::u32>(p[0]) << 24) |
                   (static_cast<hcpp::u32>(p[1]) << 16) |
                   (static_cast<hcpp::u32>(p[2]) << 8) | p[3];
        }

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        void write_be32(hcpp::u8 *p, hcpp::u32 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v >> 24);
            p[1] = static_cast<hcpp::u8>(v >> 16);
            p[2] = static_cast<hcpp::u8>(v >> 8);
            p[3] = static_cast<hcpp::u8>(v);
        }

        void write_be16(hcpp::u8 *p, hcpp::u16 v) noexcept {
            p[0] = static_cast<hcpp::u8>(v >> 8);
            p[1] = static_cast<hcpp::u8>(v);
        }

        constexpr hcpp::u32 max_tables = 128;

    }  // namespace

    CodecStatus woff_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                            void *output, Arena &arena) noexcept {
        auto *blob = static_cast<archive::BlobOut *>(output);
        if (input == nullptr || blob == nullptr || input_size < 44) return CodecStatus::BadInput;
        if (read_be32(input) != 0x774F4646) return CodecStatus::BadInput;  // 'wOFF'
        const hcpp::u32 flavor = read_be32(input + 4);
        const hcpp::u32 total = read_be32(input + 8);
        const hcpp::u16 ntables = read_be16(input + 12);
        const hcpp::u32 sfnt_size = read_be32(input + 16);
        if (ntables == 0 || ntables > max_tables || total > input_size ||
            read_be16(input + 14) != 0)
            return CodecStatus::BadInput;
        const hcpp::u64 dir_end = 44 + static_cast<hcpp::u64>(ntables) * 20;
        if (dir_end > input_size) return CodecStatus::BadInput;
        const hcpp::u64 header_size =
            12 + static_cast<hcpp::u64>(ntables) * 16;
        if (header_size > sfnt_size || sfnt_size > 0x7FFFFFFFu)
            return CodecStatus::BadInput;
        hcpp::u8 *out = blob->data;
        if (out == nullptr) {
            out = static_cast<hcpp::u8 *>(
                arena_alloc(arena, static_cast<hcpp::size_t>(sfnt_size), 8));
            if (out == nullptr) return CodecStatus::NoSpace;
            blob->data = out;
            blob->capacity = static_cast<hcpp::size_t>(sfnt_size);
        }
        if (blob->capacity < sfnt_size) return CodecStatus::NoSpace;

        // sfnt offset-table header.
        write_be32(out, flavor);
        write_be16(out + 4, ntables);
        hcpp::u16 search = 1;
        hcpp::u16 entry_sel = 0;
        while (search * 2 <= ntables) {
            search *= 2;
            ++entry_sel;
        }
        write_be16(out + 6, search * 16);
        write_be16(out + 8, entry_sel);
        write_be16(out + 10, ntables * 16 - search * 16);

        hcpp::size_t table_at = static_cast<hcpp::size_t>(header_size);
        for (hcpp::u32 i = 0; i < ntables; ++i) {
            const hcpp::u8 *ent = input + 44 + i * 20;
            const hcpp::u32 offset = read_be32(ent + 4);
            const hcpp::u32 comp = read_be32(ent + 8);
            const hcpp::u32 orig = read_be32(ent + 12);
            const hcpp::u32 crc = read_be32(ent + 16);
            if (offset > input_size || comp > input_size - offset ||
                comp > orig || orig > sfnt_size)
                return CodecStatus::BadInput;
            // Align each table to 4 bytes inside the sfnt.
            while ((table_at & 3) != 0) out[table_at++] = 0;
            if (table_at + orig > sfnt_size) return CodecStatus::BadInput;
            if (comp < orig) {
                hcpp::size_t produced = 0;
                if (!ztext::ztext_zlib_decode(out + table_at, orig, input + offset,
                                              comp, produced) ||
                    produced != orig)
                    return CodecStatus::BadInput;
            } else {
                for (hcpp::u32 k = 0; k < orig; ++k)
                    out[table_at + k] = input[offset + k];
            }
            hcpp::u8 *de = out + 12 + i * 16;
            de[0] = ent[0];
            de[1] = ent[1];
            de[2] = ent[2];
            de[3] = ent[3];
            write_be32(de + 4, crc);
            write_be32(de + 8, static_cast<hcpp::u32>(table_at));
            write_be32(de + 12, orig);
            table_at += orig;
        }
        blob->size = static_cast<hcpp::size_t>(sfnt_size);
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
