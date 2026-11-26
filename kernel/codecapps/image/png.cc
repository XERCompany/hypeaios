#include "kernel/codecapps/image/png.h"
#include "hcpp/crc32.h"
#include "kernel/codecapps/image/inflate.h"
#include "kernel/serial.hpp"

namespace hype::codec {

    namespace {

        const hcpp::u8 png_signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

        hcpp::u32 read_be32(const hcpp::u8 *p) noexcept {
            return (static_cast<hcpp::u32>(p[0]) << 24) |
                   (static_cast<hcpp::u32>(p[1]) << 16) |
                   (static_cast<hcpp::u32>(p[2]) << 8) | static_cast<hcpp::u32>(p[3]);
        }

        void write_be32(hcpp::u8 *p, hcpp::u32 value) noexcept {
            p[0] = static_cast<hcpp::u8>((value >> 24) & 0xFF);
            p[1] = static_cast<hcpp::u8>((value >> 16) & 0xFF);
            p[2] = static_cast<hcpp::u8>((value >> 8) & 0xFF);
            p[3] = static_cast<hcpp::u8>(value & 0xFF);
        }

        bool type_eq(const hcpp::u8 *p, const char *name) noexcept {
            return p[0] == static_cast<hcpp::u8>(name[0]) &&
                   p[1] == static_cast<hcpp::u8>(name[1]) &&
                   p[2] == static_cast<hcpp::u8>(name[2]) &&
                   p[3] == static_cast<hcpp::u8>(name[3]);
        }

        void copy_bytes(hcpp::u8 *dst, const hcpp::u8 *src, hcpp::size_t size) noexcept {
            for (hcpp::size_t i = 0; i < size; ++i) dst[i] = src[i];
        }

        bool bytes_equal(const hcpp::u8 *a, const hcpp::u8 *b, hcpp::size_t size) noexcept {
            for (hcpp::size_t i = 0; i < size; ++i)
                if (a[i] != b[i]) return false;
            return true;
        }

        hcpp::size_t bytes_per_pixel(hcpp::u8 color_type) noexcept {
            switch (color_type) {
                case 0:
                case 3:
                    return 1;
                case 4:
                    return 2;
                case 2:
                    return 3;
                case 6:
                    return 4;
            }
            return 0;
        }

        hcpp::u8 paeth(hcpp::u8 a, hcpp::u8 b, hcpp::u8 c) noexcept {
            const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
            const int pa = p > static_cast<int>(a) ? p - static_cast<int>(a)
                                                   : static_cast<int>(a) - p;
            const int pb = p > static_cast<int>(b) ? p - static_cast<int>(b)
                                                   : static_cast<int>(b) - p;
            const int pc = p > static_cast<int>(c) ? p - static_cast<int>(c)
                                                   : static_cast<int>(c) - p;
            if (pa <= pb && pa <= pc) return a;
            if (pb <= pc) return b;
            return c;
        }

        bool unfilter_row(hcpp::u8 *row, const hcpp::u8 *prev, hcpp::size_t stride,
                          hcpp::u8 filter, hcpp::size_t bpp) noexcept {
            switch (filter) {
                case 0:
                    return true;
                case 1:
                    for (hcpp::size_t i = bpp; i < stride; ++i)
                        row[i] = static_cast<hcpp::u8>(row[i] + row[i - bpp]);
                    return true;
                case 2:
                    if (prev != nullptr)
                        for (hcpp::size_t i = 0; i < stride; ++i)
                            row[i] = static_cast<hcpp::u8>(row[i] + prev[i]);
                    return true;
                case 3:
                    for (hcpp::size_t i = 0; i < stride; ++i) {
                        const hcpp::u8 left = i >= bpp ? row[i - bpp] : 0;
                        const hcpp::u8 up = prev != nullptr ? prev[i] : 0;
                        row[i] = static_cast<hcpp::u8>(row[i] +
                                                     static_cast<hcpp::u8>((left + up) >> 1));
                    }
                    return true;
                case 4:
                    for (hcpp::size_t i = 0; i < stride; ++i) {
                        const hcpp::u8 left = i >= bpp ? row[i - bpp] : 0;
                        const hcpp::u8 up = prev != nullptr ? prev[i] : 0;
                        const hcpp::u8 up_left =
                            prev != nullptr && i >= bpp ? prev[i - bpp] : 0;
                        row[i] = static_cast<hcpp::u8>(row[i] + paeth(left, up, up_left));
                    }
                    return true;
            }
            return false;
        }

        struct PngInfo {
            hcpp::u32 width;
            hcpp::u32 height;
            hcpp::u8 color_type;
            const hcpp::u8 *palette;
            hcpp::size_t palette_size;
            hcpp::size_t idat_total;
        };

        bool chunk_crc_ok(const hcpp::u8 *chunk, hcpp::size_t length) noexcept {
            hcpp::u32 crc = hcpp::crc32_update(0, chunk + 4, 4);
            crc = hcpp::crc32_update(crc, chunk + 8, length);
            return crc == read_be32(chunk + 8 + length);
        }

        CodecStatus scan_chunks(const hcpp::u8 *input, hcpp::size_t input_size,
                                PngInfo &info, hcpp::u8 *idat_out) noexcept {
            info.palette = nullptr;
            info.palette_size = 0;
            info.idat_total = 0;
            hcpp::size_t pos = 8;
            bool first = true;
            for (;;) {
                if (pos > input_size || input_size - pos < 12) return CodecStatus::BadInput;
                const hcpp::u8 *chunk = input + pos;
                const hcpp::u32 length = read_be32(chunk);
                if (length > input_size - pos - 12) return CodecStatus::BadInput;
                if (!chunk_crc_ok(chunk, length)) return CodecStatus::BadInput;
                const hcpp::u8 *data = chunk + 8;
                if (first && !type_eq(chunk + 4, "IHDR")) return CodecStatus::BadInput;
                first = false;
                if (type_eq(chunk + 4, "IHDR")) {
                    if (length != 13) return CodecStatus::BadInput;
                    info.width = read_be32(data);
                    info.height = read_be32(data + 4);
                    info.color_type = data[9];
                    if (info.width == 0 || info.height == 0 || data[8] != 8 ||
                        data[10] != 0 || data[11] != 0 || data[12] != 0)
                        return CodecStatus::Unsupported;
                    if (bytes_per_pixel(info.color_type) == 0)
                        return CodecStatus::Unsupported;
                } else if (type_eq(chunk + 4, "PLTE")) {
                    if (length == 0 || (length % 3) != 0 || length > 768)
                        return CodecStatus::BadInput;
                    info.palette = data;
                    info.palette_size = length;
                } else if (type_eq(chunk + 4, "IDAT")) {
                    if (idat_out != nullptr)
                        copy_bytes(idat_out + info.idat_total, data, length);
                    info.idat_total += length;
                } else if (type_eq(chunk + 4, "IEND")) {
                    if (length != 0) return CodecStatus::BadInput;
                    return CodecStatus::Ok;
                }
                pos += 12 + length;
            }
        }

        void emit_pixels(const PngInfo &info, const hcpp::u8 *image,
                         hcpp::u8 *pixels) noexcept {
            const hcpp::size_t bpp = bytes_per_pixel(info.color_type);
            const hcpp::size_t count = static_cast<hcpp::size_t>(info.width) * info.height;
            for (hcpp::size_t i = 0; i < count; ++i) {
                const hcpp::u8 *src = image + i * bpp;
                hcpp::u8 *dst = pixels + i * 4;
                switch (info.color_type) {
                    case 0:
                        dst[0] = src[0];
                        dst[1] = src[0];
                        dst[2] = src[0];
                        dst[3] = 0xFF;
                        break;
                    case 3: {
                        const hcpp::size_t at = static_cast<hcpp::size_t>(src[0]) * 3;
                        if (at + 2 < info.palette_size) {
                            dst[0] = info.palette[at];
                            dst[1] = info.palette[at + 1];
                            dst[2] = info.palette[at + 2];
                        } else {
                            dst[0] = 0;
                            dst[1] = 0;
                            dst[2] = 0;
                        }
                        dst[3] = 0xFF;
                        break;
                    }
                    case 4:
                        dst[0] = src[0];
                        dst[1] = src[0];
                        dst[2] = src[0];
                        dst[3] = src[1];
                        break;
                    case 2:
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 0xFF;
                        break;
                    default:
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = src[3];
                        break;
                }
            }
        }

        bool put_bytes(hcpp::u8 *output, hcpp::size_t output_cap, hcpp::size_t &pos,
                       const hcpp::u8 *data, hcpp::size_t size) noexcept {
            if (size > output_cap || pos > output_cap - size) return false;
            copy_bytes(output + pos, data, size);
            pos += size;
            return true;
        }

        bool put_chunk(hcpp::u8 *output, hcpp::size_t output_cap, hcpp::size_t &pos,
                       const char *name, const hcpp::u8 *data, hcpp::size_t size) noexcept {
            if (size > output_cap || pos > output_cap - size ||
                output_cap - size - pos < 12)
                return false;
            write_be32(output + pos, static_cast<hcpp::u32>(size));
            pos += 4;
            for (int i = 0; i < 4; ++i) output[pos + i] = static_cast<hcpp::u8>(name[i]);
            hcpp::u32 crc = hcpp::crc32_update(0, output + pos, 4);
            if (size != 0) {
                copy_bytes(output + pos + 4, data, size);
                crc = hcpp::crc32_update(crc, output + pos + 4, size);
            }
            write_be32(output + pos + 4 + size, crc);
            pos += 4 + size + 4;
            return true;
        }

    }  // namespace

    CodecStatus png_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept {
        if (input == nullptr || output == nullptr || input_size < 8)
            return CodecStatus::BadInput;
        for (hcpp::size_t i = 0; i < 8; ++i)
            if (input[i] != png_signature[i]) return CodecStatus::BadInput;
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        PngInfo info{};
        const CodecStatus scanned = scan_chunks(input, input_size, info, nullptr);
        if (scanned != CodecStatus::Ok) return scanned;
        if (info.idat_total == 0) return CodecStatus::BadInput;
        if (info.color_type == 3 && info.palette == nullptr) return CodecStatus::BadInput;
        const hcpp::size_t bpp = bytes_per_pixel(info.color_type);
        const hcpp::size_t stride = static_cast<hcpp::size_t>(info.width) * bpp;
        const hcpp::u64 raw_size64 = static_cast<hcpp::u64>(info.height) * (stride + 1);
        const hcpp::u64 image_size64 = static_cast<hcpp::u64>(info.height) * stride;
        const hcpp::u64 pixel_size64 =
            static_cast<hcpp::u64>(info.width) * info.height * 4;
        if (raw_size64 > 0x7FFFFFFFu || pixel_size64 > 0x7FFFFFFFu)
            return CodecStatus::NoSpace;
        const hcpp::size_t raw_size = static_cast<hcpp::size_t>(raw_size64);
        const hcpp::size_t image_size = static_cast<hcpp::size_t>(image_size64);
        const hcpp::size_t pixel_size = static_cast<hcpp::size_t>(pixel_size64);
        hcpp::u8 *idat = static_cast<hcpp::u8 *>(arena_alloc(arena, info.idat_total, 8));
        hcpp::u8 *raw = static_cast<hcpp::u8 *>(arena_alloc(arena, raw_size, 8));
        hcpp::u8 *image = static_cast<hcpp::u8 *>(arena_alloc(arena, image_size, 8));
        if (idat == nullptr || raw == nullptr || image == nullptr)
            return CodecStatus::NoSpace;
        if (scan_chunks(input, input_size, info, idat) != CodecStatus::Ok)
            return CodecStatus::BadInput;
        hcpp::size_t produced = 0;
        if (!inflate_zlib(idat, info.idat_total, raw, raw_size, produced) ||
            produced != raw_size)
            return CodecStatus::BadInput;
        for (hcpp::u32 y = 0; y < info.height; ++y) {
            const hcpp::u8 *src = raw + static_cast<hcpp::size_t>(y) * (stride + 1);
            hcpp::u8 *dst = image + static_cast<hcpp::size_t>(y) * stride;
            copy_bytes(dst, src + 1, stride);
            const hcpp::u8 *prev = y == 0 ? nullptr : dst - stride;
            if (!unfilter_row(dst, prev, stride, src[0], bpp)) return CodecStatus::BadInput;
        }
        hcpp::u8 *pixels = frame->data;
        if (pixels == nullptr || frame->capacity < pixel_size) {
            pixels = static_cast<hcpp::u8 *>(arena_alloc(arena, pixel_size, 8));
            if (pixels == nullptr) return CodecStatus::NoSpace;
        }
        emit_pixels(info, image, pixels);
        frame->width = info.width;
        frame->height = info.height;
        frame->pitch = info.width * 4;
        frame->format = video::PixelFormat::RGB32;
        frame->data = pixels;
        frame->size = pixel_size;
        return CodecStatus::Ok;
    }

    CodecStatus png_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &arena) noexcept {
        written = 0;
        const FrameBuf *frame = static_cast<const FrameBuf *>(input);
        if (frame == nullptr || frame->data == nullptr || frame->width == 0 ||
            frame->height == 0)
            return CodecStatus::BadInput;
        hcpp::u8 color_type = 0;
        hcpp::size_t bpp = 0;
        if (frame->format == video::PixelFormat::RGB24) {
            color_type = 2;
            bpp = 3;
        } else if (frame->format == video::PixelFormat::RGB32) {
            color_type = 6;
            bpp = 4;
        } else {
            return CodecStatus::Unsupported;
        }
        const hcpp::size_t stride = static_cast<hcpp::size_t>(frame->width) * bpp;
        const hcpp::u64 raw_size64 = static_cast<hcpp::u64>(frame->height) * (stride + 1);
        if (raw_size64 > 0x7FFFFFFFu) return CodecStatus::NoSpace;
        const hcpp::size_t raw_size = static_cast<hcpp::size_t>(raw_size64);
        const hcpp::u64 span =
            static_cast<hcpp::u64>(frame->height - 1) * frame->pitch + stride;
        const hcpp::u64 available = frame->size != 0 ? frame->size : frame->capacity;
        if (frame->pitch < stride || span > available) return CodecStatus::BadInput;
        hcpp::u8 *raw = static_cast<hcpp::u8 *>(arena_alloc(arena, raw_size, 8));
        if (raw == nullptr) return CodecStatus::NoSpace;
        for (hcpp::u32 y = 0; y < frame->height; ++y) {
            hcpp::u8 *dst = raw + static_cast<hcpp::size_t>(y) * (stride + 1);
            dst[0] = 0;
            copy_bytes(dst + 1, frame->data + static_cast<hcpp::size_t>(y) * frame->pitch,
                       stride);
        }
        hcpp::size_t pos = 0;
        if (!put_bytes(output, output_cap, pos, png_signature, 8))
            return CodecStatus::NoSpace;
        hcpp::u8 ihdr[13];
        write_be32(ihdr, frame->width);
        write_be32(ihdr + 4, frame->height);
        ihdr[8] = 8;
        ihdr[9] = color_type;
        ihdr[10] = 0;
        ihdr[11] = 0;
        ihdr[12] = 0;
        if (!put_chunk(output, output_cap, pos, "IHDR", ihdr, sizeof(ihdr)))
            return CodecStatus::NoSpace;
        if (output_cap - pos < 8) return CodecStatus::NoSpace;
        const hcpp::size_t length_at = pos;
        pos += 4;
        copy_bytes(output + pos, reinterpret_cast<const hcpp::u8 *>("IDAT"), 4);
        const hcpp::size_t zlib_at = pos + 4;
        if (output_cap - zlib_at < 4) return CodecStatus::NoSpace;
        const hcpp::size_t zlib_size =
            zlib_stored(raw, raw_size, output + zlib_at, output_cap - zlib_at - 4);
        if (zlib_size == 0) return CodecStatus::NoSpace;
        write_be32(output + length_at, static_cast<hcpp::u32>(zlib_size));
        hcpp::u32 crc = hcpp::crc32_update(0, output + pos, 4);
        crc = hcpp::crc32_update(crc, output + zlib_at, zlib_size);
        write_be32(output + zlib_at + zlib_size, crc);
        pos = zlib_at + zlib_size + 4;
        if (!put_chunk(output, output_cap, pos, "IEND", nullptr, 0))
            return CodecStatus::NoSpace;
        written = pos;
        return CodecStatus::Ok;
    }

    namespace {

#include "kernel/codecapps/image/png_selftest_data.h"

        bool selftest_inflate() noexcept {
            struct ZCase {
                const hcpp::u8 *stream;
                hcpp::size_t stream_size;
                const hcpp::u8 *expect;
                hcpp::size_t expect_size;
            };
            const ZCase cases[] = {
                {st_z_dyn, sizeof(st_z_dyn), st_z_dyn_raw, sizeof(st_z_dyn_raw)},
                {st_z_fix, sizeof(st_z_fix), st_z_fix_raw, sizeof(st_z_fix_raw)},
                {st_z_f3, sizeof(st_z_f3), st_z_f3_raw, sizeof(st_z_f3_raw)},
            };
            static hcpp::u8 out[2048];
            for (hcpp::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
                hcpp::size_t produced = 0;
                if (!inflate_zlib(cases[i].stream, cases[i].stream_size, out, sizeof(out),
                                  produced))
                    return false;
                if (produced != cases[i].expect_size ||
                    !bytes_equal(out, cases[i].expect, produced))
                    return false;
            }
            static hcpp::u8 packed[2048];
            const hcpp::size_t packed_size =
                zlib_stored(st_z_dyn_raw, sizeof(st_z_dyn_raw), packed, sizeof(packed));
            if (packed_size == 0) return false;
            hcpp::size_t produced = 0;
            if (!inflate_zlib(packed, packed_size, out, sizeof(out), produced) ||
                produced != sizeof(st_z_dyn_raw) ||
                !bytes_equal(out, st_z_dyn_raw, produced))
                return false;
            const hcpp::size_t raw_size =
                deflate_stored(st_z_fix_raw, sizeof(st_z_fix_raw), packed, sizeof(packed));
            hcpp::size_t consumed = 0;
            produced = 0;
            return raw_size != 0 &&
                   inflate_deflate(packed, raw_size, consumed, out, sizeof(out), produced) &&
                   produced == sizeof(st_z_fix_raw) &&
                   bytes_equal(out, st_z_fix_raw, produced) && consumed == raw_size;
        }

        bool selftest_decode(Arena &arena) noexcept {
            struct PCase {
                const hcpp::u8 *png;
                hcpp::size_t png_size;
                hcpp::u32 width;
                hcpp::u32 height;
                const hcpp::u8 *expect;
            };
            const PCase cases[] = {
                {st_png_rgb, sizeof(st_png_rgb), 6, 5, st_png_rgb_px},
                {st_png_gray, sizeof(st_png_gray), 5, 4, st_png_gray_px},
                {st_png_rgba, sizeof(st_png_rgba), 4, 3, st_png_rgba_px},
                {st_png_pal, sizeof(st_png_pal), 4, 2, st_png_pal_px},
            };
            for (hcpp::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
                arena_reset(arena);
                FrameBuf frame{};
                if (png_decode(cases[i].png, cases[i].png_size, &frame, arena) !=
                    CodecStatus::Ok)
                    return false;
                const hcpp::size_t pixel_size =
                    static_cast<hcpp::size_t>(cases[i].width) * cases[i].height * 4;
                if (frame.width != cases[i].width || frame.height != cases[i].height ||
                    frame.format != video::PixelFormat::RGB32 || frame.size != pixel_size ||
                    !bytes_equal(frame.data, cases[i].expect, pixel_size))
                    return false;
            }
            return true;
        }

        bool selftest_encode(Arena &arena) noexcept {
            arena_reset(arena);
            FrameBuf frame{};
            if (png_decode(st_png_rgb, sizeof(st_png_rgb), &frame, arena) !=
                CodecStatus::Ok)
                return false;
            static hcpp::u8 encoded[4096];
            hcpp::size_t written = 0;
            if (png_encode(&frame, encoded, sizeof(encoded), written, arena) !=
                CodecStatus::Ok)
                return false;
            hcpp::size_t short_write = 0;
            if (png_encode(&frame, encoded, 40, short_write, arena) == CodecStatus::Ok)
                return false;
            arena_reset(arena);
            FrameBuf again{};
            if (png_decode(encoded, written, &again, arena) != CodecStatus::Ok)
                return false;
            const hcpp::size_t pixel_size =
                static_cast<hcpp::size_t>(frame.width) * frame.height * 4;
            return again.width == frame.width && again.height == frame.height &&
                   again.size == pixel_size &&
                   bytes_equal(again.data, st_png_rgb_px, pixel_size);
        }

    }  // namespace

    bool png_selftest() noexcept {
        static hcpp::u8 arena_mem[16384];
        Arena arena{arena_mem, sizeof(arena_mem), 0};
        const bool inflate_ok = selftest_inflate();
        const bool decode_ok = inflate_ok && selftest_decode(arena);
        const bool encode_ok = decode_ok && selftest_encode(arena);
        serial::write("inflate=");
        serial::write_line(inflate_ok ? "ok" : "fail");
        serial::write("png decode=");
        serial::write_line(decode_ok ? "ok" : "fail");
        serial::write("png encode=");
        serial::write_line(encode_ok ? "ok" : "fail");
        return inflate_ok && decode_ok && encode_ok;
    }

}  // namespace hype::codec
