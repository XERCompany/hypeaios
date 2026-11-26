#include "kernel/codecapps/image/image.h"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/registry.h"
#include "kernel/serial.hpp"

namespace hype::codec::image {

    namespace {

        const hcpp::u8 bmp_blob[] = {
            0x42, 0x4D, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00,
            0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x14, 0x0A, 0x3C, 0x32, 0x28,
            0x5A, 0x50, 0x46, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
            0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
        };

        const hcpp::u8 bmp_topdown_blob[] = {
            0x42, 0x4D, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF,
            0xFF, 0xFF, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
            0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0x00, 0x00,
        };

        const hcpp::u8 bmp_palette_blob[] = {
            0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
            0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };

        const hcpp::u8 pnm_blob[] = {
            'P',  '6',  '\n', '3',  ' ',  '2',  '\n', '2',  '5',  '5',  '\n',
            0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF,
            0xFF, 0x00, 0xFF, 0x80, 0x80, 0x80,
        };

        const hcpp::u8 pnm_gray_blob[] = {
            'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 0x40, 0xC0,
        };

        const hcpp::u8 tga_blob[] = {
            0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x02, 0x00, 0x18, 0x20, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
            0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x80, 0x80, 0x80,
        };

        const hcpp::u8 tga_rle_blob[] = {
            0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x02, 0x00, 0x18, 0x20, 0x81, 0x1E, 0x14, 0x0A, 0x01, 0x3C,
            0x32, 0x28, 0x5A, 0x50, 0x46,
        };

        const hcpp::u8 qoi_blob[] = {
            'q',  'o',  'i',  'f',  0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
            0x04, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
            0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        };

        const hcpp::u8 expected_3x3[] = {
            0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0xFF,
            0x0A, 0x14, 0x1E, 0xFF, 0x28, 0x32, 0x3C, 0xFF, 0x46, 0x50, 0x5A, 0xFF,
        };

        const hcpp::u8 expected_3x2[] = {
            0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0xFF,
        };

        const hcpp::u8 expected_rg[] = {
            0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        };

        const hcpp::u8 expected_gray[] = {
            0x40, 0x40, 0x40, 0xFF, 0xC0, 0xC0, 0xC0, 0xFF,
        };

        const hcpp::u8 expected_tga_rle[] = {
            0x0A, 0x14, 0x1E, 0xFF, 0x0A, 0x14, 0x1E, 0xFF,
            0x28, 0x32, 0x3C, 0xFF, 0x46, 0x50, 0x5A, 0xFF,
        };

        const hcpp::u8 expected_qoi[] = {
            0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
            0x00, 0x00, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0xFF,
        };

        hcpp::u8 frame_storage[4096];
        hcpp::u8 frame_again[4096];
        hcpp::u8 encoded_storage[16384];
        hcpp::u8 arena_backing[4096];

        bool pixels_equal(const FrameBuf &frame, const hcpp::u8 *expected,
                          hcpp::u32 count) noexcept {
            if (frame.data == nullptr || frame.pitch != frame.width * 4 ||
                frame.format != video::PixelFormat::RGB32)
                return false;
            for (hcpp::u32 i = 0; i < count * 4; ++i)
                if (frame.data[i] != expected[i]) return false;
            return true;
        }

        bool decode_matches(CodecId id, const hcpp::u8 *blob, hcpp::size_t size,
                            const hcpp::u8 *expected, hcpp::u32 w, hcpp::u32 h,
                            Arena &arena, FrameBuf *frame) noexcept {
            *frame = FrameBuf{};
            frame->data = frame_storage;
            frame->capacity = sizeof(frame_storage);
            if (codec_decode(id, blob, size, frame, arena) != CodecStatus::Ok) return false;
            return frame->width == w && frame->height == h &&
                   pixels_equal(*frame, expected, w * h);
        }

        void write_result(const char *verb, const char *name, bool ok) noexcept {
            serial::write("image ");
            serial::write(verb);
            serial::write("=");
            serial::write(name);
            serial::write(" ");
            serial::write_line(ok ? "ok" : "fail");
        }

        bool roundtrip(CodecId id, const FrameBuf &frame, const hcpp::u8 *expected,
                       Arena &arena) noexcept {
            hcpp::size_t written = 0;
            if (codec_encode(id, &frame, encoded_storage, sizeof(encoded_storage), written,
                             arena) != CodecStatus::Ok ||
                written == 0)
                return false;
            FrameBuf again{};
            again.data = frame_again;
            again.capacity = sizeof(frame_again);
            if (codec_decode(id, encoded_storage, written, &again, arena) != CodecStatus::Ok)
                return false;
            return again.width == frame.width && again.height == frame.height &&
                   pixels_equal(again, expected, frame.width * frame.height);
        }

    }  // namespace

    bool image_simple_selftest() noexcept {
        Arena arena{arena_backing, sizeof(arena_backing), 0};
        FrameBuf frame{};
        bool ok = true;

        bool good = codec_probe(bmp_blob, sizeof(bmp_blob)) == CodecId::Bmp &&
                    decode_matches(CodecId::Bmp, bmp_blob, sizeof(bmp_blob), expected_3x3,
                                   3, 3, arena, &frame) &&
                    decode_matches(CodecId::Bmp, bmp_topdown_blob, sizeof(bmp_topdown_blob),
                                   expected_rg, 2, 1, arena, &frame) &&
                    decode_matches(CodecId::Bmp, bmp_palette_blob,
                                   sizeof(bmp_palette_blob), expected_rg, 2, 1, arena,
                                   &frame) &&
                    decode_matches(CodecId::Bmp, bmp_blob, sizeof(bmp_blob), expected_3x3,
                                   3, 3, arena, &frame);
        write_result("decode", "bmp", good);
        ok = ok && good;
        good = good && roundtrip(CodecId::Bmp, frame, expected_3x3, arena);
        write_result("encode", "bmp", good);
        ok = ok && good;

        good = codec_probe(pnm_blob, sizeof(pnm_blob)) == CodecId::Pnm &&
               decode_matches(CodecId::Pnm, pnm_blob, sizeof(pnm_blob), expected_3x2, 3, 2,
                              arena, &frame) &&
               decode_matches(CodecId::Pnm, pnm_gray_blob, sizeof(pnm_gray_blob),
                              expected_gray, 2, 1, arena, &frame) &&
               decode_matches(CodecId::Pnm, pnm_blob, sizeof(pnm_blob), expected_3x2, 3, 2,
                              arena, &frame);
        write_result("decode", "pnm", good);
        ok = ok && good;
        good = good && roundtrip(CodecId::Pnm, frame, expected_3x2, arena);
        write_result("encode", "pnm", good);
        ok = ok && good;

        good = codec_probe(tga_blob, sizeof(tga_blob)) == CodecId::Tga &&
               decode_matches(CodecId::Tga, tga_blob, sizeof(tga_blob), expected_3x2, 3, 2,
                              arena, &frame) &&
               decode_matches(CodecId::Tga, tga_rle_blob, sizeof(tga_rle_blob),
                              expected_tga_rle, 2, 2, arena, &frame) &&
               decode_matches(CodecId::Tga, tga_blob, sizeof(tga_blob), expected_3x2, 3, 2,
                              arena, &frame);
        write_result("decode", "tga", good);
        ok = ok && good;
        good = good && roundtrip(CodecId::Tga, frame, expected_3x2, arena);
        write_result("encode", "tga", good);
        ok = ok && good;

        good = codec_probe(qoi_blob, sizeof(qoi_blob)) == CodecId::Qoi &&
               decode_matches(CodecId::Qoi, qoi_blob, sizeof(qoi_blob), expected_qoi, 2, 2,
                              arena, &frame);
        write_result("decode", "qoi", good);
        ok = ok && good;
        good = good && roundtrip(CodecId::Qoi, frame, expected_qoi, arena);
        write_result("encode", "qoi", good);
        ok = ok && good;

        return ok;
    }

}  // namespace hype::codec::image
