#include "kernel/codecapps/image/pnm.h"
#include "kernel/codecapps/image/imageutil.h"

namespace hype::codec::image {

    namespace {

        bool pnm_space(hcpp::u8 c) noexcept {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        }

        bool pnm_digit(hcpp::u8 c) noexcept { return c >= '0' && c <= '9'; }

        void pnm_skip(const hcpp::u8 *input, hcpp::size_t size, hcpp::size_t &pos) noexcept {
            for (;;) {
                while (pos < size && pnm_space(input[pos])) ++pos;
                if (pos < size && input[pos] == '#') {
                    while (pos < size && input[pos] != '\n') ++pos;
                    continue;
                }
                return;
            }
        }

        bool pnm_uint(const hcpp::u8 *input, hcpp::size_t size, hcpp::size_t &pos,
                      hcpp::u32 &out) noexcept {
            pnm_skip(input, size, pos);
            if (pos >= size || !pnm_digit(input[pos])) return false;
            hcpp::u32 value = 0;
            while (pos < size && pnm_digit(input[pos])) {
                value = value * 10 + static_cast<hcpp::u32>(input[pos] - '0');
                if (value > 1000000) return false;
                ++pos;
            }
            out = value;
            return true;
        }

        hcpp::u8 pnm_scale(hcpp::u32 value, hcpp::u32 maxval) noexcept {
            if (value > maxval) value = maxval;
            return static_cast<hcpp::u8>((value * 255 + maxval / 2) / maxval);
        }

        hcpp::size_t pnm_write_u32(hcpp::u8 *output, hcpp::size_t pos,
                                   hcpp::u32 value) noexcept {
            char digits[10];
            hcpp::size_t count = 0;
            do {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);
            while (count != 0) output[pos++] = static_cast<hcpp::u8>(digits[--count]);
            return pos;
        }

    }  // namespace

    CodecStatus pnm_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept {
        FrameBuf *frame = static_cast<FrameBuf *>(output);
        if (input == nullptr || frame == nullptr || input_size < 3 || input[0] != 'P')
            return CodecStatus::BadInput;
        const hcpp::u8 kind = static_cast<hcpp::u8>(input[1] - '0');
        if (kind != 2 && kind != 3 && kind != 5 && kind != 6) return CodecStatus::Unsupported;
        hcpp::size_t pos = 2;
        hcpp::u32 w = 0, h = 0, maxval = 0;
        if (!pnm_uint(input, input_size, pos, w) || !pnm_uint(input, input_size, pos, h) ||
            !pnm_uint(input, input_size, pos, maxval))
            return CodecStatus::BadInput;
        if (!dims_valid(w, h) || maxval == 0) return CodecStatus::BadInput;
        if (maxval > 255) return CodecStatus::Unsupported;
        const bool binary = kind == 5 || kind == 6;
        const bool color = kind == 3 || kind == 6;
        if (binary) {
            if (pos >= input_size || !pnm_space(input[pos])) return CodecStatus::BadInput;
            ++pos;
        }
        const hcpp::size_t samples = static_cast<hcpp::size_t>(w) * h * (color ? 3 : 1);
        if (binary && (pos > input_size || samples > input_size - pos))
            return CodecStatus::BadInput;
        const CodecStatus prepared = frame_prepare(frame, w, h, arena);
        if (prepared != CodecStatus::Ok) return prepared;
        const hcpp::size_t pixels = static_cast<hcpp::size_t>(w) * h;
        for (hcpp::size_t i = 0; i < pixels; ++i) {
            hcpp::u8 r = 0, g = 0, b = 0;
            if (binary && color) {
                const hcpp::u8 *px = input + pos + i * 3;
                r = pnm_scale(px[0], maxval);
                g = pnm_scale(px[1], maxval);
                b = pnm_scale(px[2], maxval);
            } else if (binary) {
                r = g = b = pnm_scale(input[pos + i], maxval);
            } else if (color) {
                hcpp::u32 vr = 0, vg = 0, vb = 0;
                if (!pnm_uint(input, input_size, pos, vr) ||
                    !pnm_uint(input, input_size, pos, vg) ||
                    !pnm_uint(input, input_size, pos, vb))
                    return CodecStatus::BadInput;
                r = pnm_scale(vr, maxval);
                g = pnm_scale(vg, maxval);
                b = pnm_scale(vb, maxval);
            } else {
                hcpp::u32 v = 0;
                if (!pnm_uint(input, input_size, pos, v)) return CodecStatus::BadInput;
                r = g = b = pnm_scale(v, maxval);
            }
            put_pixel(*frame, static_cast<hcpp::u32>(i), r, g, b, 0xFF);
        }
        return CodecStatus::Ok;
    }

    CodecStatus pnm_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                           hcpp::size_t &written, Arena &) noexcept {
        written = 0;
        const FrameBuf *frame = static_cast<const FrameBuf *>(input);
        if (!frame_source_valid(frame) || output == nullptr) return CodecStatus::BadInput;
        const hcpp::u32 w = frame->width;
        const hcpp::u32 h = frame->height;
        const hcpp::size_t pixel_bytes = static_cast<hcpp::size_t>(w) * h * 3;
        if (output_cap < pixel_bytes + 16) return CodecStatus::NoSpace;
        hcpp::size_t pos = 0;
        output[pos++] = 'P';
        output[pos++] = '6';
        output[pos++] = '\n';
        pos = pnm_write_u32(output, pos, w);
        output[pos++] = ' ';
        pos = pnm_write_u32(output, pos, h);
        output[pos++] = '\n';
        output[pos++] = '2';
        output[pos++] = '5';
        output[pos++] = '5';
        output[pos++] = '\n';
        if (output_cap - pos < pixel_bytes) return CodecStatus::NoSpace;
        const hcpp::size_t pixels = static_cast<hcpp::size_t>(w) * h;
        for (hcpp::size_t i = 0; i < pixels; ++i) {
            const hcpp::u32 x = static_cast<hcpp::u32>(i % w);
            const hcpp::u32 y = static_cast<hcpp::u32>(i / w);
            output[pos++] = source_component(*frame, x, y, 0);
            output[pos++] = source_component(*frame, x, y, 1);
            output[pos++] = source_component(*frame, x, y, 2);
        }
        written = pos;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
