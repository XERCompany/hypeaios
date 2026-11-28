#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

// CodecsApps archive formats (issue #180): ZIP, TAR, GZIP.
// Archive decoders do not produce FrameBuf/PcmBuf — `zip_decode` and
// `tar_decode` fill an `ArchiveList` entry table, `gzip_decode` fills a
// flat `BlobOut` buffer. ZIP member extraction is a separate call so a
// caller can list first and only decompress what it needs.

namespace hype::codec::archive {

    constexpr hcpp::size_t archive_max_name = 255;

    struct ArchiveEntry {
        const char *name;     // points into the source archive, NOT NUL-terminated
        hcpp::u16 name_len;
        hcpp::u8 method;      // zip: compression method; tar: typeflag char
        hcpp::u8 flags;       // bit0: directory, bit1: unsupported method
        hcpp::u32 offset;     // payload offset inside the source buffer
        hcpp::u32 size;       // uncompressed size
        hcpp::u32 packed;     // stored/compressed size
        hcpp::u32 crc32;      // zip crc (0 for tar)
    };

    constexpr hcpp::u8 archive_entry_dir = 1;
    constexpr hcpp::u8 archive_entry_unsupported = 2;

    // Decode output for zip/tar: caller fills `entries`+`capacity` (or leaves
    // entries == nullptr to allocate up to capacity entries from the arena).
    struct ArchiveList {
        ArchiveEntry *entries;
        hcpp::size_t capacity;
        hcpp::size_t count;
    };

    // Decode output for gzip / encode input: a flat byte buffer. When
    // `data` is nullptr the decoder allocates the needed size from the arena
    // (gzip carries the original size in its ISIZE trailer).
    struct BlobOut {
        hcpp::u8 *data;
        hcpp::size_t capacity;
        hcpp::size_t size;
    };

    bool zip_probe(const hcpp::u8 *buffer, hcpp::size_t size) noexcept;
    bool tar_probe(const hcpp::u8 *buffer, hcpp::size_t size) noexcept;
    bool gzip_probe(const hcpp::u8 *buffer, hcpp::size_t size) noexcept;

    CodecStatus zip_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;
    CodecStatus tar_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                           Arena &arena) noexcept;
    CodecStatus gzip_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                            Arena &arena) noexcept;
    CodecStatus gzip_encode(const void *input, hcpp::u8 *output, hcpp::size_t output_cap,
                            hcpp::size_t &written, Arena &arena) noexcept;

    // Extract one listed ZIP entry into a caller buffer. Method 0 (stored)
    // copies, method 8 (deflate) goes through ZText inflate; the CRC-32 from
    // the central directory is verified when nonzero.
    CodecStatus zip_extract(const hcpp::u8 *zip, hcpp::size_t zip_size,
                            const ArchiveEntry &entry, hcpp::u8 *dst,
                            hcpp::size_t dst_cap, hcpp::size_t &out_len) noexcept;

    const ArchiveEntry *archive_find(const ArchiveList &list, const char *name) noexcept;

    bool archive_selftest() noexcept;

}  // namespace hype::codec::archive
