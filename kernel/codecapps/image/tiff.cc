#include "kernel/codecapps/image/tiff.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {

        constexpr hcpp::size_t tiff_max_strips = 128;

        bool bounds_ok(hcpp::size_t offset, hcpp::size_t length,
                       hcpp::size_t total) noexcept {
            return offset <= total && length <= total - offset;
        }

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        struct TiffOrder {
            bool big_endian;
            hcpp::u16 rd16(const hcpp::u8 *p) const noexcept {
                return big_endian ? read_be16(p) : read_le16(p);
            }
            hcpp::u32 rd32(const hcpp::u8 *p) const noexcept {
                return big_endian ? read_be32(p) : read_le32(p);
            }
        };

        hcpp::u32 type_size(hcpp::u16 type) noexcept {
            switch (type) {
                case 1:
                case 2:
                case 6:
                case 7:
                    return 1;
                case 3:
                case 8:
                    return 2;
                case 4:
                case 9:
                    return 4;
                case 5:
                case 10:
                    return 8;
                default:
                    return 0;
            }
        }

        hcpp::u32 entry_values(const hcpp::u8 *tiff, hcpp::size_t tiff_size,
                               const hcpp::u8 *entry, const TiffOrder &order,
                               hcpp::u32 *values, hcpp::u32 max_values) noexcept {
            const hcpp::u16 type = order.rd16(entry + 2);
            const hcpp::u32 count = order.rd32(entry + 4);
            const hcpp::u32 unit = type_size(type);
            if (unit == 0 || count == 0 || unit > 4) return 0;
            const hcpp::u8 *data;
            const hcpp::size_t total = static_cast<hcpp::size_t>(unit) * count;
            if (total <= 4) {
                data = entry + 8;
            } else {
                const hcpp::u32 offset = order.rd32(entry + 8);
                if (!bounds_ok(offset, total, tiff_size)) return 0;
                data = tiff + offset;
            }
            const hcpp::u32 take = count < max_values ? count : max_values;
            for (hcpp::u32 i = 0; i < take; ++i) {
                if (unit == 1)
                    values[i] = data[i];
                else if (unit == 2)
                    values[i] = order.rd16(data + i * 2);
                else
                    values[i] = order.rd32(data + i * 4);
            }
            return take;
        }

        hcpp::u32 entry_scalar(const hcpp::u8 *tiff, hcpp::size_t tiff_size,
                               const hcpp::u8 *entry, const TiffOrder &order,
                               hcpp::u32 fallback) noexcept {
            hcpp::u32 value = fallback;
            if (entry_values(tiff, tiff_size, entry, order, &value, 1) != 1)
                return fallback;
            return value;
        }

        struct TiffBitReader {
            const hcpp::u8 *src;
            hcpp::size_t size;
            hcpp::size_t pos;
            hcpp::u32 bit_buffer;
            hcpp::u32 bit_count;

            int read(hcpp::u32 width) noexcept {
                while (bit_count < width) {
                    if (pos >= size) return -1;
                    bit_buffer = (bit_buffer << 8) | src[pos++];
                    bit_count += 8;
                }
                bit_count -= width;
                return static_cast<int>((bit_buffer >> bit_count) &
                                      ((1u << width) - 1u));
            }
        };

        hcpp::size_t lzw_decompress(TiffBitReader &reader, hcpp::u8 *out,
                                    hcpp::size_t out_cap) noexcept {
            static hcpp::u16 prefix[4096];
            static hcpp::u8 suffix[4096];
            static hcpp::u8 stack[4096];
            const hcpp::u32 clear = 256;
            const hcpp::u32 eoi = 257;
            hcpp::u32 width = 9;
            hcpp::u32 next = 258;
            hcpp::size_t produced = 0;
            hcpp::u8 first = 0;
            int previous = -1;
            for (;;) {
                const int code = reader.read(width);
                if (code < 0) return produced;
                if (static_cast<hcpp::u32>(code) == eoi) return produced;
                if (static_cast<hcpp::u32>(code) == clear) {
                    width = 9;
                    next = 258;
                    previous = -1;
                    continue;
                }
                if (previous < 0) {
                    if (static_cast<hcpp::u32>(code) >= clear || produced >= out_cap)
                        return produced;
                    first = static_cast<hcpp::u8>(code);
                    out[produced++] = first;
                    previous = code;
                    continue;
                }
                const hcpp::u32 in_code = static_cast<hcpp::u32>(code);
                hcpp::u32 walk;
                hcpp::size_t sp = 0;
                if (in_code == next) {
                    stack[sp++] = first;
                    walk = static_cast<hcpp::u32>(previous);
                } else if (in_code < next) {
                    walk = in_code;
                } else {
                    return produced;
                }
                while (walk >= clear) {
                    if (walk >= 4096 || sp >= sizeof(stack)) return produced;
                    stack[sp++] = suffix[walk];
                    walk = prefix[walk];
                }
                first = static_cast<hcpp::u8>(walk);
                stack[sp++] = first;
                if (next < 4096) {
                    prefix[next] = static_cast<hcpp::u16>(previous);
                    suffix[next] = first;
                    ++next;
                    if (next == (1u << width) - 1u && width < 12) ++width;
                }
                while (sp != 0) {
                    if (produced >= out_cap) return produced;
                    out[produced++] = stack[--sp];
                }
                previous = static_cast<int>(in_code);
            }
        }

        hcpp::size_t packbits_decompress(const hcpp::u8 *src, hcpp::size_t size,
                                         hcpp::u8 *out, hcpp::size_t out_cap) noexcept {
            hcpp::size_t in_pos = 0;
            hcpp::size_t produced = 0;
            while (in_pos < size && produced < out_cap) {
                const hcpp::i8 n = static_cast<hcpp::i8>(src[in_pos++]);
                if (n >= 0) {
                    const hcpp::u32 count = static_cast<hcpp::u32>(n) + 1;
                    if (count > size - in_pos) break;
                    for (hcpp::u32 i = 0; i < count && produced < out_cap; ++i)
                        out[produced++] = src[in_pos++];
                } else if (n != -128) {
                    const hcpp::u32 count = 1u + static_cast<hcpp::u32>(-n);
                    if (in_pos >= size) break;
                    const hcpp::u8 value = src[in_pos++];
                    for (hcpp::u32 i = 0; i < count && produced < out_cap; ++i)
                        out[produced++] = value;
                }
            }
            return produced;
        }

        void undo_predictor(hcpp::u8 *row, hcpp::u32 width, hcpp::u32 channels) noexcept {
            for (hcpp::u32 i = channels; i < width * channels; ++i)
                row[i] = static_cast<hcpp::u8>(row[i] + row[i - channels]);
        }

    }  // namespace

    CodecStatus tiff_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                            Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (src == nullptr || frame == nullptr || size < 8) return CodecStatus::BadInput;
        TiffOrder order;
        if (src[0] == 'I' && src[1] == 'I')
            order.big_endian = false;
        else if (src[0] == 'M' && src[1] == 'M')
            order.big_endian = true;
        else
            return CodecStatus::BadInput;
        if (order.rd16(src + 2) != 42) return CodecStatus::BadInput;
        const hcpp::u32 ifd0 = order.rd32(src + 4);
        if (!bounds_ok(ifd0, 2, size)) return CodecStatus::BadInput;
        const hcpp::u16 count = order.rd16(src + ifd0);
        if (!bounds_ok(ifd0 + 2, static_cast<hcpp::size_t>(count) * 12, size))
            return CodecStatus::BadInput;

        hcpp::u32 width = 0, height = 0, compression = 1, photometric = 1;
        hcpp::u32 samples = 1, rows_per_strip = 0, predictor = 1, planar = 1;
        hcpp::u32 bits[4] = {8, 8, 8, 8};
        static hcpp::u32 strip_offsets[tiff_max_strips];
        static hcpp::u32 strip_counts[tiff_max_strips];
        hcpp::u32 strips = 0;
        hcpp::u32 strip_count_fields = 0;

        for (hcpp::u16 i = 0; i < count; ++i) {
            const hcpp::u8 *entry = src + ifd0 + 2 + static_cast<hcpp::size_t>(i) * 12;
            const hcpp::u16 tag = order.rd16(entry);
            switch (tag) {
                case 256:
                    width = entry_scalar(src, size, entry, order, 0);
                    break;
                case 257:
                    height = entry_scalar(src, size, entry, order, 0);
                    break;
                case 258:
                    entry_values(src, size, entry, order, bits, 4);
                    break;
                case 259:
                    compression = entry_scalar(src, size, entry, order, 1);
                    break;
                case 262:
                    photometric = entry_scalar(src, size, entry, order, 1);
                    break;
                case 273:
                    strips = entry_values(src, size, entry, order, strip_offsets,
                                          tiff_max_strips);
                    break;
                case 277:
                    samples = entry_scalar(src, size, entry, order, 1);
                    break;
                case 278:
                    rows_per_strip = entry_scalar(src, size, entry, order, 0);
                    break;
                case 279:
                    strip_count_fields = entry_values(src, size, entry, order,
                                                      strip_counts, tiff_max_strips);
                    break;
                case 284:
                    planar = entry_scalar(src, size, entry, order, 1);
                    break;
                case 317:
                    predictor = entry_scalar(src, size, entry, order, 1);
                    break;
                default:
                    break;
            }
        }

        if (!dims_valid(width, height)) return CodecStatus::BadInput;
        if (samples != 1 && samples != 3) return CodecStatus::Unsupported;
        if (planar != 1 || predictor > 2) return CodecStatus::Unsupported;
        for (hcpp::u32 i = 0; i < samples; ++i)
            if (bits[i] != 8) return CodecStatus::Unsupported;
        if (samples == 1 && photometric > 1) return CodecStatus::Unsupported;
        if (samples == 3 && photometric != 2) return CodecStatus::Unsupported;
        if (compression != 1 && compression != 5 && compression != 32773)
            return CodecStatus::Unsupported;
        if (strips == 0 || strip_count_fields < strips) return CodecStatus::BadInput;
        if (rows_per_strip == 0 || rows_per_strip > height) rows_per_strip = height;

        const hcpp::size_t row_bytes = static_cast<hcpp::size_t>(width) * samples;
        const hcpp::size_t raster_bytes = row_bytes * height;
        hcpp::u8 *raster = static_cast<hcpp::u8 *>(arena_alloc(arena, raster_bytes, 8));
        if (raster == nullptr) return CodecStatus::NoSpace;

        hcpp::u32 row = 0;
        for (hcpp::u32 s = 0; s < strips && row < height; ++s) {
            const hcpp::u32 strip_rows =
                rows_per_strip < height - row ? rows_per_strip : height - row;
            const hcpp::size_t want = row_bytes * strip_rows;
            const hcpp::u32 off = strip_offsets[s];
            const hcpp::u32 len = strip_counts[s];
            if (!bounds_ok(off, len, size)) return CodecStatus::BadInput;
            hcpp::u8 *dst = raster + static_cast<hcpp::size_t>(row) * row_bytes;
            hcpp::size_t got = 0;
            if (compression == 1) {
                got = len < want ? len : want;
                for (hcpp::size_t i = 0; i < got; ++i) dst[i] = src[off + i];
            } else if (compression == 32773) {
                got = packbits_decompress(src + off, len, dst, want);
            } else {
                TiffBitReader reader{src + off, len, 0, 0, 0};
                got = lzw_decompress(reader, dst, want);
            }
            if (got < want) return CodecStatus::BadInput;
            if (predictor == 2)
                for (hcpp::u32 r = 0; r < strip_rows; ++r)
                    undo_predictor(dst + static_cast<hcpp::size_t>(r) * row_bytes, width,
                                   samples);
            row += strip_rows;
        }
        if (row < height) return CodecStatus::BadInput;

        const CodecStatus status = frame_prepare(frame, width, height, arena);
        if (status != CodecStatus::Ok) return status;
        for (hcpp::u32 y = 0; y < height; ++y) {
            const hcpp::u8 *src_row = raster + static_cast<hcpp::size_t>(y) * row_bytes;
            for (hcpp::u32 x = 0; x < width; ++x) {
                if (samples == 1) {
                    hcpp::u8 v = src_row[x];
                    if (photometric == 0) v = static_cast<hcpp::u8>(255 - v);
                    put_pixel(*frame, y * width + x, v, v, v, 0xFF);
                } else {
                    put_pixel(*frame, y * width + x, src_row[x * 3 + 0],
                              src_row[x * 3 + 1], src_row[x * 3 + 2], 0xFF);
                }
            }
        }
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
