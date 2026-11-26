#include "kernel/codecapps/audio/ogg.h"
#include "kernel/codecapps/arena.h"

namespace hype::codec {

    namespace {

        struct OggCrcTable {
            hcpp::u32 value[256];
        };

        constexpr OggCrcTable make_crc_table() noexcept {
            OggCrcTable table{};
            for (hcpp::u32 i = 0; i < 256; ++i) {
                hcpp::u32 r = i << 24;
                for (int b = 0; b < 8; ++b)
                    r = (r << 1) ^ ((r & 0x80000000u) != 0 ? 0x04C11DB7u : 0u);
                table.value[i] = r;
            }
            return table;
        }

        constexpr OggCrcTable crc_table = make_crc_table();

        hcpp::u32 ogg_crc(const hcpp::u8 *data, hcpp::size_t size,
                          hcpp::size_t crc_field) noexcept {
            hcpp::u32 crc = 0;
            for (hcpp::size_t i = 0; i < size; ++i) {
                const hcpp::u8 byte =
                    (i >= crc_field && i < crc_field + 4) ? 0 : data[i];
                crc = (crc << 8) ^ crc_table.value[((crc >> 24) ^ byte) & 0xFF];
            }
            return crc;
        }

        hcpp::u32 read_le32(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }

        void finish_packet(OggPackets &out, hcpp::size_t start) noexcept {
            if (out.packet_count >= out.packet_cap) {
                out.truncated = true;
                out.payload_used = start;
                return;
            }
            out.offsets[out.packet_count] = static_cast<hcpp::u32>(start);
            out.sizes[out.packet_count] =
                static_cast<hcpp::u32>(out.payload_used - start);
            ++out.packet_count;
        }

    }  // namespace

    bool ogg_parse_page(const hcpp::u8 *input, hcpp::size_t size, hcpp::size_t offset,
                        OggPage &page) noexcept {
        if (input == nullptr || offset > size || size - offset < 27) return false;
        const hcpp::u8 *h = input + offset;
        if (h[0] != 'O' || h[1] != 'g' || h[2] != 'g' || h[3] != 'S' || h[4] != 0)
            return false;
        page.segment_count = h[26];
        const hcpp::size_t header = 27 + page.segment_count;
        if (size - offset < header) return false;
        hcpp::size_t body = 0;
        for (hcpp::u8 i = 0; i < page.segment_count; ++i) {
            page.lacing[i] = h[27 + i];
            body += h[27 + i];
        }
        if (size - offset < header + body) return false;
        const hcpp::u32 stored = read_le32(h + 22);
        if (ogg_crc(h, header + body, 22) != stored) return false;
        page.flags = h[5];
        page.granule = 0;
        for (int i = 7; i >= 0; --i)
            page.granule = (page.granule << 8) | h[6 + i];
        page.serial = read_le32(h + 14);
        page.sequence = read_le32(h + 18);
        page.crc = stored;
        page.body = h + header;
        page.header_size = header;
        page.body_size = body;
        return true;
    }

    bool ogg_sniff_vorbis(const hcpp::u8 *data, hcpp::size_t size) noexcept {
        if (data == nullptr || size < 7 || data[0] != 0x01) return false;
        const char tag[] = {'v', 'o', 'r', 'b', 'i', 's'};
        for (hcpp::size_t i = 0; i < 6; ++i)
            if (data[1 + i] != static_cast<hcpp::u8>(tag[i])) return false;
        return true;
    }

    CodecStatus audio_ogg_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                                 void *output, Arena &arena) noexcept {
        (void)arena;
        auto *out = static_cast<OggPackets *>(output);
        if (out == nullptr || input == nullptr || input_size < 27 ||
            out->payload == nullptr || out->packet_cap == 0 ||
            out->offsets == nullptr || out->sizes == nullptr)
            return CodecStatus::BadInput;
        out->packet_count = 0;
        out->payload_used = 0;
        out->serial = 0;
        out->truncated = false;
        hcpp::size_t packet_start = 0;
        bool continuing = false;
        hcpp::u32 serial = 0;
        bool serial_set = false;
        hcpp::u32 pages = 0;
        hcpp::size_t offset = 0;
        while (offset < input_size) {
            OggPage page{};
            if (!ogg_parse_page(input, input_size, offset, page)) break;
            if (!serial_set) {
                serial = page.serial;
                serial_set = true;
                out->serial = serial;
            } else if (page.serial != serial) {
                break;
            }
            ++pages;
            if ((page.flags & 0x01) == 0) {
                packet_start = out->payload_used;
                continuing = false;
            } else if (!continuing) {
                packet_start = out->payload_used;
            }
            hcpp::size_t body_offset = 0;
            for (hcpp::u8 s = 0; s < page.segment_count; ++s) {
                const hcpp::u32 lace = page.lacing[s];
                if (lace > out->payload_cap - out->payload_used) {
                    out->truncated = true;
                } else {
                    for (hcpp::u32 i = 0; i < lace; ++i)
                        out->payload[out->payload_used + i] =
                            page.body[body_offset + i];
                    out->payload_used += lace;
                }
                body_offset += lace;
                if (lace < 255) {
                    if (!out->truncated) finish_packet(*out, packet_start);
                    packet_start = out->payload_used;
                    continuing = false;
                } else {
                    continuing = true;
                }
            }
            offset += page.header_size + page.body_size;
        }
        if (pages == 0 || out->packet_count == 0) return CodecStatus::BadInput;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec
