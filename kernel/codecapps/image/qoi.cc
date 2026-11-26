#include "kernel/codecapps/image/qoi.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {
        constexpr hcpp::size_t qoi_header_size = 14;
        constexpr hcpp::size_t qoi_end_size = 8;
        constexpr hcpp::u8 qoi_op_index = 0x00;
        constexpr hcpp::u8 qoi_op_diff = 0x40;
        constexpr hcpp::u8 qoi_op_luma = 0x80;
        constexpr hcpp::u8 qoi_op_run = 0xC0;
        constexpr hcpp::u8 qoi_op_rgb = 0xFE;
        constexpr hcpp::u8 qoi_op_rgba = 0xFF;
        constexpr hcpp::u8 qoi_mask = 0xC0;

        struct QoiPixel {
            hcpp::u8 r, g, b, a;
        };

        hcpp::u32 qoi_hash(const QoiPixel &px) noexcept {
            return (px.r * 3u + px.g * 5u + px.b * 7u + px.a * 11u) % 64;
        }

        bool qoi_pixels_equal(const QoiPixel &a, const QoiPixel &b) noexcept {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }
    }  // namespace

    CodecStatus qoi_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (input == nullptr || frame == nullptr || input_size < qoi_header_size)
            return CodecStatus::BadInput;
        if (input[0] != 'q' || input[1] != 'o' || input[2] != 'i' || input[3] != 'f')
            return CodecStatus::BadInput;
        const hcpp::u32 w = read_be32(input + 4);
        const hcpp::u32 h = read_be32(input + 8);
        const hcpp::u8 channels = input[12];
        if (!dims_valid(w, h)) return CodecStatus::BadInput;
        if (channels != 3 && channels != 4) return CodecStatus::Unsupported;
        const CodecStatus prepared = frame_prepare(frame, w, h, arena);
        if (prepared != CodecStatus::Ok) return prepared;
        QoiPixel index[64]{};
        QoiPixel px{0, 0, 0, 0xFF};
        hcpp::size_t pos = qoi_header_size;
        const hcpp::u32 total = w * h;
        hcpp::u32 produced = 0;
        while (produced < total) {
            if (pos >= input_size) return CodecStatus::BadInput;
            const hcpp::u8 b1 = input[pos++];
            hcpp::u32 run = 0;
            bool index_hit = false;
            if (b1 == qoi_op_rgb) {
                if (input_size - pos < 3) return CodecStatus::BadInput;
                px.r = input[pos];
                px.g = input[pos + 1];
                px.b = input[pos + 2];
                pos += 3;
            } else if (b1 == qoi_op_rgba) {
                if (input_size - pos < 4) return CodecStatus::BadInput;
                px.r = input[pos];
                px.g = input[pos + 1];
                px.b = input[pos + 2];
                px.a = input[pos + 3];
                pos += 4;
            } else {
                switch (b1 & qoi_mask) {
                    case qoi_op_index:
                        px = index[b1 & 0x3F];
                        index_hit = true;
                        break;
                    case qoi_op_diff:
                        px.r = static_cast<hcpp::u8>(px.r + ((b1 >> 4) & 0x03) - 2);
                        px.g = static_cast<hcpp::u8>(px.g + ((b1 >> 2) & 0x03) - 2);
                        px.b = static_cast<hcpp::u8>(px.b + (b1 & 0x03) - 2);
                        break;
                    case qoi_op_luma: {
                        if (pos >= input_size) return CodecStatus::BadInput;
                        const hcpp::u8 b2 = input[pos++];
                        const int dg = (b1 & 0x3F) - 32;
                        px.r = static_cast<hcpp::u8>(px.r + dg + ((b2 >> 4) & 0x0F) - 8);
                        px.g = static_cast<hcpp::u8>(px.g + dg);
                        px.b = static_cast<hcpp::u8>(px.b + dg + (b2 & 0x0F) - 8);
                        break;
                    }
                    default:
                        run = static_cast<hcpp::u32>(b1 & 0x3F) + 1;
                        if (run > total - produced) return CodecStatus::BadInput;
                        break;
                }
            }
            if (!index_hit && run == 0) index[qoi_hash(px)] = px;
            const hcpp::u32 emit = run != 0 ? run : 1;
            for (hcpp::u32 i = 0; i < emit; ++i)
                put_pixel(*frame, produced + i, px.r, px.g, px.b, px.a);
            produced += emit;
        }
        return CodecStatus::Ok;
    }

    CodecStatus qoi_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &) noexcept {
        written = 0;
        const FrameBuf *frame = static_cast<const FrameBuf *>(input);
        if (!frame_source_valid(frame) || output == nullptr) return CodecStatus::BadInput;
        const hcpp::u32 w = frame->width;
        const hcpp::u32 h = frame->height;
        const hcpp::u8 channels = frame->format == video::PixelFormat::RGB32 ? 4 : 3;
        const hcpp::u32 total = w * h;
        const hcpp::size_t worst = qoi_header_size + static_cast<hcpp::size_t>(total) * 5 +
                                   qoi_end_size;
        if (output_cap < worst) return CodecStatus::NoSpace;
        output[0] = 'q';
        output[1] = 'o';
        output[2] = 'i';
        output[3] = 'f';
        write_be32(output + 4, w);
        write_be32(output + 8, h);
        output[12] = channels;
        output[13] = 0;
        hcpp::size_t pos = qoi_header_size;
        QoiPixel index[64]{};
        QoiPixel prev{0, 0, 0, 0xFF};
        hcpp::u32 run = 0;
        for (hcpp::u32 i = 0; i < total; ++i) {
            const hcpp::u32 x = i % w;
            const hcpp::u32 y = i / w;
            QoiPixel px{source_component(*frame, x, y, 0), source_component(*frame, x, y, 1),
                        source_component(*frame, x, y, 2),
                        static_cast<hcpp::u8>(
                            channels == 4 ? source_component(*frame, x, y, 3) : 0xFF)};
            if (qoi_pixels_equal(px, prev)) {
                ++run;
                if (run == 62 || i == total - 1) {
                    output[pos++] = static_cast<hcpp::u8>(qoi_op_run | (run - 1));
                    run = 0;
                }
                continue;
            }
            if (run != 0) {
                output[pos++] = static_cast<hcpp::u8>(qoi_op_run | (run - 1));
                run = 0;
            }
            const hcpp::u32 slot = qoi_hash(px);
            if (qoi_pixels_equal(index[slot], px)) {
                output[pos++] = static_cast<hcpp::u8>(qoi_op_index | slot);
            } else {
                index[slot] = px;
                if (px.a == prev.a) {
                    const int dr = static_cast<int>(px.r) - prev.r;
                    const int dg = static_cast<int>(px.g) - prev.g;
                    const int db = static_cast<int>(px.b) - prev.b;
                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                        output[pos++] = static_cast<hcpp::u8>(
                            qoi_op_diff | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2));
                    } else if (dg >= -32 && dg <= 31 && dr - dg >= -8 && dr - dg <= 7 &&
                               db - dg >= -8 && db - dg <= 7) {
                        output[pos++] = static_cast<hcpp::u8>(qoi_op_luma | (dg + 32));
                        output[pos++] = static_cast<hcpp::u8>(((dr - dg + 8) << 4) |
                                                              (db - dg + 8));
                    } else {
                        output[pos++] = qoi_op_rgb;
                        output[pos++] = px.r;
                        output[pos++] = px.g;
                        output[pos++] = px.b;
                    }
                } else {
                    output[pos++] = qoi_op_rgba;
                    output[pos++] = px.r;
                    output[pos++] = px.g;
                    output[pos++] = px.b;
                    output[pos++] = px.a;
                }
            }
            prev = px;
        }
        for (hcpp::size_t i = 0; i < 7; ++i) output[pos++] = 0;
        output[pos++] = 1;
        written = pos;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
