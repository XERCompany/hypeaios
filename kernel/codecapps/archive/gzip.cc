#include "kernel/codecapps/archive/archive.h"
#include "kernel/codecapps/arena.h"
#include "kernel/ztext/ztext.h"

// GZIP (RFC 1952) member decode/encode over ZText. Decode output is a flat
// BlobOut; when no caller buffer is given the decoder allocates the ISIZE
// trailer length from the arena.

namespace hype::codec::archive {

    namespace {
        hcpp::u32 rd32(const hcpp::u8 *p) noexcept {
            return static_cast<hcpp::u32>(p[0]) | (static_cast<hcpp::u32>(p[1]) << 8) |
                   (static_cast<hcpp::u32>(p[2]) << 16) |
                   (static_cast<hcpp::u32>(p[3]) << 24);
        }
    }  // namespace

    bool gzip_probe(const hcpp::u8 *b, hcpp::size_t n) noexcept {
        return b != nullptr && n >= 3 && b[0] == 0x1F && b[1] == 0x8B && b[2] == 0x08;
    }

    CodecStatus gzip_decode(const hcpp::u8 *src, hcpp::size_t size, void *output,
                            Arena &arena) noexcept {
        BlobOut *out = static_cast<BlobOut *>(output);
        if (src == nullptr || out == nullptr || size < 18 || !gzip_probe(src, size))
            return CodecStatus::BadInput;
        const hcpp::u32 isize = rd32(src + size - 4);
        if (out->data == nullptr) {
            if (out->capacity == 0) out->capacity = isize;
            out->data = static_cast<hcpp::u8 *>(arena_alloc(arena, out->capacity, 8));
            if (out->data == nullptr) return CodecStatus::NoSpace;
        }
        if (out->capacity < isize) return CodecStatus::NoSpace;
        hcpp::size_t produced = 0;
        if (!ztext::ztext_gzip_decode(out->data, out->capacity, src, size, produced))
            return CodecStatus::BadInput;
        out->size = produced;
        return CodecStatus::Ok;
    }

    CodecStatus gzip_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                            hcpp::size_t &written, Arena &) noexcept {
        const BlobOut *in = static_cast<const BlobOut *>(input);
        written = 0;
        if (in == nullptr || output == nullptr ||
            (in->data == nullptr && in->size != 0))
            return CodecStatus::BadInput;
        if (!ztext::ztext_gzip_encode(output, output_cap, in->data, in->size, written))
            return CodecStatus::NoSpace;
        return CodecStatus::Ok;
    }

}  // namespace hype::codec::archive
