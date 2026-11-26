#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/alac.h"

namespace hype::audio {

    enum class AlacDecodeStatus : hcpp::u8 { Ok, Unsupported, BadPacket, NotInitialized };

    struct AlacBitReader {
        const hcpp::u8 *data;
        hcpp::u32 size;
        hcpp::u32 position;
        bool overflow;
    };

    inline void alac_bits_init(AlacBitReader &reader, const void *data, hcpp::u32 size) noexcept {
        reader.data = static_cast<const hcpp::u8 *>(data);
        reader.size = size;
        reader.position = 0;
        reader.overflow = false;
    }

    inline hcpp::u32 alac_bits_read(AlacBitReader &reader, hcpp::u8 count) noexcept {
        if (reader.data == nullptr || count == 0 || count > 32) {
            reader.overflow = true;
            return 0;
        }
        hcpp::u32 value = 0;
        for (hcpp::u8 i = 0; i < count; ++i) {
            const hcpp::u32 bit_index = reader.position + i;
            if (bit_index >= reader.size * 8u) {
                reader.overflow = true;
                break;
            }
            value = (value << 1) | ((reader.data[bit_index >> 3] >> (7 - (bit_index & 7))) & 1u);
        }
        reader.position += count;
        return value;
    }

    struct AlacDecoderState {
        AlacConfig config;
        bool initialized;
        hcpp::u32 frames_decoded;
        AlacDecodeStatus status;
    };

    bool alac_decoder_initialize(AlacDecoderState &state, const AlacConfig &config) noexcept;
    void alac_decoder_reset(AlacDecoderState &state) noexcept;
    hcpp::u32 alac_decoder_decode(AlacDecoderState &state, const void *packet, hcpp::u32 packet_bytes,
                                  hcpp::i16 *output, hcpp::u32 max_frames) noexcept;

}  // namespace hype::audio
