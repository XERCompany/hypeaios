#include "kernel/codecapps/image/apng.h"

#include "kernel/codecapps/image/imageutil.h"
#include "kernel/codecapps/image/png.h"

// APNG decoder (wave-2): chunk walk over acTL/fcTL/fdAT on top of the
// shared PNG scanline pipeline. dispose_op applies to the previous
// frame's rect before the next frame is drawn — same shape as
// gif.cc's disposal handling.

namespace hype::codec::image {

    namespace {

        const hcpp::u8 apng_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

        bool type_eq(const hcpp::u8 *p, const char *name) noexcept {
            return p[0] == static_cast<hcpp::u8>(name[0]) &&
                   p[1] == static_cast<hcpp::u8>(name[1]) &&
                   p[2] == static_cast<hcpp::u8>(name[2]) &&
                   p[3] == static_cast<hcpp::u8>(name[3]);
        }

        bool bounds_ok(hcpp::size_t off, hcpp::size_t len, hcpp::size_t total) noexcept {
            return off <= total && len <= total - off;
        }

        hcpp::u16 read_be16(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u16>((static_cast<hcpp::u16>(p[0]) << 8) | p[1]);
        }

        void fill_transparent(FrameBuf &canvas, hcpp::u32 x, hcpp::u32 y, hcpp::u32 w,
                              hcpp::u32 h) noexcept {
            for (hcpp::u32 yy = 0; yy < h && y + yy < canvas.height; ++yy)
                for (hcpp::u32 xx = 0; xx < w && x + xx < canvas.width; ++xx) {
                    hcpp::u8 *p = canvas.data +
                                  static_cast<hcpp::size_t>(y + yy) * canvas.pitch +
                                  static_cast<hcpp::size_t>(x + xx) * 4;
                    p[0] = p[1] = p[2] = p[3] = 0;
                }
        }

        struct FrameCtl {
            hcpp::u32 w, h, x, y;
            hcpp::u16 delay_num, delay_den;
            hcpp::u8 dispose, blend;
            bool valid;
        };

        // Walks chunks from `pos`; on success returns the chunk type at
        // `pos` in `tag` and the payload length in `len` (pos points at
        // the chunk length field).
        bool chunk_at(const hcpp::u8 *src, hcpp::size_t size, hcpp::size_t pos,
                      hcpp::u32 &len) noexcept {
            if (!bounds_ok(pos, 8, size)) return false;
            len = read_be32(src + pos);
            return bounds_ok(pos, 12 + static_cast<hcpp::size_t>(len), size);
        }

        bool parse_fctl(const hcpp::u8 *data, hcpp::u32 len, FrameCtl &fc) noexcept {
            if (len != 26) return false;
            fc.w = read_be32(data + 4);
            fc.h = read_be32(data + 8);
            fc.x = read_be32(data + 12);
            fc.y = read_be32(data + 16);
            fc.delay_num = read_be16(data + 20);
            fc.delay_den = read_be16(data + 22);
            fc.dispose = data[24];
            fc.blend = data[25];
            fc.valid = true;
            return fc.w != 0 && fc.h != 0 && fc.dispose <= 2 && fc.blend <= 1;
        }

        void snapshot(ApngAnim &anim, const FrameBuf &canvas, const FrameCtl &fc,
                      hcpp::u32 cw, hcpp::u32 ch, Arena &arena) noexcept {
            const hcpp::size_t need = static_cast<hcpp::size_t>(cw) * ch * 4;
            if (anim.restore_cap < need) {
                anim.restore = static_cast<hcpp::u8 *>(arena_alloc(arena, need, 8));
                if (anim.restore == nullptr) return;
                anim.restore_cap = need;
            }
            for (hcpp::u32 yy = 0; yy < ch; ++yy) {
                const hcpp::u8 *row =
                    canvas.data + static_cast<hcpp::size_t>(fc.y + yy) * canvas.pitch +
                    static_cast<hcpp::size_t>(fc.x) * 4;
                hcpp::u8 *saved = anim.restore + static_cast<hcpp::size_t>(yy) * cw * 4;
                for (hcpp::size_t k = 0; k < static_cast<hcpp::size_t>(cw) * 4; ++k)
                    saved[k] = row[k];
            }
        }

        void restore_rect(ApngAnim &anim, FrameBuf &canvas) noexcept {
            for (hcpp::u32 yy = 0; yy < anim.prev_h; ++yy) {
                hcpp::u8 *row =
                    canvas.data +
                    static_cast<hcpp::size_t>(anim.prev_y + yy) * canvas.pitch +
                    static_cast<hcpp::size_t>(anim.prev_x) * 4;
                const hcpp::u8 *saved =
                    anim.restore + static_cast<hcpp::size_t>(yy) * anim.prev_w * 4;
                for (hcpp::size_t k = 0; k < static_cast<hcpp::size_t>(anim.prev_w) * 4;
                     ++k)
                    row[k] = saved[k];
            }
        }

    }  // namespace

    bool apng_probe(const hcpp::u8 *src, hcpp::size_t size) noexcept {
        if (src == nullptr || size < 8) return false;
        for (int i = 0; i < 8; ++i)
            if (src[i] != apng_sig[i]) return false;
        hcpp::size_t pos = 8;
        for (hcpp::u32 guard = 0; guard < 64; ++guard) {
            hcpp::u32 len = 0;
            if (!chunk_at(src, size, pos, len)) return false;
            if (type_eq(src + pos + 4, "acTL")) return true;
            if (type_eq(src + pos + 4, "IDAT")) return false;  // acTL precedes IDAT
            pos += 12 + len;
        }
        return false;
    }

    bool apng_anim_open(ApngAnim &anim, const hcpp::u8 *src, hcpp::size_t size) noexcept {
        anim = ApngAnim{};
        if (src == nullptr || size < 8) return false;
        for (int i = 0; i < 8; ++i)
            if (src[i] != apng_sig[i]) return false;
        anim.src = src;
        anim.size = size;
        anim.pos = 8;
        // First chunk must be IHDR.
        hcpp::u32 len = 0;
        if (!chunk_at(src, size, anim.pos, len) ||
            !type_eq(src + anim.pos + 4, "IHDR") || len != 13)
            return false;
        const hcpp::u8 *ihdr = src + anim.pos + 8;
        anim.canvas_w = read_be32(ihdr);
        anim.canvas_h = read_be32(ihdr + 4);
        anim.color_type = ihdr[9];
        if (!dims_valid(anim.canvas_w, anim.canvas_h) || ihdr[8] != 8 ||
            ihdr[10] != 0 || ihdr[11] != 0 || ihdr[12] != 0)
            return false;
        anim.pos += 12 + len;
        return true;
    }

    bool apng_anim_next(ApngAnim &anim, FrameBuf &canvas, hcpp::u16 &delay_ms,
                        Arena &arena) noexcept {
        if (anim.frames_done == 0) {
            if (frame_prepare(&canvas, anim.canvas_w, anim.canvas_h, arena) !=
                CodecStatus::Ok)
                return false;
            fill_transparent(canvas, 0, 0, anim.canvas_w, anim.canvas_h);
        } else if (anim.prev_dispose == 1) {
            fill_transparent(canvas, anim.prev_x, anim.prev_y, anim.prev_w,
                             anim.prev_h);
        } else if (anim.prev_dispose == 2 && anim.restore != nullptr) {
            restore_rect(anim, canvas);
        }

        // Scan for the next frame: an optional acTL/PLTE first, then
        // fcTL + its data chunks (IDAT for the default image, fdAT for
        // all others).
        for (;;) {
            hcpp::u32 len = 0;
            if (!chunk_at(anim.src, anim.size, anim.pos, len)) return false;
            const hcpp::u8 *chunk = anim.src + anim.pos;
            const hcpp::u8 *data = chunk + 8;
            if (type_eq(chunk + 4, "acTL")) {
                if (len != 8) return false;
                anim.frames_declared = read_be32(data);
                anim.plays = read_be32(data + 4);
                anim.pos += 12 + len;
                continue;
            }
            if (type_eq(chunk + 4, "PLTE")) {
                if (len == 0 || (len % 3) != 0 || len > 768) return false;
                anim.palette = data;
                anim.palette_size = len;
                anim.pos += 12 + len;
                continue;
            }
            if (type_eq(chunk + 4, "IEND")) return false;
            break;
        }

        // Expect an fcTL now (or a bare IDAT default image, which is a
        // static fallback — skip it for the animation).
        hcpp::u32 len = 0;
        FrameCtl fc{};
        bool saw_fctl = false;
        if (chunk_at(anim.src, anim.size, anim.pos, len) &&
            type_eq(anim.src + anim.pos + 4, "fcTL")) {
            if (!parse_fctl(anim.src + anim.pos + 8, len, fc)) return false;
            saw_fctl = true;
            anim.pos += 12 + len;
        }

        // Measure the frame's compressed payload (IDAT* or fdAT*), the
        // extent ends at the next fcTL/IEND.
        hcpp::size_t scan = anim.pos;
        hcpp::size_t ztotal = 0;
        for (;;) {
            hcpp::u32 clen = 0;
            if (!chunk_at(anim.src, anim.size, scan, clen)) return false;
            const hcpp::u8 *tag = anim.src + scan + 4;
            if (type_eq(tag, "fcTL") || type_eq(tag, "IEND")) break;
            if (type_eq(tag, "IDAT")) {
                ztotal += clen;
            } else if (type_eq(tag, "fdAT")) {
                if (clen < 4) return false;
                ztotal += clen - 4;
            }
            scan += 12 + clen;
        }
        if (ztotal == 0) {
            anim.pos = scan;
            return saw_fctl && apng_anim_next(anim, canvas, delay_ms, arena);
        }
        if (ztotal > anim.size) return false;
        hcpp::u8 *zdata =
            static_cast<hcpp::u8 *>(arena_alloc(arena, ztotal, 8));
        if (zdata == nullptr) return false;
        hcpp::size_t zoff = 0;
        while (anim.pos < scan) {
            hcpp::u32 clen = read_be32(anim.src + anim.pos);
            const hcpp::u8 *tag = anim.src + anim.pos + 4;
            const hcpp::u8 *body = anim.src + anim.pos + 8;
            if (type_eq(tag, "IDAT")) {
                for (hcpp::u32 k = 0; k < clen; ++k) zdata[zoff++] = body[k];
            } else if (type_eq(tag, "fdAT")) {
                for (hcpp::u32 k = 4; k < clen; ++k) zdata[zoff++] = body[k];
            }
            anim.pos += 12 + clen;
        }
        anim.pos = scan;

        // Decode frame pixels.
        const hcpp::u32 fw = saw_fctl ? fc.w : anim.canvas_w;
        const hcpp::u32 fh = saw_fctl ? fc.h : anim.canvas_h;
        const hcpp::u32 fx = saw_fctl ? fc.x : 0;
        const hcpp::u32 fy = saw_fctl ? fc.y : 0;
        if (!dims_valid(fw, fh) || fx >= anim.canvas_w || fy >= anim.canvas_h ||
            fw > anim.canvas_w - fx || fh > anim.canvas_h - fy)
            return false;
        hcpp::u8 *rgba = static_cast<hcpp::u8 *>(
            arena_alloc(arena, static_cast<hcpp::size_t>(fw) * fh * 4, 8));
        if (rgba == nullptr) return false;
        if (png_pixels_decode(fw, fh, anim.color_type, anim.palette,
                              anim.palette_size, zdata, zoff, rgba,
                              static_cast<hcpp::size_t>(fw) * fh * 4,
                              arena) != CodecStatus::Ok)
            return false;

        // Composite.
        const hcpp::u32 cw = fx < anim.canvas_w
                                 ? (fw < anim.canvas_w - fx ? fw : anim.canvas_w - fx)
                                 : 0;
        const hcpp::u32 ch = fy < anim.canvas_h
                                 ? (fh < anim.canvas_h - fy ? fh : anim.canvas_h - fy)
                                 : 0;
        if (saw_fctl && fc.dispose == 2 && cw != 0 && ch != 0)
            snapshot(anim, canvas, fc, cw, ch, arena);
        const bool over = saw_fctl && fc.blend == 1;
        for (hcpp::u32 yy = 0; yy < ch; ++yy) {
            for (hcpp::u32 xx = 0; xx < cw; ++xx) {
                const hcpp::u8 *s = rgba +
                                    (static_cast<hcpp::size_t>(yy) * fw + xx) * 4;
                hcpp::u8 *d = canvas.data +
                              static_cast<hcpp::size_t>(fy + yy) * canvas.pitch +
                              static_cast<hcpp::size_t>(fx + xx) * 4;
                if (!over) {
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = s[3];
                } else {
                    // source-over: out = s + d * (1 - sa)
                    const hcpp::u32 sa = s[3];
                    d[0] = static_cast<hcpp::u8>(s[0] + (d[0] * (255 - sa) + 127) / 255);
                    d[1] = static_cast<hcpp::u8>(s[1] + (d[1] * (255 - sa) + 127) / 255);
                    d[2] = static_cast<hcpp::u8>(s[2] + (d[2] * (255 - sa) + 127) / 255);
                    d[3] = static_cast<hcpp::u8>(sa + (d[3] * (255 - sa) + 127) / 255);
                }
            }
        }
        anim.prev_dispose = saw_fctl ? fc.dispose : 0;
        anim.prev_x = fx;
        anim.prev_y = fy;
        anim.prev_w = cw;
        anim.prev_h = ch;
        const hcpp::u32 den = saw_fctl && fc.delay_den != 0 ? fc.delay_den : 100;
        delay_ms = saw_fctl
                       ? static_cast<hcpp::u16>(
                             static_cast<hcpp::u32>(fc.delay_num) * 1000 / den)
                       : 0;
        anim.delay_ms = delay_ms;
        ++anim.frames_done;
        return true;
    }

    hcpp::u32 apng_frame_count(const hcpp::u8 *src, hcpp::size_t size) noexcept {
        ApngAnim anim;
        if (!apng_anim_open(anim, src, size)) return 0;
        // acTL is always before IDAT; scan early chunks.
        hcpp::size_t pos = anim.pos;
        for (hcpp::u32 guard = 0; guard < 64; ++guard) {
            hcpp::u32 len = 0;
            if (!chunk_at(src, size, pos, len)) return 0;
            if (type_eq(src + pos + 4, "acTL"))
                return len == 8 ? read_be32(src + pos + 8) : 0;
            if (type_eq(src + pos + 4, "IDAT")) return 0;
            pos += 12 + len;
        }
        return 0;
    }

    CodecStatus apng_decode(const hcpp::u8 *input, hcpp::size_t input_size,
                            void *output, Arena &arena) noexcept {
        auto *frame = static_cast<FrameBuf *>(output);
        if (input == nullptr || frame == nullptr) return CodecStatus::BadInput;
        if (!apng_probe(input, input_size)) return CodecStatus::BadInput;
        ApngAnim anim;
        if (!apng_anim_open(anim, input, input_size)) return CodecStatus::BadInput;
        hcpp::u16 delay = 0;
        if (!apng_anim_next(anim, *frame, delay, arena)) return CodecStatus::BadInput;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::image
