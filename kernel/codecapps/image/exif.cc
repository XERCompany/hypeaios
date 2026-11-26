#include "kernel/codecapps/image/exif.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {

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

        hcpp::u32 entry_type_size(hcpp::u16 type) noexcept {
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
                case 11:
                    return 4;
                case 5:
                case 10:
                case 12:
                    return 8;
                default:
                    return 0;
            }
        }

        const hcpp::u8 *entry_data(const hcpp::u8 *tiff, hcpp::size_t tiff_size,
                                   const hcpp::u8 *entry, const TiffOrder &order,
                                   hcpp::u16 type, hcpp::u32 count) noexcept {
            const hcpp::u32 unit = entry_type_size(type);
            if (unit == 0 || count == 0 || count > 0x100000u) return nullptr;
            const hcpp::size_t total = static_cast<hcpp::size_t>(unit) * count;
            if (total <= 4) return entry + 8;
            const hcpp::u32 offset = order.rd32(entry + 8);
            if (!bounds_ok(offset, total, tiff_size)) return nullptr;
            return tiff + offset;
        }

        void copy_ascii(char *dst, hcpp::size_t dst_cap, const hcpp::u8 *src,
                        hcpp::u32 count) noexcept {
            if (dst_cap == 0) return;
            hcpp::size_t out = 0;
            for (hcpp::u32 i = 0; i < count && out + 1 < dst_cap; ++i) {
                const char c = static_cast<char>(src[i]);
                if (c == '\0' || c < 32 || c > 126) break;
                dst[out++] = c;
            }
            dst[out] = '\0';
        }

    }  // namespace

    CodecStatus exif_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                            Arena &) noexcept {
        ExifInfo *info = static_cast<ExifInfo *>(output);
        if (src == nullptr || info == nullptr || size < 8) return CodecStatus::BadInput;
        const hcpp::u8 *tiff = src;
        hcpp::size_t tiff_size = size;
        if (bounds_ok(0, 6, size) && src[0] == 'E' && src[1] == 'x' && src[2] == 'i' &&
            src[3] == 'f' && src[4] == 0 && src[5] == 0) {
            tiff += 6;
            tiff_size -= 6;
        }
        if (tiff_size < 8) return CodecStatus::BadInput;
        TiffOrder order;
        if (tiff[0] == 'I' && tiff[1] == 'I')
            order.big_endian = false;
        else if (tiff[0] == 'M' && tiff[1] == 'M')
            order.big_endian = true;
        else
            return CodecStatus::BadInput;
        if (order.rd16(tiff + 2) != 42) return CodecStatus::BadInput;
        const hcpp::u32 ifd0 = order.rd32(tiff + 4);
        if (!bounds_ok(ifd0, 2, tiff_size)) return CodecStatus::BadInput;
        for (hcpp::size_t i = 0; i < sizeof(ExifInfo); ++i)
            reinterpret_cast<hcpp::u8 *>(info)[i] = 0;
        const hcpp::u16 count = order.rd16(tiff + ifd0);
        if (!bounds_ok(ifd0 + 2, static_cast<hcpp::size_t>(count) * 12, tiff_size))
            return CodecStatus::BadInput;
        info->tag_count = count;
        for (hcpp::u16 i = 0; i < count; ++i) {
            const hcpp::u8 *entry = tiff + ifd0 + 2 + static_cast<hcpp::size_t>(i) * 12;
            const hcpp::u16 tag = order.rd16(entry);
            const hcpp::u16 type = order.rd16(entry + 2);
            const hcpp::u32 num = order.rd32(entry + 4);
            const hcpp::u8 *data = entry_data(tiff, tiff_size, entry, order, type, num);
            if (tag == 0x0112 && type == 3 && num >= 1 && data != nullptr) {
                info->orientation = order.rd16(data);
            } else if (tag == 0x010F && type == 2 && data != nullptr) {
                copy_ascii(info->make, sizeof(info->make), data, num);
            } else if (tag == 0x0110 && type == 2 && data != nullptr) {
                copy_ascii(info->model, sizeof(info->model), data, num);
            } else if (tag == 0x0132 && type == 2 && data != nullptr) {
                copy_ascii(info->datetime, sizeof(info->datetime), data, num);
            }
        }
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
