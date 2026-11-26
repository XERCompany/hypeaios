#include "hcpp/fonts/hpf.hpp"

#include "hcpp/memory.hpp"

namespace hcpp::fonts {

    namespace {

        constexpr size_t hpf_header_size = 48;
        constexpr size_t hpf_name_size = 32;
        constexpr size_t hpf_record_size = 14;
        constexpr u32 hpf_magic = 0x31465048;
        constexpr u16 hpf_version = 1;
        constexpr u32 hpf_selftest_slot = hpf_max_slots - 1;

        struct HpfSlot {
            FontFace face;
            const u8* data;
            size_t size;
        };

        HpfSlot slots[hpf_max_slots];

        u16 read_u16(const u8* at) noexcept {
            return static_cast<u16>(at[0]) | (static_cast<u16>(at[1]) << 8);
        }

        u32 read_u32(const u8* at) noexcept {
            return static_cast<u32>(at[0]) | (static_cast<u32>(at[1]) << 8) |
                   (static_cast<u32>(at[2]) << 16) | (static_cast<u32>(at[3]) << 24);
        }

        FontGlyph slot_glyph(u32 slot, u32 codepoint) noexcept {
            const HpfSlot& loaded = slots[slot];
            if (loaded.data == nullptr) {
                return FontGlyph{};
            }
            const u8* table = loaded.data + hpf_header_size;
            size_t low = 0;
            size_t high = loaded.face.glyph_count;
            while (low < high) {
                const size_t mid = low + (high - low) / 2;
                if (read_u32(table + mid * hpf_record_size) < codepoint) {
                    low = mid + 1;
                } else {
                    high = mid;
                }
            }
            if (low >= loaded.face.glyph_count ||
                read_u32(table + low * hpf_record_size) != codepoint) {
                return FontGlyph{};
            }
            const u8* record = table + low * hpf_record_size;
            const i8 advance = static_cast<i8>(record[10]);
            FontGlyph glyph;
            glyph.bitmap = loaded.data + read_u32(record + 4);
            glyph.row_stride = static_cast<u8>((static_cast<u32>(record[8]) + 7U) / 8U);
            glyph.width = record[8];
            glyph.height = record[9];
            glyph.advance = advance > 0 ? static_cast<u8>(advance) : 0;
            glyph.bearing_x = static_cast<i8>(record[11]);
            glyph.bearing_y = static_cast<i8>(record[12]);
            return glyph;
        }

        FontGlyph slot0_glyph(u32 codepoint) noexcept { return slot_glyph(0, codepoint); }
        FontGlyph slot1_glyph(u32 codepoint) noexcept { return slot_glyph(1, codepoint); }
        FontGlyph slot2_glyph(u32 codepoint) noexcept { return slot_glyph(2, codepoint); }
        FontGlyph slot3_glyph(u32 codepoint) noexcept { return slot_glyph(3, codepoint); }

        constexpr GlyphLookup slot_lookups[hpf_max_slots] = {
            slot0_glyph, slot1_glyph, slot2_glyph, slot3_glyph,
        };

        bool valid_name(const char* name) noexcept {
            bool terminated = false;
            for (size_t index = 0; index < hpf_name_size; ++index) {
                const u8 value = static_cast<u8>(name[index]);
                if (value == 0) {
                    terminated = true;
                    break;
                }
                if (value < 0x20 || value > 0x7E) {
                    return false;
                }
            }
            return terminated && name[0] != '\0';
        }

        constexpr u8 selftest_blob[] = {
            'H', 'P', 'F', '1', 0x01, 0x00, 0x01, 0x00,
            'H', 'P', 'F', 'T', 'e', 's', 't', 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            8, 6, 8, 0, 0x02, 0x00, 0x00, 0x00,
            0x41, 0x00, 0x00, 0x00, 0x4C, 0x00, 0x00, 0x00, 8, 8, 8, 0, 6, 0,
            0x42, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 8, 8, 8, 0, 6, 0,
            0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00,
            0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00,
        };

        static_assert(sizeof(selftest_blob) == 92);

        bool reject_corrupted(const u8* blob, size_t size) noexcept {
            u8 bad[92];
            copy(bad, blob, size);
            bad[0] = 'X';
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[4] = 0x02;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[43] = 0x01;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[44] = 0xFF;
            bad[45] = 0xFF;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[hpf_header_size] = 0x42;
            bad[hpf_header_size + hpf_record_size] = 0x41;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[hpf_header_size + 13] = 0x01;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            copy(bad, blob, size);
            bad[hpf_header_size + 4] = 0x01;
            if (hpf_load(bad, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            if (hpf_load(blob, hpf_header_size - 1, hpf_selftest_slot) != nullptr) {
                return false;
            }
            if (hpf_load(nullptr, size, hpf_selftest_slot) != nullptr) {
                return false;
            }
            return hpf_load(blob, size, hpf_max_slots) == nullptr;
        }

    }  // namespace

    const FontFace* hpf_load(const u8* data, size_t size, u32 slot) noexcept {
        if (data == nullptr || slot >= hpf_max_slots || size < hpf_header_size) {
            return nullptr;
        }
        if (read_u32(data) != hpf_magic || read_u16(data + 4) != hpf_version || data[43] != 0) {
            return nullptr;
        }
        const char* name = reinterpret_cast<const char*>(data + 8);
        if (!valid_name(name)) {
            return nullptr;
        }
        const u8 height = data[40];
        const u8 baseline = data[41];
        const u8 default_advance = data[42];
        const u32 glyph_count = read_u32(data + 44);
        if (height == 0 || default_advance == 0 || baseline > height || glyph_count == 0 ||
            glyph_count > 0xFFFF) {
            return nullptr;
        }
        const u64 table_end = static_cast<u64>(hpf_header_size) +
                              static_cast<u64>(glyph_count) * hpf_record_size;
        if (table_end > size) {
            return nullptr;
        }
        const u8* table = data + hpf_header_size;
        u32 previous = 0;
        for (u32 index = 0; index < glyph_count; ++index) {
            const u8* record = table + index * hpf_record_size;
            const u32 codepoint = read_u32(record);
            const u32 data_offset = read_u32(record + 4);
            const u32 width = record[8];
            const u32 rows = record[9];
            if (record[13] != 0 || width == 0 || rows == 0) {
                return nullptr;
            }
            if (index != 0 && codepoint <= previous) {
                return nullptr;
            }
            previous = codepoint;
            const u32 stride = (width + 7U) / 8U;
            const u64 glyph_end = static_cast<u64>(data_offset) + static_cast<u64>(stride) * rows;
            if (data_offset < table_end || glyph_end > size) {
                return nullptr;
            }
        }
        slots[slot].data = data;
        slots[slot].size = size;
        slots[slot].face.name = name;
        slots[slot].face.height = height;
        slots[slot].face.baseline = baseline;
        slots[slot].face.advance = default_advance;
        slots[slot].face.glyph_count = static_cast<u16>(glyph_count);
        slots[slot].face.glyph_for = slot_lookups[slot];
        return &slots[slot].face;
    }

    bool load_embedded_fonts() noexcept {
        const FontFace* face =
            hpf_load(hpf_hypeai_8x16, hpf_hypeai_8x16_size, hpf_embedded_slot);
        if (face == nullptr || !register_font(*face)) {
            return false;
        }
        return set_default_font(face->name);
    }

    bool hpf_selftest() noexcept {
        const FontFace* face =
            hpf_load(selftest_blob, sizeof(selftest_blob), hpf_selftest_slot);
        if (face == nullptr || face->glyph_count != 2 || face->height != 8 || face->baseline != 6 ||
            face->advance != 8) {
            return false;
        }
        const FontGlyph glyph_a = face->glyph_for('A');
        const FontGlyph glyph_b = face->glyph_for('B');
        if (!glyph_a.valid() || !glyph_b.valid() || glyph_a.width != 8 || glyph_a.height != 8 ||
            glyph_a.row_stride != 1 || glyph_a.advance != 8 || glyph_a.bearing_y != 6) {
            return false;
        }
        if (!glyph_a.pixel(3, 0) || glyph_a.pixel(0, 0) || glyph_a.pixel(0, 7) ||
            glyph_b.pixel(7, 0)) {
            return false;
        }
        if (face->glyph_for('C').valid() || face->glyph_for(0x20).valid()) {
            return false;
        }
        return reject_corrupted(selftest_blob, sizeof(selftest_blob));
    }

}  // namespace hcpp::fonts
