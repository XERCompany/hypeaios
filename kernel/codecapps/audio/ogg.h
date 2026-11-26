#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    struct OggPage {
        const hcpp::u8 *body;
        hcpp::u64 granule;
        hcpp::u32 serial;
        hcpp::u32 sequence;
        hcpp::u32 crc;
        hcpp::u8 flags;
        hcpp::u8 segment_count;
        hcpp::u8 lacing[255];
        hcpp::size_t header_size;
        hcpp::size_t body_size;
    };

    struct OggPackets {
        hcpp::u8 *payload;
        hcpp::size_t payload_cap;
        hcpp::size_t payload_used;
        hcpp::u32 *offsets;
        hcpp::u32 *sizes;
        hcpp::size_t packet_cap;
        hcpp::size_t packet_count;
        hcpp::u32 serial;
        bool truncated;
    };

    bool ogg_parse_page(const hcpp::u8 *input, hcpp::size_t size, hcpp::size_t offset,
                        OggPage &page) noexcept;
    bool ogg_sniff_vorbis(const hcpp::u8 *data, hcpp::size_t size) noexcept;
    CodecStatus audio_ogg_decode(const hcpp::u8 *input, hcpp::size_t input_size, void *output,
                                 Arena &arena) noexcept;

}  // namespace hype::codec
