#pragma once

#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"

namespace hype::codec {

    constexpr hcpp::size_t codec_max_descriptors = 40;

    bool codec_register(const CodecDescriptor &descriptor) noexcept;
    hcpp::size_t codec_count() noexcept;
    CodecId codec_probe(const hcpp::u8 *buffer, hcpp::size_t size) noexcept;
    CodecStatus codec_decode(CodecId id, const hcpp::u8 *input, hcpp::size_t input_size,
                             void *output, Arena &arena) noexcept;
    CodecStatus codec_encode(CodecId id, const void *input, hcpp::u8 *output,
                             hcpp::size_t output_cap, hcpp::size_t &written, Arena &arena) noexcept;
    const CodecDescriptor *codec_info(CodecId id) noexcept;
    const CodecDescriptor *codec_at(hcpp::size_t index) noexcept;
    void codec_report() noexcept;

    void codec_register_builtins() noexcept;
    bool codec_probe_selftest() noexcept;
    bool codec_arena_selftest() noexcept;

}  // namespace hype::codec
